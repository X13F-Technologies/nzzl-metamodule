// Reference-data generator for docs/SYSTEM_TEST_GUIDE.md § 6.5.
// Prints the exact step data and pitch voltages the module produces, as a
// markdown table, so the manual test guide can be checked against real numbers.
// Not a test — run_tests.sh only globs test_*.cc, so this stays out of it.
//
//   c++ -std=c++20 -O1 -o /tmp/nzzl_refgen tests/refgen.cc && /tmp/nzzl_refgen

#include "../src/scales.hh"
#include <cstdio>

using namespace nzzl;

static const char* NOTE[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

// Reference settings: Natural Minor, root C, 2 octaves, scale lock on.
static void dump(int seed) {
    StepData s[MAX_STEPS];
    generatePattern(seed, s);
    QuantizeParams p{ 2, 0, 2, true };

    printf("\n### seed_index %d  (GROUP %d / SUBGROUP %d)\n\n",
           seed, seed / 32 + 1, seed % 32 + 1);
    printf("| Step | Weight | On @ D4 | On @ D8 | Gate len | Vel (V) | Pitch (V) | Note |\n");
    printf("|---|---|---|---|---|---|---|---|\n");
    for (int i = 0; i < MAX_STEPS; i++) {
        float v = pitchVoltage(s[i], p);
        int semi = int(v * 12.f + 0.5f);
        printf("| %d | %d | %s | %s | %.2f | %.2f | %+.3f | %s%d |\n",
               i + 1, s[i].weight,
               s[i].weight <= 4 ? "●" : "·",
               s[i].weight <= 8 ? "●" : "·",
               s[i].gateLength, s[i].velocity * 10.f, v,
               NOTE[semi % 12], 4 + semi / 12);
    }
}

int main() {
    dump(0);    // GROUP 1 / SUBGROUP 1 — the table printed in the guide
    dump(1);    // GROUP 1 / SUBGROUP 2
    dump(32);   // GROUP 2 / SUBGROUP 1
    return 0;
}
