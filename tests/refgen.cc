// Reference-data generator for docs/SYSTEM_TEST_GUIDE.md § 6.5.
// Prints the exact step data the module produces, as a markdown table, so the
// manual test guide can be checked against real numbers.
// Not a test — run_tests.sh only globs test_*.cc, so this stays out of it.
//
//   c++ -std=c++20 -O1 -o /tmp/nzzl_refgen tests/refgen.cc && /tmp/nzzl_refgen

#include "../src/scales.hh"
#include <cstdio>

using namespace nzzl;

static const char* NOTE[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

// Reference settings: Natural Minor, root C, 2 octaves, GATE 100%, ACCENT 100%.
static void dump(int seed) {
    Pattern pat;
    generatePattern(seed, pat);
    QuantizeParams p{ SCALE_NAT_MINOR, 0, 2 };

    int group = 0, subgroup = 0;
    seedKnobsFor(seed, group, subgroup);
    printf("\n### seed %d  (GROUP %d / SUBGROUP %d — %s)\n\n",
           seed, group, subgroup, styleName(seed));
    printf("Note lengths for this pattern: %.2f / %.2f / %.2f of a step\n\n",
           pat.gateLengths[0], pat.gateLengths[1], pat.gateLengths[2]);
    printf("| Step | Weight | On @ D4 | On @ D8 | Gate | Tie | Vel (V) | Pitch (V) | Note |\n");
    printf("|---|---|---|---|---|---|---|---|---|\n");
    for (int i = 0; i < MAX_STEPS; i++) {
        const StepData& s = pat.steps[i];
        float v = pitchVoltage(s, p);
        int semi = int(v * 12.f + (v < 0 ? -0.5f : 0.5f));
        printf("| %d | %d | %s | %s | %.2f | %s | %.2f | %+.3f | %s%d |\n",
               i + 1, s.weight,
               s.weight <= 4 ? "●" : "·",
               s.weight <= 8 ? "●" : "·",
               pat.gateLengths[s.gateIndex],
               s.slide ? "―" : " ",
               velocityFor(s.velLayer, 1.f) * 10.f, v,
               NOTE[((semi % 12) + 12) % 12], 4 + semi / 12);
    }
}

int main() {
    dump(0);            // GROUP 1  / SUBGROUP 1  — BASS (303)
    dump(10 * 32);      // GROUP 11 / SUBGROUP 1  — RAND
    dump(21 * 32);      // GROUP 22 / SUBGROUP 1  — ARP up
    return 0;
}
