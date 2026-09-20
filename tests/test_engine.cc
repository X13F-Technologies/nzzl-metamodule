// Native test harness for NZZL's realtime sequencer — no Rack/MetaModule deps.
// Drives Engine with synthetic time and synthetic clock voltages, so behaviour
// that used to be ear-only (clock division, gate length, run toggle, slide)
// is now machine-checked.

#include "../src/engine.hh"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static int failures = 0;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            failures++;                                     \
            printf("FAIL %s:%d  ", __FILE__, __LINE__);     \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

using namespace nzzl;

static const float SR = 48000.f;
static const float DT = 1.f / SR;
static const float EPS = 1e-4f;

// A square-wave clock generator: high for the first half of each period.
struct Clock {
    float period;
    float phase = 0.f;
    bool  enabled = true;
    explicit Clock(float p) : period(p) {}
    float tick(float dt) {
        if (!enabled) return 0.f;
        phase += dt;
        if (phase >= period) phase -= period;
        return phase < period * 0.5f ? 10.f : 0.f;
    }
};

// Run the engine for `seconds`, calling `each(out, t)` every sample.
template <typename F>
static void run(Engine& e, Clock& clk, const StepData* steps,
                const EngineParams& p, float seconds, F each,
                float runV = 0.f, bool runConnected = false) {
    int n = int(seconds * SR);
    for (int i = 0; i < n; i++) {
        EngineInputs in;
        in.clock = clk.tick(DT);
        in.run = runV;
        in.runConnected = runConnected;
        each(e.process(DT, in, steps, p), float(i) * DT);
    }
}

// ── clock, divide, length ────────────────────────────────────────────────────

static void test_clock_advances_steps() {
    StepData s[MAX_STEPS];
    generatePattern(0, s);
    EngineParams p;              // density 8, length 16, div 1

    Engine e;
    Clock clk(0.25f);            // 4 Hz
    int seen[MAX_STEPS] = {};
    int lastStep = e.step;
    int advances = 0;
    run(e, clk, s, p, 4.f, [&](EngineOutputs, float) {
        if (e.step != lastStep) { advances++; lastStep = e.step; }
        seen[e.step]++;
    });
    // 4 s at 4 Hz = 16 pulses; the first edge lands immediately, so 15–16.
    CHECK(advances >= 15 && advances <= 16,
          "expected ~16 step advances in 4 s at 4 Hz, got %d", advances);
    for (int i = 0; i < MAX_STEPS; i++)
        CHECK(seen[i] > 0, "step %d never visited in a full loop", i);
    printf("clock: advances one step per pulse, visits all 16\n");
}

static void test_clock_divide() {
    StepData s[MAX_STEPS];
    generatePattern(0, s);

    for (int div = 1; div <= 8; div++) {
        EngineParams p;
        p.clockDiv = div;
        Engine e;
        Clock clk(0.05f);        // 20 Hz
        int advances = 0;
        int lastStep = e.step;
        run(e, clk, s, p, 4.f, [&](EngineOutputs, float) {
            if (e.step != lastStep) { advances++; lastStep = e.step; }
        });
        int pulses = 80;                    // 4 s at 20 Hz
        int expected = pulses / div;
        CHECK(std::abs(advances - expected) <= 1,
              "clockDiv %d: %d advances, expected ~%d", div, advances, expected);
    }
    printf("clock divide: 1..8 all divide the step rate correctly\n");
}

static void test_length_wraps() {
    StepData s[MAX_STEPS];
    generatePattern(7, s);

    for (int len = 2; len <= MAX_STEPS; len++) {
        EngineParams p;
        p.length = len;
        Engine e;
        Clock clk(0.02f);
        int maxStep = 0;
        run(e, clk, s, p, 2.f, [&](EngineOutputs, float) {
            if (e.step > maxStep) maxStep = e.step;
            CHECK(e.step < len, "length %d: step reached %d", len, e.step);
        });
        CHECK(maxStep == len - 1,
              "length %d: highest step was %d, expected %d", len, maxStep, len - 1);
    }
    // out-of-range lengths must clamp, not index out of bounds
    EngineParams bad;
    bad.length = 99;
    Engine e2;
    Clock c2(0.02f);
    run(e2, c2, s, bad, 0.5f, [&](EngineOutputs, float) {
        CHECK(e2.step >= 0 && e2.step < MAX_STEPS, "clamped length still overran");
    });
    printf("length: every length 2..16 wraps correctly and clamps out of range\n");
}

// ── run / stop ───────────────────────────────────────────────────────────────

static void test_run_toggle() {
    StepData s[MAX_STEPS];
    generatePattern(3, s);
    EngineParams p;

    Engine e;
    Clock clk(0.05f);

    // free-run with RUN unpatched
    run(e, clk, s, p, 0.5f, [](EngineOutputs, float) {});
    int stepBefore = e.step;
    CHECK(stepBefore > 0, "did not advance with RUN unpatched");

    // one rising edge on RUN stops it
    {
        EngineInputs in;
        in.clock = 0.f; in.run = 10.f; in.runConnected = true;
        e.process(DT, in, s, p);
    }
    CHECK(!e.running, "rising edge on RUN did not stop playback");

    // held high: no further toggling, and no advancing
    int heldStep = e.step;
    for (int i = 0; i < int(0.5f * SR); i++) {
        EngineInputs in;
        in.clock = clk.tick(DT); in.run = 10.f; in.runConnected = true;
        e.process(DT, in, s, p);
    }
    CHECK(!e.running, "RUN held high toggled again — it must be edge-triggered");
    CHECK(e.step == heldStep, "stopped engine advanced from step %d to %d",
          heldStep, e.step);

    // low, then a second rising edge resumes FROM THE HELD STEP
    {
        EngineInputs in; in.run = 0.f; in.runConnected = true;
        e.process(DT, in, s, p);
        in.run = 10.f;
        e.process(DT, in, s, p);
    }
    CHECK(e.running, "second trigger did not resume playback");
    CHECK(e.step == heldStep, "resume reset the step position");

    // stop again, then UNPATCH: must free-run rather than stay stuck
    {
        EngineInputs in; in.run = 0.f; in.runConnected = true;
        e.process(DT, in, s, p);
        in.run = 10.f;
        e.process(DT, in, s, p);
    }
    CHECK(!e.running, "could not stop again");
    int stuckStep = e.step;
    run(e, clk, s, p, 0.5f, [](EngineOutputs, float) {});   // RUN unpatched
    CHECK(e.step != stuckStep,
          "unpatching RUN left the module stuck (stale stopped state)");
    printf("run: edge-toggles, holds position, and unpatched always runs\n");
}

// ── gate ─────────────────────────────────────────────────────────────────────

// Gate duration must equal gateLength x clock period x clockDiv.
static void test_gate_duration() {
    StepData s[MAX_STEPS];
    generatePattern(0, s);
    EngineParams p;
    p.density = MAX_STEPS;       // every step fires

    for (float period : {0.1f, 0.25f, 0.5f}) {
        for (int div : {1, 2, 4}) {
            p.clockDiv = div;
            Engine e;
            Clock clk(period);
            // let the period estimate settle, then measure one gate
            run(e, clk, s, p, period * float(div) * 3.f, [](EngineOutputs, float) {});

            // Wait for the gate to be LOW first, so we time a whole gate from
            // its rising edge rather than joining one already in progress.
            float high = 0.f;
            bool sawLow = false, measuring = false, done = false;
            int measuredStep = -1;
            run(e, clk, s, p, period * float(div) * 4.f,
                [&](EngineOutputs o, float) {
                    if (done) return;
                    bool g = o.gate > 5.f;
                    if (!sawLow) { if (!g) sawLow = true; return; }
                    if (g) {
                        if (!measuring) { measuring = true; measuredStep = e.step; }
                        high += DT;
                    } else if (measuring) {
                        done = true;
                    }
                });

            if (measuredStep >= 0 && done) {
                float expect = s[measuredStep].gateLength * period * float(div);
                CHECK(std::fabs(high - expect) < period * 0.05f,
                      "period %.2f div %d step %d: gate %.4f s, expected %.4f s",
                      period, div, measuredStep, high, expect);
            }
        }
    }
    printf("gate: duration = gateLength x period x clockDiv, all tempos\n");
}

// Halving the tempo must double gate length — same rhythm, stretched.
// Averaged over many gates, because gateLength varies 0.1–0.9 per step and a
// single gate says nothing.
static void test_gate_scales_with_tempo() {
    StepData s[MAX_STEPS];
    generatePattern(11, s);
    EngineParams p;
    p.density = MAX_STEPS;       // every step fires

    auto averageGate = [&](float period) {
        Engine e;
        Clock clk(period);
        run(e, clk, s, p, period * 4.f, [](EngineOutputs, float) {});   // settle
        float high = 0.f;
        int edges = 0;
        bool was = false;
        run(e, clk, s, p, period * 48.f, [&](EngineOutputs o, float) {
            bool g = o.gate > 5.f;
            if (g) high += DT;
            if (g && !was) edges++;
            was = g;
        });
        return edges > 0 ? high / float(edges) : 0.f;
    };

    float fast = averageGate(0.1f);
    float slow = averageGate(0.2f);
    CHECK(fast > 0.f, "no gates measured at the fast tempo");
    float ratio = slow / fast;
    CHECK(ratio > 1.8f && ratio < 2.2f,
          "halving the tempo scaled the gate by %.2fx, expected ~2x "
          "(%.4f s vs %.4f s)", ratio, fast, slow);
    printf("gate: halving the tempo scales gate length %.2fx (want ~2x)\n", ratio);
}

// Density gating: only steps whose weight <= density may fire.
static void test_density_gating() {
    for (int seed = 0; seed < 64; seed++) {
        StepData s[MAX_STEPS];
        generatePattern(seed, s);
        for (int density : {1, 4, 8, 16}) {
            EngineParams p;
            p.density = density;
            Engine e;
            Clock clk(0.02f);
            bool fired[MAX_STEPS] = {};
            bool wasHigh = false;
            run(e, clk, s, p, 1.f, [&](EngineOutputs o, float) {
                bool high = o.gate > 5.f;
                if (high && !wasHigh) fired[e.step] = true;
                wasHigh = high;
            });
            int count = 0;
            for (int i = 0; i < MAX_STEPS; i++) {
                if (fired[i]) {
                    count++;
                    CHECK(s[i].weight <= density,
                          "seed %d density %d: step %d fired with weight %d",
                          seed, density, i, s[i].weight);
                }
            }
            CHECK(count == density,
                  "seed %d density %d: %d steps fired", seed, density, count);
        }
    }
    printf("density: exactly N steps fire a gate at density N, 64 seeds\n");
}

// ── velocity and pitch sample-and-hold ───────────────────────────────────────

static void test_sample_and_hold() {
    StepData s[MAX_STEPS];
    generatePattern(0, s);
    EngineParams p;
    p.density = 4;               // most steps silent, so holding is visible

    Engine e;
    Clock clk(0.02f);
    run(e, clk, s, p, 0.5f, [](EngineOutputs, float) {});   // settle

    float lastVel = -1.f, lastPitch = -1e9f;
    bool wasHigh = false;
    int velChangesWhileSilent = 0, pitchChangesWhileSilent = 0;
    run(e, clk, s, p, 2.f, [&](EngineOutputs o, float) {
        bool high = o.gate > 5.f;
        bool onset = high && !wasHigh;
        if (!onset && lastVel >= 0.f) {
            if (std::fabs(o.velocity - lastVel) > EPS)   velChangesWhileSilent++;
            if (std::fabs(o.pitch - lastPitch) > EPS)    pitchChangesWhileSilent++;
        }
        lastVel = o.velocity;
        lastPitch = o.pitch;
        wasHigh = high;
    });
    CHECK(velChangesWhileSilent == 0,
          "velocity moved %d times outside a gate onset", velChangesWhileSilent);
    CHECK(pitchChangesWhileSilent == 0,
          "pitch moved %d times outside a gate onset (slide is off here)",
          pitchChangesWhileSilent);
    printf("sample-and-hold: velocity and pitch only move at gate onsets\n");
}

// Velocity output must stay in 0–10 V.
static void test_velocity_range() {
    StepData s[MAX_STEPS];
    generatePattern(5, s);
    EngineParams p;
    Engine e;
    Clock clk(0.02f);
    run(e, clk, s, p, 2.f, [&](EngineOutputs o, float) {
        CHECK(o.velocity >= 0.f && o.velocity <= 10.f,
              "velocity %f out of 0–10 V", o.velocity);
        CHECK(o.gate == 0.f || o.gate == 10.f, "gate %f is not 0 or 10 V", o.gate);
    });
    printf("ranges: gate is 0/10 V, velocity stays in 0–10 V\n");
}

// ── slide (Task 7) ───────────────────────────────────────────────────────────

// SLIDE at 0 must be indistinguishable from the pre-slide module.
static void test_slide_zero_is_instant() {
    StepData s[MAX_STEPS];
    generatePattern(0, s);
    EngineParams p;
    p.slide = 0.f;
    p.density = MAX_STEPS;

    Engine e;
    Clock clk(0.05f);
    int intermediate = 0;
    run(e, clk, s, p, 2.f, [&](EngineOutputs o, float) {
        // With no slide, every emitted pitch must be an exact quantized value.
        float semiF = o.pitch * 12.f;
        if (std::fabs(semiF - std::lround(semiF)) > EPS) intermediate++;
    });
    CHECK(intermediate == 0,
          "slide=0 produced %d off-grid pitches — it is not instantaneous",
          intermediate);
    printf("slide: 0 is instantaneous, pitch is always exactly quantized\n");
}

// A slide must reach its target exactly, without overshooting, within the
// slide time — and always before the next step.
static void test_slide_reaches_target() {
    EngineParams p;
    p.density = MAX_STEPS;
    p.slide = 1.f;               // longest glide

    int slidesChecked = 0;
    for (int seed = 0; seed < 32; seed++) {
        StepData s[MAX_STEPS];
        generatePattern(seed, s);

        Engine e;
        Clock clk(0.2f);
        run(e, clk, s, p, 0.6f, [](EngineOutputs, float) {});   // settle period

        float prevPitch = e.pitchOut(p);
        bool sliding = false;
        float from = 0.f, target = 0.f, elapsed = 0.f;

        run(e, clk, s, p, 4.f, [&](EngineOutputs o, float) {
            if (e.slideProgress < 1.f && !sliding) {
                sliding = true;
                elapsed = 0.f;
                from = prevPitch;
                StepData cur{};
                cur.pitchIndex = e.heldPitchIndex;
                cur.octaveRaw  = e.heldOctaveRaw;
                target = pitchVoltage(cur, p.quant);
            }
            if (sliding) {
                elapsed += DT;
                // never outside the two endpoints — no overshoot
                float lo = from < target ? from : target;
                float hi = from < target ? target : from;
                CHECK(o.pitch >= lo - EPS && o.pitch <= hi + EPS,
                      "seed %d: slide overshot (%.4f not in [%.4f, %.4f])",
                      seed, o.pitch, lo, hi);
                if (e.slideProgress >= 1.f) {
                    CHECK(std::fabs(o.pitch - target) < EPS,
                          "seed %d: slide ended at %.4f, target %.4f",
                          seed, o.pitch, target);
                    CHECK(elapsed <= 0.2f * SLIDE_MAX_FRACTION + 0.01f,
                          "seed %d: slide took %.4f s, longer than the step",
                          seed, elapsed);
                    slidesChecked++;
                    sliding = false;
                }
            }
            prevPitch = o.pitch;
        });
    }
    CHECK(slidesChecked > 20,
          "only %d slides observed across 32 seeds — is slide firing at all?",
          slidesChecked);
    printf("slide: %d glides all reached target exactly, no overshoot, "
           "within one step\n", slidesChecked);
}

// Longer SLIDE settings must produce longer glides.
static void test_slide_knob_scales() {
    StepData s[MAX_STEPS];
    generatePattern(0, s);

    auto measure = [&](float amount) {
        EngineParams p;
        p.density = MAX_STEPS;
        p.slide = amount;
        Engine e;
        Clock clk(0.2f);
        run(e, clk, s, p, 0.6f, [](EngineOutputs, float) {});
        float total = 0.f;
        run(e, clk, s, p, 3.f, [&](EngineOutputs, float) {
            if (e.slideProgress < 1.f) total += DT;
        });
        return total;
    };

    float quarter = measure(0.25f);
    float half    = measure(0.5f);
    float full    = measure(1.f);
    CHECK(quarter < half && half < full,
          "slide time does not grow with the knob (%.3f, %.3f, %.3f)",
          quarter, half, full);
    CHECK(measure(0.f) == 0.f, "slide=0 still spent time gliding");
    printf("slide: glide time grows with the knob (%.2f < %.2f < %.2f s)\n",
           quarter, half, full);
}

// A slide is tempo-relative: the same knob setting glides longer at a slower
// clock, so the feel survives a tempo change.
static void test_slide_follows_tempo() {
    StepData s[MAX_STEPS];
    generatePattern(0, s);

    auto measure = [&](float period) {
        EngineParams p;
        p.density = MAX_STEPS;
        p.slide = 1.f;
        Engine e;
        Clock clk(period);
        run(e, clk, s, p, period * 4.f, [](EngineOutputs, float) {});
        float total = 0.f;
        int glides = 0;
        bool was = false;
        run(e, clk, s, p, period * 16.f, [&](EngineOutputs, float) {
            bool now = e.slideProgress < 1.f;
            if (now) total += DT;
            if (now && !was) glides++;
            was = now;
        });
        return glides > 0 ? total / float(glides) : 0.f;
    };

    float fast = measure(0.1f);
    float slow = measure(0.4f);
    CHECK(fast > 0.f && slow > fast * 2.f,
          "slide is not tempo-relative (%.4f s fast vs %.4f s slow)", fast, slow);
    printf("slide: glide length tracks the clock (%.3f s vs %.3f s)\n", fast, slow);
}

// ── reseed request (Task 9, engine half) ─────────────────────────────────────

static void test_reseed_edge() {
    StepData s[MAX_STEPS];
    generatePattern(0, s);
    EngineParams p;
    Engine e;

    EngineInputs in;
    in.reseedConnected = true;

    in.reseed = 0.f;
    e.process(DT, in, s, p);
    CHECK(!e.reseedRequested, "reseed fired with no trigger");

    in.reseed = 10.f;
    e.process(DT, in, s, p);
    CHECK(e.reseedRequested, "rising edge did not request a reseed");

    e.reseedRequested = false;
    for (int i = 0; i < 100; i++) e.process(DT, in, s, p);   // held high
    CHECK(!e.reseedRequested, "held-high reseed retriggered — must be edge-only");

    in.reseed = 0.f;
    e.process(DT, in, s, p);
    in.reseed = 10.f;
    e.process(DT, in, s, p);
    CHECK(e.reseedRequested, "second rising edge did not request a reseed");

    // unpatched must never request
    e.reseedRequested = false;
    Engine e2;
    EngineInputs un;
    un.reseed = 10.f;
    un.reseedConnected = false;
    for (int i = 0; i < 100; i++) e2.process(DT, un, s, p);
    CHECK(!e2.reseedRequested, "unpatched RESEED requested a reseed");
    printf("reseed: edge-triggered only, ignored when unpatched\n");
}

// The engine itself must never invent randomness: same inputs, same outputs.
static void test_engine_determinism() {
    StepData s[MAX_STEPS];
    generatePattern(21, s);
    EngineParams p;
    p.slide = 0.6f;

    auto capture = [&](float* buf, int n) {
        Engine e;
        Clock clk(0.05f);
        int i = 0;
        run(e, clk, s, p, float(n) * DT, [&](EngineOutputs o, float) {
            if (i < n) buf[i++] = o.pitch * 1000.f + o.gate + o.velocity;
        });
    };
    const int N = 20000;
    static float a[N], b[N];
    capture(a, N);
    capture(b, N);
    for (int i = 0; i < N; i++)
        CHECK(a[i] == b[i], "engine not deterministic at sample %d", i);
    printf("engine determinism: identical output for identical input\n");
}

int main() {
    test_clock_advances_steps();
    test_clock_divide();
    test_length_wraps();
    test_run_toggle();
    test_gate_duration();
    test_gate_scales_with_tempo();
    test_density_gating();
    test_sample_and_hold();
    test_velocity_range();
    test_slide_zero_is_instant();
    test_slide_reaches_target();
    test_slide_knob_scales();
    test_slide_follows_tempo();
    test_reseed_edge();
    test_engine_determinism();

    if (failures) {
        printf("\n%d FAILURES\n", failures);
        return 1;
    }
    printf("\nall tests passed\n");
    return 0;
}
