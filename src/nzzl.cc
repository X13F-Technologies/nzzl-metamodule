#include "rack.hpp"
#include "pattern.hh"
#include "scales.hh"
#include "engine.hh"
#include "cv.hh"
#include "display.hh"
#include "roll.hh"
#include <vector>

#ifdef NZZL_METAMODULE
#include <metamodule/VCVTextDisplay.hpp>
#endif

using namespace rack;

// Thin adapter: maps params and jacks onto the pure layer. Every decision of
// substance lives in src/*.hh, where the native harness can reach it.

struct NZZL : engine::Module {
    enum ParamId {
        // Item 16: STYLE takes GROUP's slot. SUBGROUP's slot is held for
        // PITCH LENGTH (item 7), so the other params keep their ids.
        STYLE_PARAM,
        PITCH_LENGTH_RESERVED_PARAM,
        DENSITY_PARAM,
        LENGTH_PARAM,
        CLOCK_DIV_PARAM,
        OCTAVE_RANGE_PARAM,
        ROOT_PARAM,
        SCALE_PARAM,
        SLIDE_PARAM,
        SLIDE_SLOPE_PARAM,
        GATE_PARAM,
        ACCENT_PARAM,
        PITCH_OFFSET_PARAM,
        SHIFT_PARAM,
        SWING_PARAM,
        RANDOM_PARAM,
        PARAMS_LEN
    };

    enum InputId {
        CLOCK_INPUT,
        RUN_INPUT,
        RESEED_INPUT,
        CV_SCALE_INPUT,
        CV_DENSITY_INPUT,     // was CV SEED (item 16)
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

    // MetaModule text displays are addressed by light id, and there are no
    // real lights on this panel, so the display ids start at zero.
    enum LightId {
        STEPS_DISPLAY,        // "8/16": density / length
        STYLE_DISPLAY,
        SCALE_DISPLAY,
        LIGHTS_LEN
    };

    nzzl::RollPlayer player;     // the roll, and which row is playing
    nzzl::Engine     engine;

    dsp::SchmittTrigger randomButton;
    int displayDensity = 8;
    int displayLength  = 16;
    int displayStyle   = nzzl::STYLE_POS_BASS;
    int displayScale  = nzzl::SCALE_NAT_MINOR;
    int displayRoot   = 0;
    float reseedFlash = 0.f;     // seconds remaining on the display indicator

    NZZL() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Six detented positions, one row of the roll each. No CV.
        configSwitch(STYLE_PARAM, 0.f, float(nzzl::NUM_STYLES - 1),
            float(nzzl::STYLE_POS_BASS), "Style",
            {"Bass (303)", "Random", "Arp up", "Arp down", "Arp up-down",
             "Arp down-up"});
        configParam(DENSITY_PARAM,     1.f, 16.f, 8.f,  "Density");
        configParam(LENGTH_PARAM,      2.f, 16.f, 16.f, "Length", " steps");
        configParam(CLOCK_DIV_PARAM,   1.f, 16.f, 1.f,  "Clock Divide", "÷");
        configParam(OCTAVE_RANGE_PARAM,1.f, 5.f,  2.f,  "Octave Range", " oct");
        configSwitch(ROOT_PARAM,  0.f, 11.f, 0.f, "Root Note",
            {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"});
        // Position 0 is unquantized — this knob replaces the old SCALE LOCK
        // switch, so one control answers "how are these notes pitched?".
        configSwitch(SCALE_PARAM, 0.f, float(nzzl::NUM_SCALES - 1),
            float(nzzl::SCALE_NAT_MINOR), "Scale",
            {"Unquantized", "Major", "Natural Minor", "Dorian", "Phrygian",
             "Mixolydian", "Lydian", "Harmonic Minor", "Melodic Minor",
             "Pentatonic Major", "Pentatonic Minor", "Chromatic", "Whole Tone",
             "Minor triad", "Major triad", "Minor 7th", "Dominant 7th",
             "Minor 9th"});
        configParam(SLIDE_PARAM, 0.f, 1.f, 0.f, "Slide");
        configParam(SLIDE_SLOPE_PARAM, 0.f, 1.f, 0.5f, "Slide Shape",
                    "", 0.f, 1.f, 0.f);
        configParam(PITCH_OFFSET_PARAM, -7.f, 7.f, 0.f, "Pitch Offset",
                    " degrees");
        configParam(SHIFT_PARAM, -8.f, 8.f, 0.f, "Shift", " steps");
        configParam(SWING_PARAM, 0.f, 100.f, 0.f, "Swing", "%");
        configButton(RANDOM_PARAM, "Generate (roll a new pattern)");
        getParamQuantity(PITCH_OFFSET_PARAM)->snapEnabled = true;
        getParamQuantity(SHIFT_PARAM)->snapEnabled = true;
        // GATE scales the pattern's three note lengths together. Past 100%
        // notes run into the next step and tie.
        configParam(GATE_PARAM,   1.f, 200.f, 100.f, "Gate", "%");
        // ACCENT scales the contrast between the three velocity layers.
        configParam(ACCENT_PARAM, 0.f, 100.f, 100.f, "Accent", "%");

        getParamQuantity(DENSITY_PARAM)->snapEnabled    = true;
        getParamQuantity(LENGTH_PARAM)->snapEnabled     = true;
        getParamQuantity(CLOCK_DIV_PARAM)->snapEnabled  = true;
        getParamQuantity(OCTAVE_RANGE_PARAM)->snapEnabled = true;

        configInput(CLOCK_INPUT,    "Clock");
        configInput(RUN_INPUT,      "Run (trigger toggles)");
        configInput(RESEED_INPUT,   "Reseed / generate (trigger)");
        configInput(CV_SCALE_INPUT, "CV Scale (override, 0-10V spans the list)");
        configInput(CV_DENSITY_INPUT, "CV Density (offset, 10V spans 1-16)");
        configInput(CV_ROOT_INPUT,  "CV Root (override, V/oct)");
        configInput(CV_SLIDE_INPUT, "CV Slide (summed with the step's slide)");

        configOutput(CV_PITCH_OUTPUT, "CV Pitch");
        configOutput(GATE_OUTPUT,     "Gate");
        configOutput(VELOCITY_OUTPUT, "Velocity");

        // A freshly placed module needs something to play: roll once. A
        // loaded patch then replaces this with its saved roll.
        generate(false);
    }

    // Generate — the one non-deterministic moment in the module, and only on
    // a RESEED trigger, the RANDOM button, or a new module being placed.
    // Everything after the draw is the pure, harness-tested roll.
    void generate(bool flash = true) {
        nzzl::RollEntropy e;
        for (int i = 0; i < nzzl::NUM_LANES; i++)
            e.lane[i] = random::u32();
        player.generate(e);
        if (flash)
            reseedFlash = 0.35f;     // brief display indicator, per the spec
    }

    void process(const ProcessArgs& args) override {
        if (engine.generateRequested) {
            engine.generateRequested = false;
            generate();
        }
        // The RANDOM button does exactly what the RESEED jack does.
        if (randomButton.process(params[RANDOM_PARAM].getValue(), 0.1f, 0.5f))
            generate();
        if (reseedFlash > 0.f) reseedFlash -= args.sampleTime;

        // A new roll or STYLE position waits for the loop to wrap (roll.hh).
        const int style = (int)std::round(params[STYLE_PARAM].getValue());
        player.update(style, engine.loopWrapped);

        nzzl::EngineParams p;
        p.density  = nzzl::applyDensityCv(
            (int)std::round(params[DENSITY_PARAM].getValue()),
            inputs[CV_DENSITY_INPUT].getVoltage(),
            inputs[CV_DENSITY_INPUT].isConnected());
        p.length   = (int)std::round(params[LENGTH_PARAM].getValue());
        p.clockDiv = (int)std::round(params[CLOCK_DIV_PARAM].getValue());
        p.slideKnob = params[SLIDE_PARAM].getValue();
        p.slideCv   = nzzl::cvSlideAmount(
            inputs[CV_SLIDE_INPUT].getVoltage(),
            inputs[CV_SLIDE_INPUT].isConnected());
        p.slideSlope = params[SLIDE_SLOPE_PARAM].getValue();
        p.gate      = params[GATE_PARAM].getValue() / 100.f;
        p.accent    = params[ACCENT_PARAM].getValue() / 100.f;
        p.shift     = (int)std::round(params[SHIFT_PARAM].getValue());
        p.swing     = params[SWING_PARAM].getValue() / 100.f;
        p.quant.degreeOffset =
            (int)std::round(params[PITCH_OFFSET_PARAM].getValue());

        p.quant.octaveRange = (int)std::round(params[OCTAVE_RANGE_PARAM].getValue());
        p.quant.scaleIndex  = nzzl::applyScaleCv(
            (int)std::round(params[SCALE_PARAM].getValue()),
            inputs[CV_SCALE_INPUT].getVoltage(),
            inputs[CV_SCALE_INPUT].isConnected());
        p.quant.root = nzzl::applyRootCv(
            (int)std::round(params[ROOT_PARAM].getValue()),
            inputs[CV_ROOT_INPUT].getVoltage(),
            inputs[CV_ROOT_INPUT].isConnected());

        displayScale   = p.quant.scaleIndex;
        displayRoot    = p.quant.root;
        displayDensity = p.density;
        displayLength  = p.length;
        displayStyle   = player.style < 0 ? style : player.style;

        nzzl::EngineInputs in;
        in.clock           = inputs[CLOCK_INPUT].getVoltage();
        in.run             = inputs[RUN_INPUT].getVoltage();
        in.reseed          = inputs[RESEED_INPUT].getVoltage();
        in.runConnected    = inputs[RUN_INPUT].isConnected();
        in.reseedConnected = inputs[RESEED_INPUT].isConnected();

        const nzzl::EngineOutputs out =
            engine.process(args.sampleTime, in, player.current, p);

        outputs[CV_PITCH_OUTPUT].setVoltage(out.pitch);
        outputs[GATE_OUTPUT].setVoltage(out.gate);
        outputs[VELOCITY_OUTPUT].setVoltage(out.velocity);
    }

    // Patch state. Item 16: the whole roll is saved — every row, not just
    // the one STYLE points at — so a reloaded patch plays what you heard and
    // the STYLE knob still behaves the same. A pending Generate is saved as
    // the roll, since that is what was about to play. Plus the run state.
    //
    // The step position is deliberately NOT saved: a reloaded patch should
    // start from a predictable place rather than halfway through a phrase.
    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "running", json_boolean(engine.running));
        std::vector<char> hex(nzzl::ROLL_HEX_LEN + 1);
        nzzl::encodeRoll(player.newest(), hex.data());
        json_object_set_new(rootJ, "rollFormat", json_integer(nzzl::ROLL_FORMAT));
        json_object_set_new(rootJ, "roll", json_string(hex.data()));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        if (json_t* r = json_object_get(rootJ, "running"))
            engine.running = json_boolean_value(r);
        // A patch from before item 16, or a malformed roll, keeps the fresh
        // roll the constructor made. Breaking test-era patches is accepted.
        json_t* fmt  = json_object_get(rootJ, "rollFormat");
        json_t* roll = json_object_get(rootJ, "roll");
        if (fmt && roll && json_integer_value(fmt) == nzzl::ROLL_FORMAT) {
            nzzl::Roll loaded;
            if (nzzl::decodeRoll(json_string_value(roll), loaded))
                player.load(loaded);
        }
    }
};

// ── Display ─────────────────────────────────────────────────────────────────
// On MetaModule a text display must derive from MetaModule::VCVTextDisplay;
// in VCV Rack it is an ordinary LightWidget. Same draw() either way.

#ifdef NZZL_METAMODULE
using NZZLDisplayBase = MetaModule::VCVTextDisplay;
#else
using NZZLDisplayBase = app::LightWidget;
#endif

struct NZZLDisplay : NZZLDisplayBase {
    NZZL* module = nullptr;
    int   line   = 0;          // 0 = density/length, 1 = style, 2 = root/scale
    float fontSize = 13.f;

    // Deliberately draw() rather than drawLayer(): drawLayer is a Rack-only
    // "lights layer" concept, and the MetaModule firmware reads the text from
    // an ordinary draw pass.
    void draw(const DrawArgs& args) override {
        nzzl::DisplayText d;
        if (module)
            nzzl::buildDisplay(module->displayDensity, module->displayLength,
                               module->displayStyle, module->displayScale,
                               module->displayRoot, d);
        else
            nzzl::buildDisplay(8, 16, nzzl::STYLE_POS_BASS, 3, 0, d);   // browser preview

        const char* text = (line == 0) ? d.steps : (line == 1) ? d.style : d.scale;
        const bool flash = module && module->reseedFlash > 0.f;

#ifndef NZZL_METAMODULE
        // MetaModule supplies the face itself via the `font` field set on the
        // widget, so only the Rack build loads a TTF. Bailing out on a failed
        // font load must not be allowed to skip nvgText on hardware.
        std::shared_ptr<window::Font> font =
            APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        if (!font)
            return;
        nvgFontFaceId(args.vg, font->handle);
#endif
        nvgFontSize(args.vg, fontSize);
        nvgFillColor(args.vg, flash ? nvgRGB(0xff, 0xcc, 0x44)
                                    : nvgRGB(0x33, 0xff, 0x99));
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgText(args.vg, 2.f, 1.f, text, NULL);
    }
};

// ── Panel ───────────────────────────────────────────────────────────────────
// Deliberately left until last (docs/DESIGN.md § Deferred decisions). Three
// columns: controls on the left, inputs in the middle, outputs on the right,
// with the display across the top.

struct NZZLLabel : widget::Widget {
    std::string text;
    float size = 9.f;
    NVGcolor color = nvgRGB(0xcc, 0xcc, 0xcc);

    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font =
            APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        if (!font)
            return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, size);
        nvgFillColor(args.vg, color);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        // Draw at the centre of a real box — a zero-size widget can be
        // scissored away before anything reaches the screen.
        nvgText(args.vg, box.size.x * 0.5f, 0.f, text.c_str(), NULL);
    }
};

constexpr float LABEL_W = 80.f;
constexpr float LABEL_H = 14.f;

struct NZZLWidget : app::ModuleWidget {
    void addLabel(math::Vec pos, const std::string& text, float size = 9.f) {
        NZZLLabel* l = new NZZLLabel;
        l->box.size = math::Vec(LABEL_W, LABEL_H);
        l->box.pos  = math::Vec(pos.x - LABEL_W * 0.5f, pos.y);
        l->text = text;
        l->size = size;
        addChild(l);
    }

    void addKnobWithLabel(math::Vec pos, NZZL* module, int paramId,
                          const std::string& text) {
        addParam(createParamCentered<componentlibrary::RoundBlackKnob>(
            pos, module, paramId));
        addLabel(math::Vec(pos.x, pos.y + 13.f), text);
    }

    void addInputWithLabel(math::Vec pos, NZZL* module, int portId,
                           const std::string& text) {
        addInput(createInputCentered<componentlibrary::PJ301MPort>(
            pos, module, portId));
        addLabel(math::Vec(pos.x, pos.y + 12.f), text, 8.f);
    }

    void addOutputWithLabel(math::Vec pos, NZZL* module, int portId,
                            const std::string& text) {
        addOutput(createOutputCentered<componentlibrary::PJ301MPort>(
            pos, module, portId));
        addLabel(math::Vec(pos.x, pos.y + 12.f), text, 8.f);
    }

    void addDisplayLine(math::Vec pos, NZZL* module, int line, int lightId) {
        NZZLDisplay* d = new NZZLDisplay;
        d->box.pos = pos;
        d->box.size = math::Vec(70.f, 15.f);
        d->module = module;
        d->line = line;
#ifdef NZZL_METAMODULE
        d->font = "Default_12";
        d->color = Colors565::Green;
        d->firstLightId = lightId;
#else
        (void)lightId;
#endif
        addChild(d);
    }

    NZZLWidget(NZZL* module) {
        setModule(module);
        box.size = math::Vec(app::RACK_GRID_WIDTH * 20, app::RACK_GRID_HEIGHT);

        addLabel(math::Vec(box.size.x * 0.5f, 6.f), "N Z Z L", 16.f);

        // ── Display ──────────────────────────────────────────────────────────
        addDisplayLine(math::Vec(12.f, 26.f), module, 0, NZZL::STEPS_DISPLAY);
        addDisplayLine(math::Vec(92.f, 26.f), module, 1, NZZL::STYLE_DISPLAY);
        addDisplayLine(math::Vec(12.f, 42.f), module, 2, NZZL::SCALE_DISPLAY);

        // ── Controls (left two columns) ──────────────────────────────────────
        const float cx1 = 30.f, cx2 = 85.f;
        float y = 72.f;
        const float dy = 38.f;

        addKnobWithLabel(math::Vec(cx1, y), module, NZZL::STYLE_PARAM,    "STYLE");
        // (cx2, y) is held for PITCH LENGTH, item 7.
        y += dy;
        addKnobWithLabel(math::Vec(cx1, y), module, NZZL::DENSITY_PARAM,  "DENSITY");
        addKnobWithLabel(math::Vec(cx2, y), module, NZZL::LENGTH_PARAM,   "LENGTH");
        y += dy;
        addKnobWithLabel(math::Vec(cx1, y), module, NZZL::CLOCK_DIV_PARAM,   "CLK DIV");
        addKnobWithLabel(math::Vec(cx2, y), module, NZZL::OCTAVE_RANGE_PARAM,"OCT RNG");
        y += dy;
        addKnobWithLabel(math::Vec(cx1, y), module, NZZL::ROOT_PARAM,  "ROOT");
        addKnobWithLabel(math::Vec(cx2, y), module, NZZL::SCALE_PARAM, "SCALE");
        y += dy;
        addKnobWithLabel(math::Vec(cx1, y), module, NZZL::SLIDE_PARAM,  "SLIDE");
        addKnobWithLabel(math::Vec(cx2, y), module, NZZL::SLIDE_SLOPE_PARAM, "SHAPE");
        y += dy;
        addKnobWithLabel(math::Vec(cx1, y), module, NZZL::GATE_PARAM,   "GATE");
        addKnobWithLabel(math::Vec(cx2, y), module, NZZL::ACCENT_PARAM, "ACCENT");
        y += dy;
        addKnobWithLabel(math::Vec(cx1, y), module, NZZL::PITCH_OFFSET_PARAM, "P.OFFSET");
        addKnobWithLabel(math::Vec(cx2, y), module, NZZL::SHIFT_PARAM,  "SHIFT");
        y += dy;
        addKnobWithLabel(math::Vec(cx1, y), module, NZZL::SWING_PARAM,  "SWING");
        addParam(createParamCentered<componentlibrary::VCVButton>(
            math::Vec(cx2, y), module, NZZL::RANDOM_PARAM));
        addLabel(math::Vec(cx2, y + 13.f), "RANDOM");

        // ── Jacks ────────────────────────────────────────────────────────────
        const float jx1 = 195.f, jx2 = 250.f;
        float jy = 80.f;
        const float jdy = 42.f;

        addInputWithLabel(math::Vec(jx1, jy), module, NZZL::CLOCK_INPUT,  "CLOCK");
        addInputWithLabel(math::Vec(jx2, jy), module, NZZL::RUN_INPUT,    "RUN");
        jy += jdy;
        addInputWithLabel(math::Vec(jx1, jy), module, NZZL::RESEED_INPUT,   "RESEED");
        addInputWithLabel(math::Vec(jx2, jy), module, NZZL::CV_DENSITY_INPUT, "cvDENS");
        jy += jdy;
        addInputWithLabel(math::Vec(jx1, jy), module, NZZL::CV_SCALE_INPUT, "cvSCALE");
        addInputWithLabel(math::Vec(jx2, jy), module, NZZL::CV_ROOT_INPUT,  "cvROOT");
        jy += jdy;
        addInputWithLabel(math::Vec(jx1, jy), module, NZZL::CV_SLIDE_INPUT, "cvSLIDE");

        jy += jdy + 8.f;
        addLabel(math::Vec((jx1 + jx2) * 0.5f, jy - 16.f), "OUT", 10.f);
        addOutputWithLabel(math::Vec(jx1, jy), module, NZZL::CV_PITCH_OUTPUT, "PITCH");
        addOutputWithLabel(math::Vec(jx2, jy), module, NZZL::GATE_OUTPUT,     "GATE");
        jy += jdy;
        addOutputWithLabel(math::Vec(jx1, jy), module, NZZL::VELOCITY_OUTPUT, "VEL");
    }
};

Model* modelNZZL = createModel<NZZL, NZZLWidget>("NZZL");
