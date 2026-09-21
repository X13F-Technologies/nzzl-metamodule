// Native test harness for NZZL's style zones — no Rack deps.
// Style is a statistical effect, so most of these are aggregate assertions
// over whole zones rather than per-seed checks.

#include "../src/pattern.hh"
#include "../src/scales.hh"
#include <cstdio>
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

static bool isAcidPitch(int idx) {
    return idx == ACID_ROOT || idx == ACID_THIRD || idx == ACID_FOURTH
        || idx == ACID_FIFTH || idx == ACID_SEVENTH;
}

// Bitmask of which steps play at a given density.
static unsigned activeMask(const Pattern& pat, int density) {
    unsigned m = 0;
    for (int i = 0; i < MAX_STEPS; i++)
        if (pat.steps[i].weight <= density) m |= (1u << i);
    return m;
}

// ── zones ───────────────────────────────────────────────────────────────────

// The handoff spec's boundaries: 1–10 BASS, 11–21 RAND, 22–32 ARP.
static void test_style_zones() {
    int counts[3] = {};
    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        int g = groupForSeed(seed);
        CHECK(g >= 1 && g <= NUM_GROUPS, "seed %d gave group %d", seed, g);
        Style st = styleForSeed(seed);
        Style expected = (g <= 10) ? STYLE_BASS : (g <= 21) ? STYLE_RANDOM : STYLE_ARP;
        CHECK(st == expected, "seed %d (group %d) got style %d, expected %d",
              seed, g, int(st), int(expected));
        counts[int(st)]++;
        CHECK(styleName(seed)[0] != '\0', "seed %d has no zone name", seed);
    }
    CHECK(styleForSeed(0) == STYLE_BASS,             "group 1 is not BASS");
    CHECK(styleForSeed(10 * 32 - 1) == STYLE_BASS,   "group 10 is not BASS");
    CHECK(styleForSeed(10 * 32) == STYLE_RANDOM,     "group 11 is not RAND");
    CHECK(styleForSeed(21 * 32 - 1) == STYLE_RANDOM, "group 21 is not RAND");
    CHECK(styleForSeed(21 * 32) == STYLE_ARP,        "group 22 is not ARP");
    CHECK(styleForSeed(NUM_SEEDS - 1) == STYLE_ARP,  "group 32 is not ARP");
    CHECK(counts[STYLE_BASS] == 320 && counts[STYLE_RANDOM] == 352
       && counts[STYLE_ARP] == 352,
          "zone sizes wrong: %d / %d / %d",
          counts[STYLE_BASS], counts[STYLE_RANDOM], counts[STYLE_ARP]);
    printf("zones: %d BASS / %d RAND / %d ARP, spec boundaries exact\n",
           counts[STYLE_BASS], counts[STYLE_RANDOM], counts[STYLE_ARP]);
}

// Inside the ARP zone the SUBGROUP knob picks the direction.
static void test_arp_subgroup_modes() {
    for (int seed = 21 * 32; seed < NUM_SEEDS; seed++) {
        int sg = subgroupForSeed(seed);
        ArpMode want = (sg <= 8) ? ARP_UP : (sg <= 16) ? ARP_DOWN
                     : (sg <= 24) ? ARP_UPDOWN : ARP_DOWNUP;
        CHECK(arpModeForSeed(seed) == want,
              "seed %d (subgroup %d) got arp mode %d, expected %d",
              seed, sg, int(arpModeForSeed(seed)), int(want));
    }
    // and the display names differ per mode
    const char* up   = styleName(21 * 32 + 0);
    const char* down = styleName(21 * 32 + 8);
    const char* ud   = styleName(21 * 32 + 16);
    const char* du   = styleName(21 * 32 + 24);
    CHECK(up != down && down != ud && ud != du, "arp modes share a display name");
    printf("arp modes: subgroup 1-8 up, 9-16 down, 17-24 up-down, 25-32 down-up\n");
}

// THE PATCH-STABILITY PROOF: the random zone must be what the unshaped
// generator produced. Style must be purely additive.
static void test_random_zone_unchanged() {
    int checked = 0;
    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        if (styleForSeed(seed) != STYLE_RANDOM) continue;
        Pattern styled, base;
        generatePattern(seed, styled);
        generateBasePattern(seed, base);
        for (int g = 0; g < NUM_GATE_LENGTHS; g++)
            CHECK(styled.gateLengths[g] == base.gateLengths[g],
                  "seed %d: style altered gate length %d", seed, g);
        for (int i = 0; i < MAX_STEPS; i++) {
            const StepData& a = styled.steps[i];
            const StepData& b = base.steps[i];
            CHECK(a.weight == b.weight && a.pitchIndex == b.pitchIndex
               && a.gateIndex == b.gateIndex && a.velLayer == b.velLayer
               && a.slide == b.slide && a.octaveJump == b.octaveJump
               && a.octaveRaw == b.octaveRaw,
                  "seed %d step %d: style layer altered the RANDOM zone", seed, i);
        }
        checked++;
    }
    printf("random zone: %d seeds bit-identical to the unshaped generator\n", checked);
}

// Style must never break the weight permutation — DENSITY exactness depends
// on it, and the bass zone rebuilds the weights outright.
static void test_style_preserves_permutation() {
    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        Pattern pat; generatePattern(seed, pat);
        bool seen[MAX_STEPS + 1] = {};
        for (int i = 0; i < MAX_STEPS; i++) {
            int w = pat.steps[i].weight;
            CHECK(w >= 1 && w <= MAX_STEPS, "seed %d weight %d out of range", seed, w);
            CHECK(!seen[w], "seed %d duplicate weight %d after style", seed, w);
            seen[w] = true;
        }
        for (int d = 1; d <= MAX_STEPS; d++) {
            int n = 0;
            for (int i = 0; i < MAX_STEPS; i++) if (pat.steps[i].weight <= d) n++;
            CHECK(n == d, "seed %d density %d activates %d steps", seed, d, n);
        }
    }
    printf("style: weights remain a permutation of 1..16 in every zone\n");
}

// ── the bug this rewrite exists to fix ──────────────────────────────────────
//
// The old bass zone pulled weights 1-4 onto a FIXED beat-priority table, so
// at DENSITY 4 all 320 bass seeds played the same four positions in the bar.
// Rhythm must vary seed to seed.
static void test_bass_rhythm_varies_between_seeds() {
    // Measured floors, comfortably under what the generator actually produces
    // (16 / 29 / 46 / 100 / 145 / 189 at densities 1-6) but far above the
    // 1-2 a fixed beat-priority table would give. Bass is DELIBERATELY more
    // constrained than the random zone — the point is that it is not collapsed.
    static const int FLOOR[7] = { 0, 12, 24, 38, 80, 120, 150 };

    for (int density = 1; density <= 6; density++) {
        unsigned seen[512]; int n = 0, total = 0, randDistinct = 0;
        unsigned rseen[512]; int rn = 0;
        for (int seed = 0; seed < NUM_SEEDS; seed++) {
            Style st = styleForSeed(seed);
            if (st != STYLE_BASS && st != STYLE_RANDOM) continue;
            Pattern pat; generatePattern(seed, pat);
            unsigned m = activeMask(pat, density);
            if (st == STYLE_BASS) {
                total++;
                bool known = false;
                for (int i = 0; i < n; i++) if (seen[i] == m) { known = true; break; }
                if (!known && n < 512) seen[n++] = m;
            } else {
                bool known = false;
                for (int i = 0; i < rn; i++) if (rseen[i] == m) { known = true; break; }
                if (!known && rn < 512) rseen[rn++] = m;
            }
        }
        randDistinct = rn;
        CHECK(n >= FLOOR[density],
              "density %d: only %d distinct bass rhythms across %d seeds "
              "(floor %d) — the zone has collapsed", density, n, total, FLOOR[density]);
        CHECK(density == 1 || n < randDistinct,
              "density %d: bass is not more constrained than random (%d vs %d)",
              density, n, randDistinct);
        if (density == 4)
            printf("bass rhythm: %d distinct patterns across %d seeds at DENSITY 4 "
                   "(random zone %d; a fixed beat table gives 1)\n",
                   n, total, randDistinct);
    }
}

// Acid lines run in sixteenths: active steps should be adjacent far more
// often than chance, and more often than in the random zone.
static void test_bass_runs_of_sixteenths() {
    auto adjacency = [](Style want, int density) {
        int adj = 0, active = 0;
        for (int seed = 0; seed < NUM_SEEDS; seed++) {
            if (styleForSeed(seed) != want) continue;
            Pattern pat; generatePattern(seed, pat);
            unsigned m = activeMask(pat, density);
            for (int i = 0; i < MAX_STEPS; i++) {
                if (!(m & (1u << i))) continue;
                active++;
                if (m & (1u << ((i + 1) % MAX_STEPS))) adj++;
            }
        }
        return active ? float(adj) / float(active) : 0.f;
    };
    float bass   = adjacency(STYLE_BASS, 6);
    float random = adjacency(STYLE_RANDOM, 6);
    CHECK(bass > random * 1.25f,
          "bass notes are not run-like: %.2f adjacency vs random %.2f", bass, random);
    printf("bass runs: %.0f%% of notes are followed immediately by another "
           "(random zone %.0f%%)\n", bass * 100, random * 100);
}

// The downbeat should usually lead the pattern.
static void test_bass_downbeat() {
    int onDownbeat = 0, total = 0;
    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        if (styleForSeed(seed) != STYLE_BASS) continue;
        Pattern pat; generatePattern(seed, pat);
        total++;
        if (pat.steps[0].weight == 1) onDownbeat++;
    }
    float frac = float(onDownbeat) / float(total);
    CHECK(frac > 0.7f && frac < 0.95f,
          "bass downbeat rate %.2f is outside the intended band", frac);
    printf("bass downbeat: %.0f%% of seeds put their first note on step 1\n", frac * 100);
}

// Acid vocabulary: root-dominated, and nothing outside the acid set.
static void test_bass_pitch_vocabulary() {
    int root = 0, inSet = 0, total = 0;
    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        if (styleForSeed(seed) != STYLE_BASS) continue;
        Pattern pat; generatePattern(seed, pat);
        for (int i = 0; i < MAX_STEPS; i++) {
            int idx = pat.steps[i].pitchIndex;
            total++;
            if (isAcidPitch(idx)) inSet++;
            if (idx == ACID_ROOT) root++;
            CHECK(isAcidPitch(idx),
                  "seed %d step %d: pitch %d is outside the acid vocabulary",
                  seed, i, idx);
        }
    }
    CHECK(inSet == total, "%d bass notes left the acid set", total - inSet);
    float rootFrac = float(root) / float(total);
    CHECK(rootFrac > 0.45f && rootFrac < 0.65f,
          "root dominance %.2f is outside the intended band", rootFrac);
    // and the degrees those indices map to, in a 7-note scale
    CHECK(degreeForIndex(ACID_ROOT, 7) == 0,    "ACID_ROOT is not degree 0");
    CHECK(degreeForIndex(ACID_THIRD, 7) == 2,   "ACID_THIRD is not degree 2");
    CHECK(degreeForIndex(ACID_FOURTH, 7) == 3,  "ACID_FOURTH is not degree 3");
    CHECK(degreeForIndex(ACID_FIFTH, 7) == 4,   "ACID_FIFTH is not degree 4");
    CHECK(degreeForIndex(ACID_SEVENTH, 7) == 6, "ACID_SEVENTH is not degree 6");
    printf("bass pitch: %.0f%% root, every note from the acid set "
           "(root/3rd/4th/5th/7th)\n", rootFrac * 100);
}

// Octave-up jumps: present, rare, and never anything but +1.
static void test_bass_octave_jumps() {
    int up = 0, total = 0;
    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        if (styleForSeed(seed) != STYLE_BASS) continue;
        Pattern pat; generatePattern(seed, pat);
        for (int i = 0; i < MAX_STEPS; i++) {
            int j = pat.steps[i].octaveJump;
            CHECK(j == 0 || j == 1, "seed %d step %d octaveJump %d", seed, i, j);
            if (j == 1) up++;
            total++;
        }
    }
    float frac = float(up) / float(total);
    CHECK(frac > 0.10f && frac < 0.28f,
          "octave-up rate %.2f is outside the intended band", frac);
    // and the jump survives a narrow OCTAVE RANGE setting
    CHECK(octaveForStep(1, 0.f, 1) == 0, "a jump did not clamp at range 1");
    CHECK(octaveForStep(1, 0.f, 2) == 1, "a jump was lost at range 2");
    CHECK(octaveForStep(1, 0.f, 5) == 1, "a jump grew with the range");
    printf("bass octaves: %.0f%% of notes jump up one, and stay +1 at any range\n",
           frac * 100);
}

// Ties are common in acid, and far more common than in the random zone.
static void test_bass_ties() {
    auto slideRate = [](Style want) {
        int n = 0, total = 0;
        for (int seed = 0; seed < NUM_SEEDS; seed++) {
            if (styleForSeed(seed) != want) continue;
            Pattern pat; generatePattern(seed, pat);
            for (int i = 0; i < MAX_STEPS; i++) { total++; if (pat.steps[i].slide) n++; }
        }
        return total ? float(n) / float(total) : 0.f;
    };
    float bass = slideRate(STYLE_BASS), random = slideRate(STYLE_RANDOM);
    CHECK(bass > random * 1.15f, "bass does not tie more than random (%.2f vs %.2f)",
          bass, random);
    printf("bass ties: %.0f%% of steps slide (random zone %.0f%%)\n",
           bass * 100, random * 100);
}

// Arp: the pattern must follow its subgroup's direction.
static void test_arp_shapes() {
    int checked = 0;
    for (int seed = 21 * 32; seed < NUM_SEEDS; seed += 7) {
        Pattern pat; generatePattern(seed, pat);
        const int* p0 = nullptr; (void)p0;
        ArpMode mode = arpModeForSeed(seed);
        const StepData* s = pat.steps;
        if (mode == ARP_UP) {
            for (int i = 1; i < MAX_STEPS; i++)
                CHECK(s[i].pitchIndex >= s[i-1].pitchIndex,
                      "seed %d ARP_UP dips at step %d", seed, i);
        } else if (mode == ARP_DOWN) {
            for (int i = 1; i < MAX_STEPS; i++)
                CHECK(s[i].pitchIndex <= s[i-1].pitchIndex,
                      "seed %d ARP_DOWN rises at step %d", seed, i);
        } else if (mode == ARP_UPDOWN) {
            for (int i = 1; i < MAX_STEPS / 2; i++)
                CHECK(s[i].pitchIndex >= s[i-1].pitchIndex,
                      "seed %d ARP_UPDOWN first half dips at %d", seed, i);
            for (int i = MAX_STEPS / 2 + 1; i < MAX_STEPS; i++)
                CHECK(s[i].pitchIndex <= s[i-1].pitchIndex,
                      "seed %d ARP_UPDOWN second half rises at %d", seed, i);
        } else {
            for (int i = 1; i < MAX_STEPS / 2; i++)
                CHECK(s[i].pitchIndex <= s[i-1].pitchIndex,
                      "seed %d ARP_DOWNUP first half rises at %d", seed, i);
            for (int i = MAX_STEPS / 2 + 1; i < MAX_STEPS; i++)
                CHECK(s[i].pitchIndex >= s[i-1].pitchIndex,
                      "seed %d ARP_DOWNUP second half dips at %d", seed, i);
        }
        // arps are even and do not glide
        for (int i = 0; i < MAX_STEPS; i++) {
            CHECK(!s[i].slide, "seed %d step %d: an arp slid", seed, i);
            CHECK(s[i].gateIndex == 1, "seed %d step %d: arp gate is not consistent",
                  seed, i);
        }
        checked++;
    }
    printf("arp shapes: %d seeds follow their subgroup's direction, even gates, "
           "no glide\n", checked);
}

// Each zone must still have its own gate character.
static void test_gate_character() {
    auto meanIndex = [](Style want) {
        double sum = 0; int n = 0;
        for (int seed = 0; seed < NUM_SEEDS; seed++) {
            if (styleForSeed(seed) != want) continue;
            Pattern pat; generatePattern(seed, pat);
            for (int i = 0; i < MAX_STEPS; i++) { sum += pat.steps[i].gateIndex; n++; }
        }
        return n ? float(sum / n) : 0.f;
    };
    float bass = meanIndex(STYLE_BASS), random = meanIndex(STYLE_RANDOM),
          arp  = meanIndex(STYLE_ARP);
    CHECK(bass < random, "bass gates are not shorter than random (%.2f vs %.2f)",
          bass, random);
    CHECK(std::fabs(arp - 1.f) < 1e-5f, "arp gates are not all the middle length");
    printf("gate character: mean length index — bass %.2f, random %.2f, arp %.2f\n",
           bass, random, arp);
}

// Determinism, and no two neighbouring seeds collapsing together.
static void test_determinism_and_variety() {
    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        Pattern a, b;
        generatePattern(seed, a);
        generatePattern(seed, b);
        for (int i = 0; i < MAX_STEPS; i++)
            CHECK(a.steps[i].weight == b.steps[i].weight
               && a.steps[i].pitchIndex == b.steps[i].pitchIndex
               && a.steps[i].gateIndex == b.steps[i].gateIndex
               && a.steps[i].velLayer == b.steps[i].velLayer,
                  "seed %d step %d not deterministic after style", seed, i);
    }
    int identical = 0;
    for (int seed = 0; seed < NUM_SEEDS - 1; seed++) {
        if (styleForSeed(seed) != styleForSeed(seed + 1)) continue;
        Pattern a, b;
        generatePattern(seed, a);
        generatePattern(seed + 1, b);
        int same = 0;
        for (int i = 0; i < MAX_STEPS; i++)
            if (a.steps[i].pitchIndex == b.steps[i].pitchIndex
             && a.steps[i].weight == b.steps[i].weight) same++;
        if (same >= 15) identical++;
    }
    CHECK(identical == 0,
          "%d neighbouring seeds collapsed into near-identical patterns", identical);
    printf("style: deterministic, and no two neighbouring seeds collapse\n");
}

int main() {
    test_style_zones();
    test_arp_subgroup_modes();
    test_random_zone_unchanged();
    test_style_preserves_permutation();
    test_bass_rhythm_varies_between_seeds();
    test_bass_runs_of_sixteenths();
    test_bass_downbeat();
    test_bass_pitch_vocabulary();
    test_bass_octave_jumps();
    test_bass_ties();
    test_arp_shapes();
    test_gate_character();
    test_determinism_and_variety();

    if (failures) {
        printf("\n%d FAILURES\n", failures);
        return 1;
    }
    printf("\nall tests passed\n");
    return 0;
}
