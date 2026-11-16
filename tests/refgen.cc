// Reference-data generator for docs/SYSTEM_TEST_GUIDE.md § 6.5.
// Prints the exact step data of one roll, as a markdown table. Item 16: the
// module no longer has seeds, so these numbers cannot be dialled up in Rack —
// they illustrate what a BASS / RAND / ARP row looks like, from harness roll 0.
// Not a test — run_tests.sh only globs test_*.cc, so this stays out of it.
//
//   c++ -std=c++20 -O1 -o /tmp/nzzl_refgen tests/refgen.cc && /tmp/nzzl_refgen

#include "../src/scales.hh"
#include "rolls.hh"
#include <cstdio>

using namespace nzzl;

static const char* NOTE[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

// Reference settings: Natural Minor, root C, 2 octaves, GATE 100%, ACCENT 100%.
static void dump(int k, int stylePos) {
    Pattern pat;
    nzzl_test::patternFor(k, stylePos, pat);
    QuantizeParams p{ SCALE_NAT_MINOR, 0, 2 };

    printf("\n### harness roll %d, STYLE %s\n\n", k, styleName(stylePos));
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
    dump(0, STYLE_POS_BASS);
    dump(0, STYLE_POS_RAND);
    dump(0, STYLE_POS_ARP_UP);
    return 0;
}
