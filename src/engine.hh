#pragma once
#include "pattern.hh"
#include "scales.hh"

// The realtime sequencer state machine, extracted from the Rack adapter so the
// native harness can drive it with fake time and fake voltages.
//
// docs/DESIGN.md § Testing strategy always planned this extraction "at Task 7
// (slide) when timing logic gets harder to eyeball" — this is that moment.
// nzzl.cc now only maps params/jacks onto Engine; every decision lives here.
//
// Determinism: Engine NEVER generates randomness. A reseed trigger only sets
// `reseedRequested`; the adapter is what picks a new seed. That keeps the one
// non-deterministic moment in the module outside the pure layer.

namespace nzzl {

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Schmitt trigger with the module's long-standing 0.1 V / 2.0 V thresholds.
// Pure, so trigger behaviour is testable from raw voltages.
struct Trigger {
    bool high = false;

    bool process(float v, float lo = 0.1f, float hi = 2.f) {
        if (high) {
            if (v <= lo) high = false;
        } else if (v >= hi) {
            high = true;
            return true;      // rising edge
        }
        return false;
    }
    void reset() { high = false; }
};

constexpr float CLOCK_PERIOD_MIN = 0.01f;   // 10 ms
constexpr float CLOCK_PERIOD_MAX = 4.f;     // 4 s
// A slide never occupies the whole step, so the note is always sitting at its
// exact quantized pitch before the next one starts.
constexpr float SLIDE_MAX_FRACTION = 0.9f;

struct EngineParams {
    int   density  = 8;
    int   length   = MAX_STEPS;
    int   clockDiv = 1;
    float slide    = 0.f;      // 0..1
    QuantizeParams quant{};
};

struct EngineInputs {
    float clock  = 0.f;
    float run    = 0.f;
    float reseed = 0.f;
    bool  runConnected    = false;
    bool  reseedConnected = false;
};

struct EngineOutputs {
    float pitch    = 0.f;      // volts, 1V/oct
    float gate     = 0.f;      // 0 or 10 V
    float velocity = 0.f;      // 0–10 V
};

struct Engine {
    int   step          = 0;
    int   clockDivCount = 0;
    bool  running       = true;
    float clockPeriod   = 0.5f;   // estimated from clock edges, seconds
    float clockPhase    = 0.f;    // since last accepted edge, seconds
    float gateTimer     = 0.f;    // remaining gate-high time, seconds
    float heldVelocity  = 0.f;    // latched at gate onset

    // Pitch is stored as the STEP that is sounding, not as a voltage, so the
    // quantizer can re-run every sample and knob turns re-pitch the held note.
    int   heldPitchIndex = 0;
    float heldOctaveRaw  = 0.f;
    int   prevPitchIndex = 0;     // where a slide starts from
    float prevOctaveRaw  = 0.f;
    float slideProgress  = 1.f;   // 0 = at prev, 1 = arrived at held
    float slideDuration  = 0.f;   // seconds

    Trigger clockTrig, runTrig, reseedTrig;

    // Set on a reseed rising edge; the adapter acts on it and clears it.
    bool reseedRequested = false;

    void reset() {
        *this = Engine{};
    }

    EngineOutputs process(float dt, const EngineInputs& in,
                          const StepData* steps, const EngineParams& p) {
        const int length   = clampi(p.length, 2, MAX_STEPS);
        const int clockDiv = p.clockDiv < 1 ? 1 : p.clockDiv;

        if (in.reseedConnected && reseedTrig.process(in.reseed))
            reseedRequested = true;

        if (in.runConnected) {
            if (runTrig.process(in.run))
                running = !running;
        }
        // Unpatched RUN means always running — a stale `false` from a cable
        // that has since been removed must not leave the module stuck.
        const bool isRunning = in.runConnected ? running : true;

        clockPhase += dt;
        if (clockTrig.process(in.clock)) {
            if (clockPhase > CLOCK_PERIOD_MIN && clockPhase < CLOCK_PERIOD_MAX)
                clockPeriod = clockPhase;
            clockPhase = 0.f;

            if (isRunning) {
                clockDivCount++;
                if (clockDivCount >= clockDiv) {
                    clockDivCount = 0;
                    step = (step + 1) % length;
                    advanceStep(steps, p, clockDiv);
                }
            }
        }

        if (slideProgress < 1.f) {
            if (slideDuration > 0.f) {
                slideProgress += dt / slideDuration;
                if (slideProgress > 1.f) slideProgress = 1.f;
            } else {
                slideProgress = 1.f;
            }
        }

        // Clamp at zero rather than running negative forever — this counter is
        // decremented every sample for as long as the module is loaded.
        if (gateTimer > 0.f) gateTimer -= dt;

        EngineOutputs out;
        out.gate     = gateTimer > 0.f ? 10.f : 0.f;
        out.velocity = heldVelocity * 10.f;
        out.pitch    = pitchOut(p);
        return out;
    }

    // Voltage for the note currently sounding, mid-slide or not.
    // Interpolating between two STEPS (not two stored voltages) means a knob
    // turn moves both ends of the glide, and the arrival value is always
    // exactly the quantized target.
    float pitchOut(const EngineParams& p) const {
        StepData cur{};
        cur.pitchIndex = heldPitchIndex;
        cur.octaveRaw  = heldOctaveRaw;
        const float target = pitchVoltage(cur, p.quant);
        if (slideProgress >= 1.f)
            return target;

        StepData prev{};
        prev.pitchIndex = prevPitchIndex;
        prev.octaveRaw  = prevOctaveRaw;
        const float from = pitchVoltage(prev, p.quant);
        return from + (target - from) * slideProgress;
    }

private:
    void advanceStep(const StepData* steps, const EngineParams& p, int clockDiv) {
        if (steps[step].weight > p.density)
            return;                            // silent step: hold everything

        const float stepDur = clockPeriod * float(clockDiv);
        gateTimer    = steps[step].gateLength * stepDur;
        heldVelocity = steps[step].velocity;

        prevPitchIndex = heldPitchIndex;
        prevOctaveRaw  = heldOctaveRaw;
        heldPitchIndex = steps[step].pitchIndex;
        heldOctaveRaw  = steps[step].octaveRaw;

        const float slideAmt = clampf(p.slide, 0.f, 1.f);
        if (steps[step].slide && slideAmt > 0.f) {
            // Linear ramp over a fraction of the step. Linear, not exponential:
            // it arrives exactly (so the note ends up precisely in tune), it
            // cannot overshoot, and "reaches the target within the slide time"
            // is a property the harness can actually assert.
            slideDuration = slideAmt * SLIDE_MAX_FRACTION * stepDur;
            slideProgress = 0.f;
        } else {
            slideDuration = 0.f;
            slideProgress = 1.f;               // instantaneous
        }
    }
};

} // namespace nzzl
