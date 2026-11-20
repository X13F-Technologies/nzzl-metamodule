#pragma once
#include "pattern.hh"
#include "scales.hh"
#include "cv.hh"
#include <cmath>

// The realtime sequencer state machine, extracted from the Rack adapter so the
// native harness can drive it with fake time and fake voltages.
//
// docs/DESIGN.md § Testing strategy always planned this extraction "at Task 7
// (slide) when timing logic gets harder to eyeball" — this is that moment.
// nzzl.cc now only maps params/jacks onto Engine; every decision lives here.
//
// Determinism: Engine NEVER generates randomness. A RESEED trigger only sets
// `generateRequested`; the adapter is what rolls a new pattern. That keeps the one
// non-deterministic moment in the module outside the pure layer.

namespace nzzl {

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

// A tied step holds its gate slightly past the step boundary, so the next
// step re-arms the timer before it can fall. That is what makes a 303 slide
// legato rather than two separate notes with a glide between them.
constexpr float TIE_OVERHANG = 1.02f;

// The longest a swung step may be pushed back, as a fraction of one step.
// 1/3 is a hard shuffle; past that it stops reading as groove.
constexpr float SWING_MAX_FRACTION = 0.33f;

struct EngineParams {
    int   density  = 8;
    int   length   = MAX_STEPS;
    int   clockDiv = 1;
    float slideKnob = 0.f;     // SLIDE attenuator, 0..1
    float slideCv   = 0.f;     // CV SLIDE jack, 0..1 (summed per step first)
    float slideSlope = 0.5f;   // 0 = logarithmic, 0.5 = linear, 1 = exponential
    float gate     = 1.f;      // GATE knob as a factor: 1..200 -> 0.01..2.0
    float accent   = 1.f;      // ACCENT knob 0..100 -> 0..1
    int   shift    = 0;        // SHIFT knob: rotate the pattern by whole steps
    float swing    = 0.f;      // SWING knob, 0..1, scales the roll's own swing
    // PITCH LENGTH: the pitch sequence loops on its own length, independent
    // of LENGTH, so melody drifts against rhythm. 0 = follow LENGTH, which is
    // exactly the original behaviour. The pitch counter advances on EVERY
    // step, sounding or silent, so it stays locked to the clock grid.
    int   pitchLength = 0;
    // Opt-in for CPUs without an FPU (o_c on Teensy 3.2): the slide curve
    // uses fastPow (no libm call per sample) instead of std::pow. Off by
    // default, so VCV/MetaModule output is unchanged.
    bool  fastSlide = false;
    QuantizeParams quant{};
};

// SHIFT rotates the whole sixteen-step pattern BEFORE the LENGTH window, so
// at a short length shifting brings different parts of the pattern into the
// loop rather than just re-phasing what is already playing.
inline int rotatedIndex(int step, int shift) {
    return ((step + shift) % MAX_STEPS + MAX_STEPS) % MAX_STEPS;
}

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
    bool  started       = false;  // has any step fired yet?
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
    int   heldOctaveJump = -1;
    int   heldAcidRole   = -1;    // must travel with the index, see pitchOut
    int   prevPitchIndex = 0;     // where a slide starts from
    float prevOctaveRaw  = 0.f;
    int   prevOctaveJump = -1;
    int   prevAcidRole   = -1;
    int   pitchStep      = 0;     // position in the PITCH LENGTH loop
    float slideProgress  = 1.f;   // 0 = at prev, 1 = arrived at held
    float slideDuration  = 0.f;   // seconds

    Trigger clockTrig, runTrig, reseedTrig;

    // Set on a RESEED rising edge (Generate); the adapter rolls and clears it.
    bool generateRequested = false;

    // A step waiting out its swing delay before it fires.
    bool  stepPending   = false;
    float pendingTimer  = 0.f;

    // True on the sample the loop wraps back to step 0 (step 0 has just
    // fired). Informational; too late to swap patterns on.
    bool loopWrapped = false;

    // True while the NEXT step to fire is step 0: before the first pulse, and
    // for the whole of the loop's last step. The adapter swaps a queued roll
    // or STYLE change while this is set, so the new pattern's own step 1 is
    // the first thing heard. Swapping then is safe because the engine reads
    // the pattern only at the moment a step fires; the sounding last step is
    // already latched. (Swapping on `loopWrapped` instead was one step late:
    // step 1 of the first loop still played the old pattern.)
    bool atLoopEnd = true;

    void reset() {
        *this = Engine{};
    }

    EngineOutputs process(float dt, const EngineInputs& in,
                          const Pattern& pat, const EngineParams& p) {
        const int length   = clampi(p.length, 2, MAX_STEPS);
        const int clockDiv = p.clockDiv < 1 ? 1 : p.clockDiv;

        if (in.reseedConnected && reseedTrig.process(in.reseed))
            generateRequested = true;

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
                // Fire on the FIRST pulse, then every clockDiv-th. Counting
                // up to clockDiv before the first fire put a divided pattern
                // clockDiv-1 pulses behind the bar (bug K1).
                if (clockDivCount == 0) {
                    const int next = started ? (step + 1) % length : 0;
                    const float stepDur = clockPeriod * float(clockDiv);
                    const float sw = clampf(p.swing, 0.f, 1.f) * pat.swing;
                    // Off-beat sixteenths get pushed late; downbeats never do.
                    const float delay = (next % 2 == 1)
                                      ? sw * SWING_MAX_FRACTION * stepDur : 0.f;
                    if (delay > 0.f) {
                        stepPending  = true;
                        pendingTimer = delay;
                    } else {
                        fireStep(pat, p, clockDiv, length);
                    }
                }
                clockDivCount = (clockDivCount + 1) % clockDiv;
            }
        }

        if (stepPending) {
            pendingTimer -= dt;
            if (pendingTimer <= 0.f) {
                stepPending = false;
                fireStep(pat, p, clockDiv, length);
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
        loopWrapped  = wrappedThisSample;
        wrappedThisSample = false;
        // A swung step waiting to fire is never step 0 (downbeats are not
        // delayed), so `step` is the right place to look even then.
        atLoopEnd    = !started || (step + 1) % length == 0;
        out.velocity = heldVelocity * 10.f;
        out.pitch    = pitchOut(p);
        return out;
    }

    // Voltage for the note currently sounding, mid-slide or not.
    // Interpolating between two STEPS (not two stored voltages) means a knob
    // turn moves both ends of the glide, and the arrival value is always
    // exactly the quantized target.
    float pitchOut(const EngineParams& p) const {
        const float target = pitchVoltage(heldStep(), p.quant);
        if (slideProgress >= 1.f)
            return target;

        const float from = pitchVoltage(prevStep(), p.quant);
        const float shaped = p.fastSlide
                           ? slideCurveFast(slideProgress, p.slideSlope)
                           : slideCurve(slideProgress, p.slideSlope);
        return from + (target - from) * shaped;
    }

    // The note now sounding, rebuilt as a StepData. acidRole MUST be copied:
    // a value-initialised StepData has acidRole 0 ("the root"), which made
    // every quantized note collapse onto the root (bug K7).
    StepData heldStep() const {
        StepData s{};
        s.pitchIndex = heldPitchIndex;
        s.octaveRaw  = heldOctaveRaw;
        s.octaveJump = heldOctaveJump;
        s.acidRole   = heldAcidRole;
        return s;
    }
    StepData prevStep() const {
        StepData s{};
        s.pitchIndex = prevPitchIndex;
        s.octaveRaw  = prevOctaveRaw;
        s.octaveJump = prevOctaveJump;
        s.acidRole   = prevAcidRole;
        return s;
    }


    // Shape the glide without breaking its guarantees. Any exponent maps
    // t = 1 to exactly 1, so the note still lands precisely on the quantized
    // target within the slide time, and the curve stays monotonic so it can
    // never overshoot.
    static float slideCurve(float t, float slope) {
        const float s = clampf(slope, 0.f, 1.f);
        if (t <= 0.f) return 0.f;
        if (t >= 1.f) return 1.f;
        // slope 0 -> 0.35 (fast attack, slow settle), 0.5 -> 1 (linear),
        // 1 -> 3 (slow start, rushing arrival)
        const float k = (s < 0.5f) ? 0.35f + (s / 0.5f) * 0.65f
                                   : 1.f + ((s - 0.5f) / 0.5f) * 2.f;
        return std::pow(t, k);
    }

    // Same curve via fastPow. Ends are pinned, so arrival stays exact.
    static float slideCurveFast(float t, float slope) {
        const float s = clampf(slope, 0.f, 1.f);
        if (t <= 0.f) return 0.f;
        if (t >= 1.f) return 1.f;
        const float k = (s < 0.5f) ? 0.35f + (s / 0.5f) * 0.65f
                                   : 1.f + ((s - 0.5f) / 0.5f) * 2.f;
        const float v = fastPow(t, k);
        return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    }

    // t^k for t in (0, 1], k > 0, without libm: log2 from the float's exponent
    // plus a polynomial on the mantissa, exp2 likewise. Relative error ~1e-4,
    // i.e. well under a cent on a several-octave glide.
    static float fastPow(float t, float k) {
        return fastExp2(k * fastLog2(t));
    }
    static float fastLog2(float x) {
        union { float f; uint32_t i; } u{x};
        const int e = int((u.i >> 23) & 0xff) - 127;
        u.i = (u.i & 0x007fffffu) | 0x3f800000u;          // mantissa in [1, 2)
        const float m = u.f;
        // least-squares fit of log2(m) on [1, 2), 5th order, max error 3.2e-5
        const float p = -2.7868458f + m * (5.0469786f + m * (-3.4926196f
                      + m * (1.5939760f + m * (-0.40488876f + m * 0.043431324f))));
        return float(e) + p;
    }
    static float fastExp2(float x) {
        if (x < -126.f) return 0.f;
        const float fl = float(int(x) - (x < 0.f && float(int(x)) != x ? 1 : 0));
        const float f  = x - fl;                            // [0, 1)
        // least-squares fit of 2^f on [0, 1), 5th order, max error 2.5e-7
        const float p = 0.99999977f + f * (0.69315677f + f * (0.24013173f
                      + f * (0.055876505f + f * (0.0089406036f + f * 0.0018943829f))));
        union { float f; uint32_t i; } u{p};
        u.i += uint32_t(int32_t(fl)) << 23;
        return u.f;
    }

private:
    bool wrappedThisSample = false;

    void fireStep(const Pattern& pat, const EngineParams& p,
                  int clockDiv, int length) {
        const bool first = !started;
        if (first) {
            started = true;
            step = 0;                 // pulse 1 plays step index 0 (bug K1)
        } else {
            step = (step + 1) % length;
        }
        if (p.pitchLength <= 0) {
            pitchStep = step;         // follow LENGTH: identical to before
        } else {
            const int pl = clampi(p.pitchLength, 1, MAX_STEPS);
            pitchStep = first ? 0 : (pitchStep + 1) % pl;
        }
        if (step == 0) wrappedThisSample = true;
        advanceStep(pat, p, clockDiv);
    }

    void advanceStep(const Pattern& pat, const EngineParams& p, int clockDiv) {
        const StepData& st = pat.steps[rotatedIndex(step, p.shift)];
        // Density is ranked among the steps INSIDE the loop, not across all
        // sixteen — otherwise at short LENGTH half the knob switches on steps
        // the loop never reaches (bug K2).
        if (!stepActive(pat, step, clampi(p.length, 2, MAX_STEPS),
                        p.density, p.shift))
            return;                            // silent step: hold everything

        const float stepDur  = clockPeriod * float(clockDiv);
        const float gateKnob = clampf(p.gate, 0.01f, 2.f);
        const int   gi       = clampi(st.gateIndex, 0, NUM_GATE_LENGTHS - 1);

        // One of the pattern's three note lengths, scaled by the GATE knob.
        // Past 100% a note runs into the next step and ties.
        float gateFrac = pat.gateLengths[gi] * gateKnob;

        // Step flag + CV, then the attenuator — see cv.hh for why the sum
        // happens here and not upstream.
        const float slideAmt = slideForStep(st.slide, p.slideCv, p.slideKnob);
        const bool  tied     = slideAmt > 0.f;
        if (tied && gateFrac < TIE_OVERHANG)
            gateFrac = TIE_OVERHANG;           // a slide is a tie, not a gap

        gateTimer    = gateFrac * stepDur;
        heldVelocity = velocityFor(st.velLayer, p.accent);

        // Rhythm (gate, velocity, slide) comes from `st`; pitch from the
        // PITCH LENGTH position, which is the same step when following.
        const StepData& ps = pat.steps[rotatedIndex(pitchStep, p.shift)];
        prevPitchIndex = heldPitchIndex;
        prevOctaveRaw  = heldOctaveRaw;
        prevOctaveJump = heldOctaveJump;
        prevAcidRole   = heldAcidRole;
        heldPitchIndex = ps.pitchIndex;
        heldOctaveRaw  = ps.octaveRaw;
        heldOctaveJump = ps.octaveJump;
        heldAcidRole   = ps.acidRole;

        if (tied) {
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
