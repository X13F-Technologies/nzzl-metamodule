// Native test harness for NZZL's pattern generation — no Rack/MetaModule deps.
// Item 16: there are no seeds. These are properties of a roll, checked over
// NUM_ROLLS reproducible rolls (tests/rolls.hh) and every STYLE row of each.
// Build & run:  ./tests/run_tests.sh

#include "../src/pattern.hh"
#include "rolls.hh"
#include <cstdio>
#include <cstring>
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

using namespace nzzl_test;

// The roll is pure given its entropy: the only randomness is the draw at
// Generate. Same entropy -> identical roll, every row.
static void test_roll_is_pure() {
    for (int k = 0; k < NUM_ROLLS; k++) {
        Roll a, b;
        rollFor(k, a);
        rollFor(k, b);
        for (int r = 0; r < NUM_STYLES; r++)
            CHECK(samePattern(a.rows[r], b.rows[r]),
                  "roll %d row %d: same entropy gave a different pattern", k, r);
    }
    printf("roll purity: same entropy -> identical rows, %d rolls\n", NUM_ROLLS);
}

// One shuffled order per row => density N activates exactly N steps, and
// turning DENSITY up only ever adds steps (nested). Checked at every LENGTH
// and SHIFT too, since stepActive ranks within the loop window.
static void test_density_exact_and_nested() {
    long checked = 0;
    for (int k = 0; k < NUM_ROLLS; k++) {
        Roll roll; rollFor(k, roll);
        for (int r = 0; r < NUM_STYLES; r++) {
            const Pattern& pat = roll.rows[r];
            const StepData* s = pat.steps;
            bool seen[MAX_STEPS + 1] = {};
            for (int i = 0; i < MAX_STEPS; i++) {
                CHECK(s[i].weight >= 1 && s[i].weight <= 16,
                      "roll %d row %d step %d weight %d out of range", k, r, i, s[i].weight);
                CHECK(!seen[s[i].weight],
                      "roll %d row %d duplicate weight %d", k, r, s[i].weight);
                seen[s[i].weight] = true;
            }
            // a sample of lengths/shifts per row keeps the run time sane
            const int length = 2 + (k + r) % 15;
            const int shift  = (k * 7 + r) % 17 - 8;
            for (int len : {16, length}) {
                bool prev[MAX_STEPS] = {};
                for (int d = 1; d <= len; d++) {
                    int n = 0;
                    for (int i = 0; i < len; i++) {
                        const bool on = stepActive(pat, i, len, d, shift);
                        if (on) n++;
                        CHECK(!prev[i] || on,
                              "roll %d row %d len %d: step %d dropped out at density %d",
                              k, r, len, i, d);
                        prev[i] = on;
                    }
                    CHECK(n == d, "roll %d row %d len %d density %d: %d steps active",
                          k, r, len, d, n);
                    checked++;
                }
            }
        }
    }
    printf("density: exact and nested in every row of %d rolls (%ld cases)\n",
           NUM_ROLLS, checked);
}

// Successive Generates must give genuinely different patterns.
static void test_rolls_differ() {
    int nearIdentical = 0;
    for (int k = 0; k + 1 < NUM_ROLLS; k++) {
        Pattern a, b;
        patternFor(k, STYLE_POS_RAND, a);
        patternFor(k + 1, STYLE_POS_RAND, b);
        int samePitch = 0, sameWeight = 0;
        for (int i = 0; i < MAX_STEPS; i++) {
            if (a.steps[i].pitchIndex == b.steps[i].pitchIndex) samePitch++;
            if (a.steps[i].weight == b.steps[i].weight) sameWeight++;
        }
        // Chance is ~1 in 16 per step; >= 12 of 16 matching means a bug.
        if (samePitch >= 12 && sameWeight >= 12)
            nearIdentical++;
    }
    CHECK(nearIdentical == 0, "%d successive rolls are near-identical", nearIdentical);
    printf("roll variety: no two successive rolls near-identical\n");
}

// Value ranges, every row of every roll.
static void test_ranges() {
    for (int k = 0; k < NUM_ROLLS; k++) {
        Roll roll; rollFor(k, roll);
        for (int r = 0; r < NUM_STYLES; r++) {
            const Pattern& pat = roll.rows[r];
            const StepData* s = pat.steps;
            for (int g = 0; g < NUM_GATE_LENGTHS; g++)
                CHECK(pat.gateLengths[g] > 0.f && pat.gateLengths[g] <= 1.f,
                      "roll %d row %d gateLength[%d] = %f", k, r, g, pat.gateLengths[g]);
            CHECK(pat.gateLengths[0] < pat.gateLengths[1]
               && pat.gateLengths[1] < pat.gateLengths[2],
                  "roll %d row %d gate lengths not short<mid<long", k, r);
            CHECK(pat.swing >= 0.f && pat.swing <= 1.f, "roll %d swing %f", k, pat.swing);
            for (int i = 0; i < MAX_STEPS; i++) {
                CHECK(s[i].pitchIndex >= 0 && s[i].pitchIndex < 16,
                      "roll %d row %d pitchIndex %d", k, r, s[i].pitchIndex);
                CHECK(s[i].gateIndex >= 0 && s[i].gateIndex < NUM_GATE_LENGTHS,
                      "roll %d row %d gateIndex %d", k, r, s[i].gateIndex);
                CHECK(s[i].velLayer >= 0 && s[i].velLayer < NUM_VEL_LAYERS,
                      "roll %d row %d velLayer %d", k, r, s[i].velLayer);
                CHECK(s[i].octaveJump >= -1 && s[i].octaveJump <= 4,
                      "roll %d row %d octaveJump %d", k, r, s[i].octaveJump);
                CHECK(s[i].octaveRaw >= 0.f && s[i].octaveRaw < 1.f,
                      "roll %d row %d octaveRaw %f", k, r, s[i].octaveRaw);
            }
        }
    }
    printf("ranges: all attributes in bounds, every row of %d rolls\n", NUM_ROLLS);
}

// Lanes draw independently within a roll: changing one lane's entropy
// changes only that lane's attribute in the RAND (base) row.
static void test_lanes_independent() {
    for (int k = 0; k < 64; k++) {
        RollEntropy e = entropyFor(k);
        Pattern a; generateBasePattern(e, a);
        e.lane[LANE_PITCH] ^= 0x5A5A1234u;
        Pattern b; generateBasePattern(e, b);
        int pitchDiffs = 0;
        for (int i = 0; i < MAX_STEPS; i++) {
            const StepData& x = a.steps[i];
            const StepData& y = b.steps[i];
            if (x.pitchIndex != y.pitchIndex) pitchDiffs++;
            CHECK(x.weight == y.weight && x.gateIndex == y.gateIndex
               && x.velLayer == y.velLayer && x.slide == y.slide
               && x.octaveRaw == y.octaveRaw,
                  "roll %d step %d: re-rolling pitch disturbed another lane", k, i);
        }
        CHECK(pitchDiffs > 0, "roll %d: re-rolling the pitch lane changed nothing", k);
        for (int g = 0; g < NUM_GATE_LENGTHS; g++)
            CHECK(a.gateLengths[g] == b.gateLengths[g],
                  "roll %d: re-rolling pitch moved gate length %d", k, g);
        CHECK(a.swing == b.swing, "roll %d: re-rolling pitch moved swing", k);
    }
    printf("lane independence: re-rolling one lane leaves every other lane alone\n");
}

// The rows of one roll are built from one base: RAND is the base itself, and
// the ARP rows reorder its pitches without changing which steps play.
static void test_rows_share_a_base() {
    for (int k = 0; k < NUM_ROLLS; k++) {
        const RollEntropy e = entropyFor(k);
        Pattern base; generateBasePattern(e, base);
        Roll roll; rollPatterns(e, roll);
        CHECK(samePattern(base, roll.rows[STYLE_POS_RAND]),
              "roll %d: RAND row is not the base pattern", k);

        int baseCount[16] = {};
        for (int i = 0; i < MAX_STEPS; i++) baseCount[base.steps[i].pitchIndex]++;
        for (int r = STYLE_POS_ARP_UP; r <= STYLE_POS_ARP_DOWNUP; r++) {
            int c[16] = {};
            for (int i = 0; i < MAX_STEPS; i++) {
                c[roll.rows[r].steps[i].pitchIndex]++;
                CHECK(roll.rows[r].steps[i].weight == base.steps[i].weight,
                      "roll %d row %d: arp changed the rhythm", k, r);
            }
            for (int p = 0; p < 16; p++)
                CHECK(c[p] == baseCount[p],
                      "roll %d row %d: arp changed the set of pitches", k, r);
        }
    }
    printf("rows: RAND is the base, ARP rows reorder its notes, %d rolls\n", NUM_ROLLS);
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
    test_roll_is_pure();
    test_accent_scaling();
    test_density_exact_and_nested();
    test_rolls_differ();
    test_ranges();
    test_lanes_independent();
    test_rows_share_a_base();

    if (failures) {
        printf("\n%d FAILURES\n", failures);
        return 1;
    }
    printf("\nall tests passed\n");
    return 0;
}
