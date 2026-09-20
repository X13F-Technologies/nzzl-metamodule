#include "rack.hpp"
#include "pattern.hh"
#include "scales.hh"

using namespace rack;

struct NZZL : engine::Module {
    enum ParamId {
        GROUP_PARAM,
        SUBGROUP_PARAM,
        DENSITY_PARAM,
        LENGTH_PARAM,
        CLOCK_DIV_PARAM,
        OCTAVE_RANGE_PARAM,
        ROOT_PARAM,
        SCALE_PARAM,
        SCALE_LOCK_PARAM,
        SLIDE_PARAM,
        PARAMS_LEN
    };

    enum InputId {
        CLOCK_INPUT,
        RUN_INPUT,
        RESEED_INPUT,
        CV_SCALE_INPUT,
        CV_SEED_INPUT,
        CV_ROOT_INPUT,
        CV_SLIDE_INPUT,
        INPUTS_LEN
    };

    enum OutputId {
        CV_PITCH_OUTPUT,
        GATE_OUTPUT,
        VELOCITY_OUTPUT,
        OUTPUTS_LEN
    };

    nzzl::StepData steps[nzzl::MAX_STEPS] = {};
    int seedIndex    = 0;
    int lastGroup    = -1;
    int lastSubgroup = -1;

    void generateSteps(int group, int subgroup) {
        seedIndex = (group - 1) * 32 + (subgroup - 1);
        nzzl::generatePattern(seedIndex, steps);
    }

    // sequencer state
    int   step = 0;
    int   clockDivCount = 0;
    bool  running = true;
    float clockPeriod = 0.5f;   // seconds, estimated from clock edges
    float clockPhase  = 0.f;    // time since last accepted clock edge (seconds)
    float gateTimer   = 0.f;    // time remaining for current gate high (seconds)
    float heldVelocity = 0.f;   // latched at gate onset (sample-and-hold)
    int   heldPitchIndex = 0;   // latched at gate onset; knobs re-quantize it live
    float heldOctaveRaw  = 0.f;
    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger runTrigger;

    NZZL() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN);

        configParam(GROUP_PARAM,      1.f, 32.f, 1.f,  "Group",        "", 0, 1, 1);
        configParam(SUBGROUP_PARAM,   1.f, 32.f, 1.f,  "Subgroup",     "", 0, 1, 1);
        configParam(DENSITY_PARAM,    1.f, 16.f, 8.f,  "Density");
        configParam(LENGTH_PARAM,     2.f, 16.f, 16.f, "Length",       " steps");
        configParam(CLOCK_DIV_PARAM,  1.f, 16.f, 1.f,  "Clock Divide", "÷");
        configParam(OCTAVE_RANGE_PARAM,1.f, 5.f, 2.f,  "Octave Range", " oct");
        configSwitch(ROOT_PARAM,  0.f, 11.f, 0.f, "Root Note",
            {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"});
        configSwitch(SCALE_PARAM, 0.f, float(nzzl::NUM_SCALES - 1), 2.f, "Scale",
            {"Chromatic", "Major", "Natural Minor", "Dorian", "Phrygian",
             "Phrygian Dominant", "Lydian", "Mixolydian", "Harmonic Minor",
             "Melodic Minor", "Minor Pentatonic", "Blues"});
        configSwitch(SCALE_LOCK_PARAM, 0.f, 1.f, 1.f, "Scale Lock",
            {"Off (unquantized)", "On"});
        configParam(SLIDE_PARAM,      0.f, 1.f,  0.f,  "Slide");

        configInput(CLOCK_INPUT,    "Clock");
        configInput(RUN_INPUT,      "Run");
        configInput(RESEED_INPUT,   "Reseed");
        configInput(CV_SCALE_INPUT, "CV Scale");
        configInput(CV_SEED_INPUT,  "CV Seed");
        configInput(CV_ROOT_INPUT,  "CV Root");
        configInput(CV_SLIDE_INPUT, "CV Slide");

        configOutput(CV_PITCH_OUTPUT, "CV Pitch");
        configOutput(GATE_OUTPUT,     "Gate");
        configOutput(VELOCITY_OUTPUT, "Velocity");
    }

    void process(const ProcessArgs& args) override {
        int length   = (int)std::round(params[LENGTH_PARAM].getValue());
        int clockDiv = (int)std::round(params[CLOCK_DIV_PARAM].getValue());
        int group    = (int)std::round(params[GROUP_PARAM].getValue());
        int subgroup = (int)std::round(params[SUBGROUP_PARAM].getValue());

        // Regenerate step data when seed knobs change
        if (group != lastGroup || subgroup != lastSubgroup) {
            lastGroup    = group;
            lastSubgroup = subgroup;
            generateSteps(group, subgroup);
        }

        // RUN: trigger toggles running state; unpatched = always running
        if (inputs[RUN_INPUT].isConnected()) {
            if (runTrigger.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 2.f))
                running = !running;
        }

        // Clock period estimation and step advance
        clockPhase += args.sampleTime;
        if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f)) {
            // Update period estimate (clamped to sane range: 10ms–4s)
            if (clockPhase > 0.01f && clockPhase < 4.f)
                clockPeriod = clockPhase;
            clockPhase = 0.f;

            if (running) {
                clockDivCount++;
                if (clockDivCount >= clockDiv) {
                    clockDivCount = 0;
                    step = (step + 1) % length;

                    int density = (int)std::round(params[DENSITY_PARAM].getValue());
                    bool active = steps[step].weight <= density;
                    if (active) {
                        float gateDuration = steps[step].gateLength
                                           * clockPeriod * clockDiv;
                        gateTimer = gateDuration;
                        heldVelocity   = steps[step].velocity;
                        heldPitchIndex = steps[step].pitchIndex;
                        heldOctaveRaw  = steps[step].octaveRaw;
                    }
                }
            }
        }

        // Gate output
        gateTimer -= args.sampleTime;
        outputs[GATE_OUTPUT].setVoltage(gateTimer > 0.f ? 10.f : 0.f);

        // VELOCITY: latched at gate onset, held through silent steps (0–10V)
        outputs[VELOCITY_OUTPUT].setVoltage(heldVelocity * 10.f);

        // CV PITCH: the step is latched at gate onset, but the quantizer runs
        // every sample so turning ROOT / SCALE / OCTAVE transposes the held
        // note immediately instead of waiting for the next gate.
        nzzl::QuantizeParams qp;
        qp.scaleIndex  = (int)std::round(params[SCALE_PARAM].getValue());
        qp.root        = (int)std::round(params[ROOT_PARAM].getValue());
        qp.octaveRange = (int)std::round(params[OCTAVE_RANGE_PARAM].getValue());
        qp.scaleLock   = params[SCALE_LOCK_PARAM].getValue() > 0.5f;
        outputs[CV_PITCH_OUTPUT].setVoltage(
            qp.scaleLock ? nzzl::quantizedVoltage(heldPitchIndex, heldOctaveRaw, qp)
                         : nzzl::rawVoltage(heldPitchIndex, heldOctaveRaw, qp));
    }
};

struct NZZLWidget : app::ModuleWidget {
    NZZLWidget(NZZL* module) {
        setModule(module);
        box.size = math::Vec(app::RACK_GRID_WIDTH * 16, app::RACK_GRID_HEIGHT);

        // ── Knobs ────────────────────────────────────────────────────────────
        float x = 15.f;
        float y = 40.f;
        float dy = 40.f;

        addParam(createParamCentered<componentlibrary::RoundBlackKnob>(
            math::Vec(x, y), module, NZZL::GROUP_PARAM));
        addParam(createParamCentered<componentlibrary::RoundBlackKnob>(
            math::Vec(x + 40, y), module, NZZL::SUBGROUP_PARAM));

        y += dy;
        addParam(createParamCentered<componentlibrary::RoundBlackKnob>(
            math::Vec(x, y), module, NZZL::DENSITY_PARAM));
        addParam(createParamCentered<componentlibrary::RoundBlackKnob>(
            math::Vec(x + 40, y), module, NZZL::LENGTH_PARAM));

        y += dy;
        addParam(createParamCentered<componentlibrary::RoundBlackKnob>(
            math::Vec(x, y), module, NZZL::CLOCK_DIV_PARAM));
        addParam(createParamCentered<componentlibrary::RoundBlackKnob>(
            math::Vec(x + 40, y), module, NZZL::OCTAVE_RANGE_PARAM));

        y += dy;
        addParam(createParamCentered<componentlibrary::RoundBlackKnob>(
            math::Vec(x, y), module, NZZL::ROOT_PARAM));
        addParam(createParamCentered<componentlibrary::RoundBlackKnob>(
            math::Vec(x + 40, y), module, NZZL::SCALE_PARAM));

        y += dy;
        addParam(createParamCentered<componentlibrary::CKSS>(
            math::Vec(x, y), module, NZZL::SCALE_LOCK_PARAM));
        addParam(createParamCentered<componentlibrary::RoundBlackKnob>(
            math::Vec(x + 40, y), module, NZZL::SLIDE_PARAM));

        // ── Inputs ───────────────────────────────────────────────────────────
        float ix = 120.f;
        float iy = 40.f;

        addInput(createInputCentered<componentlibrary::PJ301MPort>(
            math::Vec(ix, iy), module, NZZL::CLOCK_INPUT));
        addInput(createInputCentered<componentlibrary::PJ301MPort>(
            math::Vec(ix, iy + 35), module, NZZL::RUN_INPUT));
        addInput(createInputCentered<componentlibrary::PJ301MPort>(
            math::Vec(ix, iy + 70), module, NZZL::RESEED_INPUT));
        addInput(createInputCentered<componentlibrary::PJ301MPort>(
            math::Vec(ix, iy + 105), module, NZZL::CV_SCALE_INPUT));
        addInput(createInputCentered<componentlibrary::PJ301MPort>(
            math::Vec(ix, iy + 140), module, NZZL::CV_SEED_INPUT));
        addInput(createInputCentered<componentlibrary::PJ301MPort>(
            math::Vec(ix, iy + 175), module, NZZL::CV_ROOT_INPUT));
        addInput(createInputCentered<componentlibrary::PJ301MPort>(
            math::Vec(ix, iy + 210), module, NZZL::CV_SLIDE_INPUT));

        // ── Outputs ──────────────────────────────────────────────────────────
        float ox = 180.f;
        float oy = 40.f;

        addOutput(createOutputCentered<componentlibrary::PJ301MPort>(
            math::Vec(ox, oy), module, NZZL::CV_PITCH_OUTPUT));
        addOutput(createOutputCentered<componentlibrary::PJ301MPort>(
            math::Vec(ox, oy + 35), module, NZZL::GATE_OUTPUT));
        addOutput(createOutputCentered<componentlibrary::PJ301MPort>(
            math::Vec(ox, oy + 70), module, NZZL::VELOCITY_OUTPUT));
    }
};

Model* modelNZZL = createModel<NZZL, NZZLWidget>("NZZL");
