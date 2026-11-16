// Native test harness for NZZL's STYLE rows — no Rack deps.
// Style is a statistical effect, so most of these are aggregate assertions
// over the same STYLE row of many rolls (tests/rolls.hh) rather than per-roll
// checks. Item 16 replaced the GROUP zones with the six-position STYLE knob.

#include "../src/pattern.hh"
#include "../src/scales.hh"
#include "rolls.hh"
#include <cstdio>
#include <cmath>
#include <string_view>

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

// Call f(pattern, k) for the given STYLE row of every test roll.
template <typename F>
static void forRows(int stylePos, F f) {
    for (int k = 0; k < NUM_ROLLS; k++) {
        Pattern pat; patternFor(k, stylePos, pat);
        f(pat, k);
    }
}

static bool isAcidRole(int r) {
    return r == ACID_ROOT || r == ACID_THIRD || r == ACID_FOURTH
        || r == ACID_FIFTH || r == ACID_SEVENTH;
}

// Bitmask of which steps play at a given density.
static unsigned activeMask(const Pattern& pat, int density) {
    unsigned m = 0;
    for (int i = 0; i < MAX_STEPS; i++)
        if (pat.steps[i].weight <= density) m |= (1u << i);
    return m;
}

// ── STYLE positions ─────────────────────────────────────────────────────────

// Six detented positions: BASS, RAND, ARP up / down / up-down / down-up.
// The index order is saved in patches, so it is pinned here.
static void test_style_positions() {
    CHECK(NUM_STYLES == 6, "STYLE has %d positions, expected 6", NUM_STYLES);
    CHECK(styleOf(STYLE_POS_BASS) == STYLE_BASS,   "position 0 is not BASS");
    CHECK(styleOf(STYLE_POS_RAND) == STYLE_RANDOM, "position 1 is not RAND");
    const ArpMode want[4] = { ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_DOWNUP };
    for (int i = 0; i < 4; i++) {
        CHECK(styleOf(STYLE_POS_ARP_UP + i) == STYLE_ARP, "position %d is not an ARP", 2 + i);
        CHECK(arpModeOf(STYLE_POS_ARP_UP + i) == want[i],
              "position %d has arp mode %d, expected %d",
              2 + i, int(arpModeOf(STYLE_POS_ARP_UP + i)), int(want[i]));
    }
    // names: present and all different
    for (int a = 0; a < NUM_STYLES; a++) {
        CHECK(styleName(a)[0] != '\0', "position %d has no name", a);
        for (int b = a + 1; b < NUM_STYLES; b++)
            CHECK(styleName(a) != styleName(b), "positions %d and %d share a name", a, b);
    }
    CHECK(std::string_view(styleName(0)) == "BASS" && std::string_view(styleName(1)) == "RAND",
          "BASS/RAND names wrong");
    // out-of-range positions (an old GROUP value in a test-era patch) clamp
    CHECK(clampStyle(-3) == 0 && clampStyle(32) == NUM_STYLES - 1, "STYLE did not clamp");
    CHECK(styleOf(32) == STYLE_ARP && arpModeOf(32) == ARP_DOWNUP, "clamped STYLE misread");
    printf("style positions: BASS, RAND, ARP up/down/up-down/down-up, clamped\n");
}

// Style must never break the weight permutation — DENSITY exactness depends
// on it, and the bass zone rebuilds the weights outright.
static void test_style_preserves_permutation() {
    for (int st = 0; st < NUM_STYLES; st++)
        forRows(st, [st](const Pattern& pat, int k) {
            bool seen[MAX_STEPS + 1] = {};
            for (int i = 0; i < MAX_STEPS; i++) {
                int w = pat.steps[i].weight;
                CHECK(w >= 1 && w <= MAX_STEPS, "roll %d row %d weight %d out of range", k, st, w);
                CHECK(!seen[w], "roll %d row %d duplicate weight %d after style", k, st, w);
                seen[w] = true;
            }
        });
    printf("style: weights remain a permutation of 1..16 in every row\n");
}

// ── the bug the bass shaping was rewritten to fix ───────────────────────────
//
// The first bass zone pulled weights 1-4 onto a FIXED beat-priority table, so
// at DENSITY 4 every bass pattern played the same four positions in the bar.
// Rhythm must vary roll to roll.
static int distinctRhythms(int stylePos, int density) {
    static unsigned seen[NUM_ROLLS];
    int n = 0;
    forRows(stylePos, [&](const Pattern& pat, int) {
        const unsigned m = activeMask(pat, density);
        for (int i = 0; i < n; i++) if (seen[i] == m) return;
        seen[n++] = m;
    });
    return n;
}

static void test_bass_rhythm_varies_across_rolls() {
    // Floors comfortably under what the generator produces over NUM_ROLLS
    // rolls (measured 2026-11-16: 16 / 29 / 67 / 201 / 285 / 366 at DENSITY
    // 1-6; DENSITY 2 is structurally small, the downbeat plus one run
    // neighbour), but far above the 1-2 a fixed beat table
    // would give. Bass is DELIBERATELY more constrained than RAND — the
    // point is that it is not collapsed.
    static const int FLOOR[7] = { 0, 14, 24, 55, 160, 230, 300 };
    for (int density = 1; density <= 6; density++) {
        const int bass = distinctRhythms(STYLE_POS_BASS, density);
        const int rnd  = distinctRhythms(STYLE_POS_RAND, density);
        CHECK(bass >= FLOOR[density],
              "density %d: only %d distinct bass rhythms across %d rolls "
              "(floor %d) — the style has collapsed", density, bass, NUM_ROLLS,
              FLOOR[density]);
        CHECK(density == 1 || bass < rnd,
              "density %d: bass is not more constrained than random (%d vs %d)",
              density, bass, rnd);
        printf("bass rhythm: DENSITY %d — %d distinct across %d rolls (RAND %d)\n",
               density, bass, NUM_ROLLS, rnd);
    }
}

// Acid lines run in sixteenths: active steps should be adjacent far more
// often than chance, and more often than in the random zone.
static void test_bass_runs_of_sixteenths() {
    auto adjacency = [](int want, int density) {
        int adj = 0, active = 0;
        forRows(want, [&](const Pattern& pat, int) {
            unsigned m = activeMask(pat, density);
            for (int i = 0; i < MAX_STEPS; i++) {
                if (!(m & (1u << i))) continue;
                active++;
                if (m & (1u << ((i + 1) % MAX_STEPS))) adj++;
            }
        });
        return active ? float(adj) / float(active) : 0.f;
    };
    float bass   = adjacency(STYLE_POS_BASS, 6);
    float random = adjacency(STYLE_POS_RAND, 6);
    CHECK(bass > random * 1.25f,
          "bass notes are not run-like: %.2f adjacency vs random %.2f", bass, random);
    printf("bass runs: %.0f%% of notes are followed immediately by another "
           "(RAND %.0f%%)\n", bass * 100, random * 100);
}

// The downbeat should usually lead the pattern.
static void test_bass_downbeat() {
    int onDownbeat = 0, total = 0;
    forRows(STYLE_POS_BASS, [&](const Pattern& pat, int) {
        total++;
        if (pat.steps[0].weight == 1) onDownbeat++;
    });
    float frac = float(onDownbeat) / float(total);
    CHECK(frac > 0.7f && frac < 0.95f,
          "bass downbeat rate %.2f is outside the intended band", frac);
    printf("bass downbeat: %.0f%% of rolls put their first note on step 1\n", frac * 100);
}

// Acid vocabulary: root-dominated, nothing outside it, and expressed as
// intervals so it survives being pointed at a chord.
static void test_bass_pitch_vocabulary() {
    int root = 0, total = 0;
    forRows(STYLE_POS_BASS, [&](const Pattern& pat, int k) {
        for (int i = 0; i < MAX_STEPS; i++) {
            int r = pat.steps[i].acidRole;
            total++;
            if (r == ACID_ROOT) root++;
            CHECK(isAcidRole(r),
                  "roll %d step %d: role %d is outside the acid vocabulary",
                  k, i, r);
        }
    });
    float rootFrac = float(root) / float(total);
    CHECK(rootFrac > 0.45f && rootFrac < 0.65f,
          "root dominance %.2f is outside the intended band", rootFrac);
    printf("bass pitch: %.0f%% root, every note from the acid set "
           "(root/3rd/4th/5th/7th)\n", rootFrac * 100);
}

// THE CHORD FIX: a role must resolve to the musically right degree on every
// scale AND every chord — including triads, where a raw pitch index used to
// land "the fifth" on the chord's third.
static void test_acid_roles_resolve_on_every_scale() {
    const int roles[5] = { ACID_ROOT, ACID_THIRD, ACID_FOURTH,
                           ACID_FIFTH, ACID_SEVENTH };
    for (int sc = 1; sc < NUM_SCALES; sc++) {
        const Scale& scale = getScale(sc);
        for (int k = 0; k < 5; k++) {
            int role = roles[k];
            int d = nearestDegree(scale, role);
            CHECK(d >= 0 && d < scale.noteCount,
                  "scale %s: role %d gave degree %d", scale.name, role, d);
            int err = scale.intervals[d] - role;
            if (err < 0) err = -err;
            for (int i = 0; i < scale.noteCount; i++) {
                int e2 = scale.intervals[i] - role;
                if (e2 < 0) e2 = -e2;
                CHECK(e2 >= err, "scale %s: role %d picked %d st, %d st was closer",
                      scale.name, role, scale.intervals[d], scale.intervals[i]);
            }
        }
        CHECK(scale.intervals[nearestDegree(scale, ACID_ROOT)] == 0,
              "scale %s: the root role did not resolve to the root", scale.name);
    }
    // and specifically: a triad keeps its fifth
    const Scale& minTriad = getScale(NUM_SCALES - 5);
    CHECK(minTriad.noteCount == 3, "expected a triad at that position");
    CHECK(minTriad.intervals[nearestDegree(minTriad, ACID_FIFTH)] == 7,
          "a triad lost its fifth — the bug this fix exists for");
    CHECK(minTriad.intervals[nearestDegree(minTriad, ACID_THIRD)] == 3,
          "a minor triad did not resolve the third role to its minor third");
    printf("acid roles: resolve to the nearest real degree on all %d scales "
           "and chords, triads included\n", NUM_SCALES - 1);
}

// Octave-up jumps: present, rare, and never anything but +1.
static void test_bass_octave_jumps() {
    int up = 0, total = 0;
    forRows(STYLE_POS_BASS, [&](const Pattern& pat, int k) {
        for (int i = 0; i < MAX_STEPS; i++) {
            int j = pat.steps[i].octaveJump;
            CHECK(j == 0 || j == 1, "roll %d step %d octaveJump %d", k, i, j);
            if (j == 1) up++;
            total++;
        }
    });
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
    auto slideRate = [](int want) {
        int n = 0, total = 0;
        forRows(want, [&](const Pattern& pat, int) {
            for (int i = 0; i < MAX_STEPS; i++) { total++; if (pat.steps[i].slide) n++; }
        });
        return total ? float(n) / float(total) : 0.f;
    };
    float bass = slideRate(STYLE_POS_BASS), random = slideRate(STYLE_POS_RAND);
    CHECK(bass > random * 1.15f, "bass does not tie more than random (%.2f vs %.2f)",
          bass, random);
    printf("bass ties: %.0f%% of steps slide (RAND %.0f%%)\n",
           bass * 100, random * 100);
}

// Arp: every ARP row must follow its STYLE position's direction, in every
// roll.
static void test_arp_shapes() {
    int checked = 0;
    for (int pos = STYLE_POS_ARP_UP; pos <= STYLE_POS_ARP_DOWNUP; pos++) {
        const ArpMode mode = arpModeOf(pos);
        forRows(pos, [&](const Pattern& pat, int k) {
            const StepData* s = pat.steps;
            if (mode == ARP_UP) {
                for (int i = 1; i < MAX_STEPS; i++)
                    CHECK(s[i].pitchIndex >= s[i-1].pitchIndex,
                          "roll %d ARP_UP dips at step %d", k, i);
            } else if (mode == ARP_DOWN) {
                for (int i = 1; i < MAX_STEPS; i++)
                    CHECK(s[i].pitchIndex <= s[i-1].pitchIndex,
                          "roll %d ARP_DOWN rises at step %d", k, i);
            } else if (mode == ARP_UPDOWN) {
                for (int i = 1; i < MAX_STEPS / 2; i++)
                    CHECK(s[i].pitchIndex >= s[i-1].pitchIndex,
                          "roll %d ARP_UPDOWN first half dips at %d", k, i);
                for (int i = MAX_STEPS / 2 + 1; i < MAX_STEPS; i++)
                    CHECK(s[i].pitchIndex <= s[i-1].pitchIndex,
                          "roll %d ARP_UPDOWN second half rises at %d", k, i);
            } else {
                for (int i = 1; i < MAX_STEPS / 2; i++)
                    CHECK(s[i].pitchIndex <= s[i-1].pitchIndex,
                          "roll %d ARP_DOWNUP first half rises at %d", k, i);
                for (int i = MAX_STEPS / 2 + 1; i < MAX_STEPS; i++)
                    CHECK(s[i].pitchIndex >= s[i-1].pitchIndex,
                          "roll %d ARP_DOWNUP second half dips at %d", k, i);
            }
            // arps are even
            for (int i = 0; i < MAX_STEPS; i++)
                CHECK(s[i].gateIndex == 1, "roll %d step %d: arp gate is not consistent",
                      k, i);
            checked++;
        });
    }
    printf("arp shapes: %d ARP rows follow their direction, even gates\n", checked);
}

// Each style must still have its own gate character.
static void test_gate_character() {
    auto meanIndex = [](int want) {
        double sum = 0; int n = 0;
        forRows(want, [&](const Pattern& pat, int) {
            for (int i = 0; i < MAX_STEPS; i++) { sum += pat.steps[i].gateIndex; n++; }
        });
        return n ? float(sum / n) : 0.f;
    };
    float bass = meanIndex(STYLE_POS_BASS), random = meanIndex(STYLE_POS_RAND),
          arp  = meanIndex(STYLE_POS_ARP_UPDOWN);
    CHECK(bass < random, "bass gates are not shorter than random (%.2f vs %.2f)",
          bass, random);
    CHECK(std::fabs(arp - 1.f) < 1e-5f, "arp gates are not all the middle length");
    printf("gate character: mean length index — bass %.2f, random %.2f, arp %.2f\n",
           bass, random, arp);
}

// No two successive rolls collapse together in any style.
static void test_variety_across_rolls() {
    int identical = 0;
    for (int st = 0; st < NUM_STYLES; st++)
        for (int k = 0; k + 1 < NUM_ROLLS; k++) {
            Pattern a, b;
            patternFor(k, st, a);
            patternFor(k + 1, st, b);
            int same = 0;
            for (int i = 0; i < MAX_STEPS; i++)
                if (a.steps[i].pitchIndex == b.steps[i].pitchIndex
                 && a.steps[i].weight == b.steps[i].weight) same++;
            if (same >= 15) identical++;
        }
    CHECK(identical == 0,
          "%d successive rolls collapsed into near-identical rows", identical);
    printf("style: no two successive rolls collapse, in any row\n");
}

int main() {
    test_style_positions();
    test_style_preserves_permutation();
    test_bass_rhythm_varies_across_rolls();
    test_bass_runs_of_sixteenths();
    test_bass_downbeat();
    test_bass_pitch_vocabulary();
    test_acid_roles_resolve_on_every_scale();
    test_bass_octave_jumps();
    test_bass_ties();
    test_arp_shapes();
    test_gate_character();
    test_variety_across_rolls();

    if (failures) {
        printf("\n%d FAILURES\n", failures);
        return 1;
    }
    printf("\nall tests passed\n");
    return 0;
}
