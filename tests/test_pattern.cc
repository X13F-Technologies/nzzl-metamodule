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
        StepData a[MAX_STEPS], b[MAX_STEPS];
        generatePattern(seed, a);
        generatePattern(seed, b);
        // field-wise compare (memcmp would read struct padding bytes)
        for (int i = 0; i < MAX_STEPS; i++) {
            bool same = a[i].weight == b[i].weight
                     && a[i].pitchIndex == b[i].pitchIndex
                     && a[i].gateLength == b[i].gateLength
                     && a[i].velocity == b[i].velocity
                     && a[i].slide == b[i].slide
                     && a[i].octaveRaw == b[i].octaveRaw;
            CHECK(same, "seed %d step %d not deterministic", seed, i);
        }
    }
    printf("determinism: all 1024 seeds OK\n");
}

// Weights are a permutation of 1..16 => density N activates exactly N steps
static void test_density_exact() {
    for (int seed = 0; seed < 1024; seed++) {
        StepData s[MAX_STEPS];
        generatePattern(seed, s);
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
        StepData a[MAX_STEPS], b[MAX_STEPS];
        generatePattern(seed, a);
        generatePattern(seed + 1, b);
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
        StepData s[MAX_STEPS];
        generatePattern(seed, s);
        for (int i = 0; i < MAX_STEPS; i++) {
            CHECK(s[i].pitchIndex >= 0 && s[i].pitchIndex < 16,
                  "seed %d pitchIndex %d", seed, s[i].pitchIndex);
            CHECK(s[i].gateLength >= 0.1f && s[i].gateLength <= 0.9f,
                  "seed %d gateLength %f", seed, s[i].gateLength);
            CHECK(s[i].velocity >= 0.f && s[i].velocity < 1.f,
                  "seed %d velocity %f", seed, s[i].velocity);
            CHECK(s[i].octaveRaw >= 0.f && s[i].octaveRaw < 1.f,
                  "seed %d octaveRaw %f", seed, s[i].octaveRaw);
        }
    }
    printf("ranges: all attributes in bounds, all seeds OK\n");
}

// Attribute independence: pitch stream must not depend on how many
// draws the weight stream makes (guards patch stability across versions)
static void test_attribute_isolation() {
    StepData s[MAX_STEPS];
    generatePattern(42, s);
    auto rng = attrRng(42, SALT_PITCH);
    for (int i = 0; i < MAX_STEPS; i++) {
        int expected = rng.nextInt(16);
        CHECK(s[i].pitchIndex == expected,
              "pitch stream not independent at step %d", i);
    }
    printf("attribute isolation: pitch stream independent of other attrs\n");
}

int main() {
    test_determinism();
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
