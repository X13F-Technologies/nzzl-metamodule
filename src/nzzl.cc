#include "rack.hpp"
#include "pattern.hh"
#include "scales.hh"
#include "engine.hh"

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

    // All sequencer state and timing lives in the pure engine (engine.hh),
    // so it can be driven by the native test harness. This file only maps
    // params and jacks onto it.
    nzzl::Engine engine;

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
        // Position 0 is unquantized — this knob replaces the old SCALE LOCK
        // switch, so one control answers "how are these notes pitched?".
        configSwitch(SCALE_PARAM, 0.f, float(nzzl::NUM_SCALES - 1), 3.f, "Scale",
            {"Unquantized", "Chromatic", "Major", "Natural Minor", "Dorian",
             "Phrygian", "Phrygian Dominant", "Lydian", "Mixolydian",
             "Harmonic Minor", "Melodic Minor", "Minor Pentatonic"});
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
        int group    = (int)std::round(params[GROUP_PARAM].getValue());
        int subgroup = (int)std::round(params[SUBGROUP_PARAM].getValue());

        // Regenerate step data when seed knobs change
        if (group != lastGroup || subgroup != lastSubgroup) {
            lastGroup    = group;
            lastSubgroup = subgroup;
            generateSteps(group, subgroup);
        }

        nzzl::EngineParams p;
        p.density  = (int)std::round(params[DENSITY_PARAM].getValue());
        p.length   = (int)std::round(params[LENGTH_PARAM].getValue());
        p.clockDiv = (int)std::round(params[CLOCK_DIV_PARAM].getValue());
        p.slide    = params[SLIDE_PARAM].getValue();
        p.quant.scaleIndex  = (int)std::round(params[SCALE_PARAM].getValue());
        p.quant.root        = (int)std::round(params[ROOT_PARAM].getValue());
        p.quant.octaveRange = (int)std::round(params[OCTAVE_RANGE_PARAM].getValue());

        nzzl::EngineInputs in;
        in.clock           = inputs[CLOCK_INPUT].getVoltage();
        in.run             = inputs[RUN_INPUT].getVoltage();
        in.reseed          = inputs[RESEED_INPUT].getVoltage();
        in.runConnected    = inputs[RUN_INPUT].isConnected();
        in.reseedConnected = inputs[RESEED_INPUT].isConnected();

        nzzl::EngineOutputs out = engine.process(args.sampleTime, in, steps, p);

        outputs[CV_PITCH_OUTPUT].setVoltage(out.pitch);
        outputs[GATE_OUTPUT].setVoltage(out.gate);
        outputs[VELOCITY_OUTPUT].setVoltage(out.velocity);
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
        addParam(createParamCentered<componentlibrary::RoundBlackKnob>(
            math::Vec(x, y), module, NZZL::SLIDE_PARAM));
        // x + 40 on this row is free — SCALE LOCK folded into the SCALE knob.

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
