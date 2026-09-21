// Native test harness for NZZL's pure logic — no Rack/MetaModule deps.
// Build & run:  ./tests/run_tests.sh

#include "../src/pattern.hh"
#include <cstdio>
#include <cstring>
#include <cmath>

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

// Same seed always produces identical patterns
static void test_determinism() {
    for (int seed = 0; seed < 1024; seed++) {
        Pattern pa, pb;
        generatePattern(seed, pa);
        generatePattern(seed, pb);
        StepData *a = pa.steps, *b = pb.steps;
        // field-wise compare (memcmp would read struct padding bytes)
        for (int i = 0; i < MAX_STEPS; i++) {
            bool same = a[i].weight == b[i].weight
                     && a[i].pitchIndex == b[i].pitchIndex
                     && a[i].gateIndex == b[i].gateIndex
                     && a[i].velLayer == b[i].velLayer
                     && a[i].slide == b[i].slide
                     && a[i].octaveJump == b[i].octaveJump
                     && a[i].octaveRaw == b[i].octaveRaw;
            CHECK(same, "seed %d step %d not deterministic", seed, i);
        }
    }
    printf("determinism: all 1024 seeds OK\n");
}

// Weights are a permutation of 1..16 => density N activates exactly N steps
static void test_density_exact() {
    for (int seed = 0; seed < 1024; seed++) {
        Pattern pat; generatePattern(seed, pat); StepData* s = pat.steps;
        bool seen[MAX_STEPS + 1] = {};
        for (int i = 0; i < MAX_STEPS; i++) {
            CHECK(s[i].weight >= 1 && s[i].weight <= 16,
                  "seed %d step %d weight %d out of range", seed, i, s[i].weight);
            CHECK(!seen[s[i].weight],
                  "seed %d duplicate weight %d", seed, s[i].weight);
            seen[s[i].weight] = true;
        }
        for (int density = 1; density <= 16; density++) {
            int active = 0;
            for (int i = 0; i < MAX_STEPS; i++)
                if (s[i].weight <= density)
                    active++;
            CHECK(active == density,
                  "seed %d density %d activates %d steps", seed, density, active);
        }
    }
    printf("density: exactly N steps active at density N, all seeds OK\n");
}

// Adjacent seeds must produce meaningfully different patterns
static void test_seed_distinctness() {
    int identicalNeighbors = 0;
    for (int seed = 0; seed < 1023; seed++) {
        Pattern pa, pb;
        generatePattern(seed, pa);
        generatePattern(seed + 1, pb);
        StepData *a = pa.steps, *b = pb.steps;
        int samePitch = 0, sameWeight = 0;
        for (int i = 0; i < MAX_STEPS; i++) {
            if (a[i].pitchIndex == b[i].pitchIndex) samePitch++;
            if (a[i].weight == b[i].weight) sameWeight++;
        }
        // Random chance: pitch matches ~1/16 per step => ~1 of 16.
        // Flag near-identical neighbors (>= 12 of 16 matching).
        if (samePitch >= 12 && sameWeight >= 12)
            identicalNeighbors++;
    }
    CHECK(identicalNeighbors == 0,
          "%d adjacent seed pairs are near-identical", identicalNeighbors);
    printf("seed distinctness: no near-identical adjacent seeds\n");
}

// Value ranges
static void test_ranges() {
    for (int seed = 0; seed < 1024; seed++) {
        Pattern pat; generatePattern(seed, pat); StepData* s = pat.steps;
        for (int g = 0; g < NUM_GATE_LENGTHS; g++)
            CHECK(pat.gateLengths[g] > 0.f && pat.gateLengths[g] <= 1.f,
                  "seed %d gateLength[%d] = %f", seed, g, pat.gateLengths[g]);
        CHECK(pat.gateLengths[0] < pat.gateLengths[1]
           && pat.gateLengths[1] < pat.gateLengths[2],
              "seed %d gate lengths not short<mid<long", seed);
        for (int i = 0; i < MAX_STEPS; i++) {
            CHECK(s[i].pitchIndex >= 0 && s[i].pitchIndex < 16,
                  "seed %d pitchIndex %d", seed, s[i].pitchIndex);
            CHECK(s[i].gateIndex >= 0 && s[i].gateIndex < NUM_GATE_LENGTHS,
                  "seed %d gateIndex %d", seed, s[i].gateIndex);
            CHECK(s[i].velLayer >= 0 && s[i].velLayer < NUM_VEL_LAYERS,
                  "seed %d velLayer %d", seed, s[i].velLayer);
            CHECK(s[i].octaveJump >= -1 && s[i].octaveJump <= 4,
                  "seed %d octaveJump %d", seed, s[i].octaveJump);
            CHECK(s[i].octaveRaw >= 0.f && s[i].octaveRaw < 1.f,
                  "seed %d octaveRaw %f", seed, s[i].octaveRaw);
        }
    }
    printf("ranges: all attributes in bounds, all seeds OK\n");
}

// Attribute independence: pitch stream must not depend on how many
// draws the weight stream makes (guards patch stability across versions)
static void test_attribute_isolation() {
    Pattern pat;
    // The base generator is what owns stream isolation; the style layer
    // deliberately rewrites some attributes afterwards from its own salt.
    generateBasePattern(42, pat);
    StepData* s = pat.steps;
    auto rng = attrRng(42, SALT_PITCH);
    for (int i = 0; i < MAX_STEPS; i++) {
        int expected = rng.nextInt(16);
        CHECK(s[i].pitchIndex == expected,
              "pitch stream not independent at step %d", i);
    }
    printf("attribute isolation: pitch stream independent of other attrs\n");
}

// ACCENT flattens or spreads the three velocity layers.
static void test_accent_scaling() {
    for (int l = 0; l < NUM_VEL_LAYERS; l++)
        CHECK(std::fabs(velocityFor(l, 0.f) - VEL_FLAT) < 1e-5f,
              "accent 0 did not flatten layer %d", l);
    for (int l = 0; l < NUM_VEL_LAYERS; l++)
        CHECK(std::fabs(velocityFor(l, 1.f) - VEL_LAYER[l]) < 1e-5f,
              "accent 100 did not reach layer %d's full value", l);
    // monotonic in the layer at every accent setting above zero
    for (float a = 0.1f; a <= 1.f; a += 0.1f)
        CHECK(velocityFor(0, a) < velocityFor(1, a)
           && velocityFor(1, a) < velocityFor(2, a),
              "accent %.1f: layers are not ordered off < low < high", a);
    // and never leaves 0..1
    for (float a = 0.f; a <= 1.f; a += 0.05f)
        for (int l = -2; l < 5; l++) {
            float v = velocityFor(l, a);
            CHECK(v >= 0.f && v <= 1.f, "velocity %f out of range", v);
        }
    printf("accent: 0 flattens the layers, 100 spreads them fully, always ordered\n");
}

int main() {
    test_determinism();
    test_accent_scaling();
    test_density_exact();
    test_seed_distinctness();
    test_ranges();
    test_attribute_isolation();

    if (failures) {
        printf("\n%d FAILURES\n", failures);
        return 1;
    }
    printf("\nall tests passed\n");
    return 0;
}
