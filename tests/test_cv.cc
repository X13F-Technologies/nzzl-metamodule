// Native test harness for the seed<->knob round trip (Task 9) and the CV
// input mappings (Task 10). No Rack deps.

#include "../src/cv.hh"
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

// ── Task 9: seed <-> knob round trip ────────────────────────────────────────
//
// RESEED picks a seed and must drive the knobs to match it. If this mapping
// is not an exact bijection, a reseeded pattern stops being reproducible by
// hand — which is the module's core promise.
static void test_seed_knob_roundtrip() {
    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        int g = 0, sg = 0;
        seedKnobsFor(seed, g, sg);
        CHECK(g >= 1 && g <= NUM_GROUPS, "seed %d gave group %d", seed, g);
        CHECK(sg >= 1 && sg <= SEEDS_PER_GROUP, "seed %d gave subgroup %d", seed, sg);
        CHECK(seedIndexFor(g, sg) == seed,
              "seed %d -> (%d, %d) -> %d: round trip broken",
              seed, g, sg, seedIndexFor(g, sg));
    }
    // and the other direction: every knob pair maps to a distinct seed
    bool seen[NUM_SEEDS] = {};
    for (int g = 1; g <= NUM_GROUPS; g++) {
        for (int sg = 1; sg <= SEEDS_PER_GROUP; sg++) {
            int idx = seedIndexFor(g, sg);
            CHECK(idx >= 0 && idx < NUM_SEEDS, "(%d,%d) gave seed %d", g, sg, idx);
            CHECK(!seen[idx], "(%d,%d) collides on seed %d", g, sg, idx);
            seen[idx] = true;
        }
    }
    for (int i = 0; i < NUM_SEEDS; i++)
        CHECK(seen[i], "seed %d is unreachable from the knobs", i);

    // out-of-range inputs must clamp rather than compute a bogus seed
    CHECK(seedIndexFor(0, 0) == 0, "low knobs did not clamp");
    CHECK(seedIndexFor(99, 99) == NUM_SEEDS - 1, "high knobs did not clamp");
    int g2 = 0, sg2 = 0;
    seedKnobsFor(-5, g2, sg2);
    CHECK(g2 == 1 && sg2 == 1, "negative seed did not clamp");
    seedKnobsFor(99999, g2, sg2);
    CHECK(g2 == NUM_GROUPS && sg2 == SEEDS_PER_GROUP, "huge seed did not clamp");
    printf("seed round trip: all %d seeds <-> knobs bijective, clamps safely\n",
           NUM_SEEDS);
}

// ── Unpatched inputs must change nothing ────────────────────────────────────
static void test_unpatched_is_neutral() {
    for (float v = -12.f; v <= 12.f; v += 0.37f) {
        CHECK(applyScaleCv(5, v, false) == 5, "unpatched CV SCALE moved the scale");
        CHECK(applyRootCv(7, v, false) == 7,  "unpatched CV ROOT moved the root");
        CHECK(applySeedCv(300, v, false) == 300, "unpatched CV SEED moved the seed");
        CHECK(cvSlideAmount(v, false) == 0.f, "unpatched CV SLIDE contributed slide");
        CHECK(std::fabs(slideForStep(true, cvSlideAmount(v, false), 0.4f) - 0.4f) < 1e-5f,
              "unpatched CV SLIDE changed a flagged step's slide");
    }
    printf("unpatched: every CV input is a no-op when nothing is plugged in\n");
}

// ── CV SCALE: 1 V per position, clamped ─────────────────────────────────────
static void test_cv_scale() {
    // Override: when patched the knob is ignored and the voltage names the
    // position, 0-10 V across the whole list.
    CHECK(applyScaleCv(7, 0.f, true) == 0, "0 V should select position 0 (unquantized)");
    CHECK(applyScaleCv(7, 10.f, true) == NUM_SCALES - 1, "10 V should select the last scale");
    CHECK(applyScaleCv(0, 5.f, true) == (NUM_SCALES - 1) / 2,
          "5 V should land mid-list, got %d", applyScaleCv(0, 5.f, true));
    CHECK(applyScaleCv(7, 3.f, true) == applyScaleCv(2, 3.f, true),
          "the knob still influenced an overridden scale");

    // clamps at both ends, for any voltage a module might send
    for (float v = -20.f; v <= 20.f; v += 0.13f) {
        int r = applyScaleCv(4, v, true);
        CHECK(r >= 0 && r < NUM_SCALES, "%.2f V gave scale %d", v, r);
    }
    // every position reachable by sweeping 0-10 V
    bool reached[NUM_SCALES] = {};
    for (float v = 0.f; v <= 10.f; v += 0.002f) reached[applyScaleCv(0, v, true)] = true;
    for (int i = 0; i < NUM_SCALES; i++)
        CHECK(reached[i], "scale position %d unreachable by a 0-10 V sweep", i);
    printf("CV scale: override, 0-10 V spans all %d positions, clamped\n", NUM_SCALES);
}

// ── CV ROOT: V/oct, whole semitones only, wraps ─────────────────────────────
static void test_cv_root() {
    CHECK(applyRootCv(9, 0.f, true) == 0, "0 V should name C, overriding the knob");
    CHECK(applyRootCv(0, 1.f / 12.f, true) == 1, "1 semitone CV should name C#");
    CHECK(applyRootCv(0, 7.f / 12.f, true) == 7, "a fifth CV should name G");
    CHECK(applyRootCv(0, 1.f, true) == 0, "1 V (an octave) should wrap to the same root");
    CHECK(applyRootCv(5, -1.f, true) == 0, "-1 V should wrap to C, not to the knob");
    CHECK(applyRootCv(3, 0.5f, true) == applyRootCv(11, 0.5f, true),
          "the knob still influenced an overridden root");

    // THE INVARIANT: whatever the voltage, the result is always a whole
    // semitone in 0..11 — a smooth CV can never de-quantize the output.
    for (float v = -10.f; v <= 10.f; v += 0.001f) {
        int r = applyRootCv(4, v, true);
        CHECK(r >= 0 && r < 12, "%.4f V gave root %d", v, r);
    }
    // every root must be reachable by sweeping the CV
    bool reached[12] = {};
    for (float v = 0.f; v < 1.f; v += 0.0005f)
        reached[applyRootCv(0, v, true)] = true;
    for (int i = 0; i < 12; i++)
        CHECK(reached[i], "root %d unreachable by sweeping CV ROOT over one octave", i);
    printf("CV root: override, V/oct, always a whole semitone, all 12 reachable\n");
}

// A smooth ramp into CV ROOT must produce a staircase, not a slide.
static void test_cv_root_never_dequantizes() {
    QuantizeParams p;
    p.scaleIndex = SCALE_NAT_MINOR;
    p.octaveRange = 2;
    StepData st{};
    st.pitchIndex = 9;
    st.octaveRaw = 0.5f;
    st.octaveJump = -1;

    const Scale& sc = getScale(p.scaleIndex);
    for (float v = -5.f; v <= 5.f; v += 0.0007f) {
        p.root = applyRootCv(0, v, true);
        float volts = pitchVoltage(st, p);
        float semiF = volts * 12.f;
        int semi = int(std::lround(semiF));
        CHECK(std::fabs(semiF - float(semi)) < 1e-4f,
              "CV ROOT %.4f V produced %.4f V, off the semitone grid", v, volts);
        int rel = ((semi - p.root) % 12 + 12) % 12;
        bool inScale = false;
        for (int i = 0; i < sc.noteCount; i++)
            if (sc.intervals[i] == rel) inScale = true;
        CHECK(inScale, "CV ROOT %.4f V pushed the note out of key", v);
    }
    printf("CV root: a smooth ramp stays on the grid and in key at every point\n");
}

// ── CV SEED: offset, wraps ──────────────────────────────────────────────────
static void test_cv_seed() {
    CHECK(applySeedCv(0, 0.f, true) == 0, "0 V should not offset the seed");
    CHECK(applySeedCv(0, 10.f, true) == 0, "10 V is a full range and should wrap to 0");
    CHECK(applySeedCv(0, 5.f, true) == NUM_SEEDS / 2, "5 V should be half the range");
    CHECK(applySeedCv(100, -1.f, true) == ((100 - 102) % NUM_SEEDS + NUM_SEEDS) % NUM_SEEDS,
          "negative CV did not wrap correctly");

    for (float v = -30.f; v <= 30.f; v += 0.11f) {
        for (int knob : {0, 1, 511, 1023}) {
            int r = applySeedCv(knob, v, true);
            CHECK(r >= 0 && r < NUM_SEEDS, "knob %d at %.2f V gave seed %d", knob, v, r);
        }
    }
    // sweeping ±5 V from the middle should reach the whole range
    bool reached[NUM_SEEDS] = {};
    for (float v = -5.f; v <= 5.f; v += 0.001f)
        reached[applySeedCv(512, v, true)] = true;
    int missing = 0;
    for (int i = 0; i < NUM_SEEDS; i++) if (!reached[i]) missing++;
    CHECK(missing == 0, "%d seeds unreachable by a +/-5 V sweep", missing);
    printf("CV seed: offset, wraps, a +/-5 V sweep reaches all %d seeds\n", NUM_SEEDS);
}

// ── CV SLIDE: summed with the seed flag, then attenuated ────────────────────
static void test_cv_slide() {
    // the jack's own contribution
    CHECK(cvSlideAmount(0.f, true) == 0.f, "0 V contributed slide");
    CHECK(std::fabs(cvSlideAmount(10.f, true) - 1.f) < 1e-5f, "10 V is not full range");
    CHECK(std::fabs(cvSlideAmount(2.5f, true) - 0.25f) < 1e-5f, "2.5 V is not a quarter");
    CHECK(cvSlideAmount(-5.f, true) == 0.f, "negative CV was not clamped");
    CHECK(cvSlideAmount(50.f, true) == 1.f, "over-range CV was not clamped");

    // THE ATTENUATOR RULE: CCW kills slide however much CV arrives.
    for (float cv = 0.f; cv <= 1.f; cv += 0.05f) {
        CHECK(slideForStep(true,  cv, 0.f) == 0.f,
              "knob CCW still allowed slide on a flagged step at cv %.2f", cv);
        CHECK(slideForStep(false, cv, 0.f) == 0.f,
              "knob CCW still allowed slide on an unflagged step at cv %.2f", cv);
    }
    // a flagged step at full knob is full slide, with or without CV
    CHECK(std::fabs(slideForStep(true, 0.f, 1.f) - 1.f) < 1e-5f,
          "a flagged step at full knob is not full slide");
    CHECK(std::fabs(slideForStep(true, 1.f, 1.f) - 1.f) < 1e-5f,
          "CV pushed a flagged step past full slide");
    // CV puts glide on steps the seed did NOT flag — the useful direction
    CHECK(slideForStep(false, 0.f, 1.f) == 0.f,
          "an unflagged step slid with no CV");
    CHECK(std::fabs(slideForStep(false, 0.5f, 1.f) - 0.5f) < 1e-5f,
          "CV did not add slide to an unflagged step");
    // the knob scales the sum, so it is a multiplier not an addend
    CHECK(std::fabs(slideForStep(false, 1.f, 0.5f) - 0.5f) < 1e-5f,
          "the knob did not attenuate the CV contribution");

    // always in range, whatever arrives
    for (float cv = -2.f; cv <= 2.f; cv += 0.07f)
        for (float k = -1.f; k <= 2.f; k += 0.1f)
            for (int f = 0; f < 2; f++) {
                float r = slideForStep(f != 0, cv, k);
                CHECK(r >= 0.f && r <= 1.f, "slide %f out of 0..1", r);
            }
    printf("CV slide: summed with the seed flag then attenuated; CCW always kills it\n");
}

int main() {
    test_seed_knob_roundtrip();
    test_unpatched_is_neutral();
    test_cv_scale();
    test_cv_root();
    test_cv_root_never_dequantizes();
    test_cv_seed();
    test_cv_slide();

    if (failures) {
        printf("\n%d FAILURES\n", failures);
        return 1;
    }
    printf("\nall tests passed\n");
    return 0;
}
