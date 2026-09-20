// Native test harness for NZZL's style zones (Task 8) — no Rack deps.
// Style is a statistical effect, so these are aggregate assertions over whole
// zones, not per-seed checks.

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

// NUM_SEEDS (1024) comes from pattern.hh.

static bool isStrongBeat(int step) {
    return step == 0 || step == 4 || step == 8 || step == 12;
}
static bool isAnchorPitch(int idx) {
    for (int i = 0; i < 5; i++)
        if (BASS_ANCHOR[i] == idx) return true;
    return false;
}

// Zone boundaries: GROUP 1–10 bass, 11–22 random, 23–32 arp.
static void test_style_zones() {
    int counts[3] = {};
    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        int g = groupForSeed(seed);
        CHECK(g >= 1 && g <= 32, "seed %d gave group %d", seed, g);
        Style st = styleForSeed(seed);
        Style expected = (g <= 10) ? STYLE_BASS
                       : (g <= 22) ? STYLE_RANDOM
                                   : STYLE_ARP;
        CHECK(st == expected, "seed %d (group %d) got style %d, expected %d",
              seed, g, int(st), int(expected));
        counts[int(st)]++;
        CHECK(styleName(st)[0] != '\0', "style %d has no name", int(st));
    }
    // exact boundaries — an off-by-one here silently moves a whole zone
    CHECK(styleForSeed(0) == STYLE_BASS,           "group 1 is not BASS");
    CHECK(styleForSeed(10 * 32 - 1) == STYLE_BASS, "group 10 is not BASS");
    CHECK(styleForSeed(10 * 32) == STYLE_RANDOM,   "group 11 is not RAND");
    CHECK(styleForSeed(22 * 32 - 1) == STYLE_RANDOM, "group 22 is not RAND");
    CHECK(styleForSeed(22 * 32) == STYLE_ARP,      "group 23 is not ARP");
    CHECK(styleForSeed(NUM_SEEDS - 1) == STYLE_ARP, "group 32 is not ARP");
    CHECK(counts[STYLE_BASS] == 320 && counts[STYLE_RANDOM] == 384
       && counts[STYLE_ARP] == 320,
          "zone sizes wrong: %d / %d / %d",
          counts[STYLE_BASS], counts[STYLE_RANDOM], counts[STYLE_ARP]);
    printf("style zones: %d BASS / %d RAND / %d ARP, boundaries exact\n",
           counts[STYLE_BASS], counts[STYLE_RANDOM], counts[STYLE_ARP]);
}

// THE PATCH-STABILITY PROOF: the random zone must be byte-for-byte what the
// module produced before Task 8 existed. Style must be purely additive.
static void test_random_zone_unchanged() {
    int checked = 0;
    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        if (styleForSeed(seed) != STYLE_RANDOM) continue;
        StepData styled[MAX_STEPS], base[MAX_STEPS];
        generatePattern(seed, styled);
        generateBasePattern(seed, base);
        for (int i = 0; i < MAX_STEPS; i++) {
            CHECK(styled[i].weight     == base[i].weight
               && styled[i].pitchIndex == base[i].pitchIndex
               && styled[i].gateLength == base[i].gateLength
               && styled[i].velocity   == base[i].velocity
               && styled[i].slide      == base[i].slide
               && styled[i].octaveRaw  == base[i].octaveRaw,
                  "seed %d step %d: style layer altered the RANDOM zone",
                  seed, i);
        }
        checked++;
    }
    printf("random zone: %d seeds bit-identical to the pre-style generator\n",
           checked);
}

// Style must never break the weight permutation — DENSITY exactness depends
// on it (invariant 3), and the bass zone actively reorders weights.
static void test_style_preserves_permutation() {
    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        StepData s[MAX_STEPS];
        generatePattern(seed, s);
        bool seen[MAX_STEPS + 1] = {};
        for (int i = 0; i < MAX_STEPS; i++) {
            CHECK(s[i].weight >= 1 && s[i].weight <= MAX_STEPS,
                  "seed %d weight %d out of range", seed, s[i].weight);
            CHECK(!seen[s[i].weight], "seed %d duplicate weight %d after style",
                  seed, s[i].weight);
            seen[s[i].weight] = true;
        }
    }
    printf("style: weights remain a permutation of 1..16 in every zone\n");
}

// Bass zone: the first steps to appear as DENSITY rises should be on the beat.
static void test_bass_beat_bias() {
    auto strongFraction = [](Style want) {
        int strong = 0, total = 0;
        for (int seed = 0; seed < NUM_SEEDS; seed++) {
            if (styleForSeed(seed) != want) continue;
            StepData s[MAX_STEPS];
            generatePattern(seed, s);
            for (int i = 0; i < MAX_STEPS; i++) {
                if (s[i].weight <= 4) {          // what DENSITY 4 plays
                    total++;
                    if (isStrongBeat(i)) strong++;
                }
            }
        }
        return total ? float(strong) / float(total) : 0.f;
    };

    float bass   = strongFraction(STYLE_BASS);
    float random = strongFraction(STYLE_RANDOM);
    // Chance level is 4 of 16 = 0.25; the random zone should sit near it.
    CHECK(random > 0.18f && random < 0.33f,
          "random zone is not near chance on strong beats (%.2f)", random);
    CHECK(bass > 0.6f,
          "bass zone only puts %.0f%% of its first notes on the beat", bass * 100);
    CHECK(bass > random * 2.f,
          "bass beat bias too weak: %.2f vs %.2f", bass, random);
    printf("bass rhythm: %.0f%% of DENSITY-4 notes land on beats 1/2/3/4 "
           "(random zone %.0f%%, chance 25%%)\n", bass * 100, random * 100);
}

// Bass zone: pitches should cluster on the root and the fifth.
static void test_bass_pitch_anchor() {
    auto anchorFraction = [](Style want) {
        int anchored = 0, total = 0;
        for (int seed = 0; seed < NUM_SEEDS; seed++) {
            if (styleForSeed(seed) != want) continue;
            StepData s[MAX_STEPS];
            generatePattern(seed, s);
            for (int i = 0; i < MAX_STEPS; i++) {
                total++;
                if (isAnchorPitch(s[i].pitchIndex)) anchored++;
            }
        }
        return total ? float(anchored) / float(total) : 0.f;
    };

    float bass   = anchorFraction(STYLE_BASS);
    float random = anchorFraction(STYLE_RANDOM);
    CHECK(random > 0.2f && random < 0.42f,
          "random zone is not near chance on anchor pitches (%.2f)", random);
    CHECK(bass > random * 1.5f,
          "bass pitch anchoring too weak: %.2f vs %.2f", bass, random);
    printf("bass pitch: %.0f%% of notes on root/fifth (random zone %.0f%%, "
           "chance 31%%)\n", bass * 100, random * 100);
}

// Bass zone in degree terms: notes should favour scale degree 0 and 4.
static void test_bass_favours_root_and_fifth() {
    auto degreeFraction = [](Style want) {
        int hits = 0, total = 0;
        for (int seed = 0; seed < NUM_SEEDS; seed++) {
            if (styleForSeed(seed) != want) continue;
            StepData s[MAX_STEPS];
            generatePattern(seed, s);
            for (int i = 0; i < MAX_STEPS; i++) {
                int d = degreeForIndex(s[i].pitchIndex, 7);   // a 7-note scale
                total++;
                if (d == 0 || d == 4) hits++;
            }
        }
        return total ? float(hits) / float(total) : 0.f;
    };
    float bass   = degreeFraction(STYLE_BASS);
    float random = degreeFraction(STYLE_RANDOM);
    CHECK(bass > random * 1.4f,
          "bass does not favour degrees 0/4 enough: %.2f vs %.2f", bass, random);
    printf("bass degrees: %.0f%% of notes are the root or the fifth "
           "(random zone %.0f%%)\n", bass * 100, random * 100);
}

// Arp zone: each block of four steps must be a monotonic run.
static void test_arp_stepwise() {
    auto monotonicBlocks = [](Style want) {
        int mono = 0, total = 0;
        for (int seed = 0; seed < NUM_SEEDS; seed++) {
            if (styleForSeed(seed) != want) continue;
            StepData s[MAX_STEPS];
            generatePattern(seed, s);
            for (int b = 0; b < MAX_STEPS; b += 4) {
                bool up = true, down = true;
                for (int k = 1; k < 4; k++) {
                    if (s[b + k].pitchIndex < s[b + k - 1].pitchIndex) up = false;
                    if (s[b + k].pitchIndex > s[b + k - 1].pitchIndex) down = false;
                }
                total++;
                if (up || down) mono++;
            }
        }
        return total ? float(mono) / float(total) : 0.f;
    };

    float arp    = monotonicBlocks(STYLE_ARP);
    float random = monotonicBlocks(STYLE_RANDOM);
    CHECK(arp > 0.999f,
          "arp blocks are not all monotonic runs (%.3f)", arp);
    CHECK(random < 0.5f,
          "random zone is suspiciously arpeggiated (%.2f)", random);
    printf("arp shape: %.0f%% of 4-step blocks are runs (random zone %.0f%%)\n",
           arp * 100, random * 100);
}

// Every zone must have its own gate-length character.
static void test_gate_distributions_differ() {
    auto stats = [](Style want, float& mean, float& lo, float& hi) {
        double sum = 0; int n = 0;
        lo = 1e9f; hi = -1e9f;
        for (int seed = 0; seed < NUM_SEEDS; seed++) {
            if (styleForSeed(seed) != want) continue;
            StepData s[MAX_STEPS];
            generatePattern(seed, s);
            for (int i = 0; i < MAX_STEPS; i++) {
                float g = s[i].gateLength;
                sum += g; n++;
                if (g < lo) lo = g;
                if (g > hi) hi = g;
            }
        }
        mean = n ? float(sum / n) : 0.f;
    };

    float bm, bl, bh, rm, rl, rh, am, al, ah;
    stats(STYLE_BASS,   bm, bl, bh);
    stats(STYLE_RANDOM, rm, rl, rh);
    stats(STYLE_ARP,    am, al, ah);

    CHECK(bm < rm - 0.05f, "bass gates are not shorter than random (%.2f vs %.2f)", bm, rm);
    CHECK(am < rm - 0.05f, "arp gates are not shorter than random (%.2f vs %.2f)", am, rm);
    CHECK(std::fabs(bm - am) > 0.02f,
          "bass and arp gate means are indistinguishable (%.3f vs %.3f)", bm, am);
    CHECK((ah - al) < (rh - rl) * 0.6f,
          "arp gates are not more even than random (%.2f vs %.2f)", ah - al, rh - rl);
    // every zone must still be inside the module's documented gate range
    CHECK(bl >= 0.1f && bh <= 0.9f, "bass gates out of range %.2f–%.2f", bl, bh);
    CHECK(al >= 0.1f && ah <= 0.9f, "arp gates out of range %.2f–%.2f", al, ah);
    printf("gate character: bass %.2f (%.2f–%.2f), random %.2f (%.2f–%.2f), "
           "arp %.2f (%.2f–%.2f)\n", bm, bl, bh, rm, rl, rh, am, al, ah);
}

// Style must not reintroduce nondeterminism, and must not make neighbouring
// seeds within a zone collapse into each other.
static void test_style_determinism_and_variety() {
    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        StepData a[MAX_STEPS], b[MAX_STEPS];
        generatePattern(seed, a);
        generatePattern(seed, b);
        for (int i = 0; i < MAX_STEPS; i++)
            CHECK(a[i].weight == b[i].weight && a[i].pitchIndex == b[i].pitchIndex
               && a[i].gateLength == b[i].gateLength,
                  "seed %d step %d not deterministic after style", seed, i);
    }
    int identical = 0;
    for (int seed = 0; seed < NUM_SEEDS - 1; seed++) {
        if (styleForSeed(seed) != styleForSeed(seed + 1)) continue;
        StepData a[MAX_STEPS], b[MAX_STEPS];
        generatePattern(seed, a);
        generatePattern(seed + 1, b);
        int same = 0;
        for (int i = 0; i < MAX_STEPS; i++)
            if (a[i].pitchIndex == b[i].pitchIndex && a[i].weight == b[i].weight)
                same++;
        if (same >= 14) identical++;
    }
    CHECK(identical == 0,
          "%d neighbouring seeds collapsed into near-identical patterns", identical);
    printf("style: deterministic, and no two neighbouring seeds collapse\n");
}

int main() {
    test_style_zones();
    test_random_zone_unchanged();
    test_style_preserves_permutation();
    test_bass_beat_bias();
    test_bass_pitch_anchor();
    test_bass_favours_root_and_fifth();
    test_arp_stepwise();
    test_gate_distributions_differ();
    test_style_determinism_and_variety();

    if (failures) {
        printf("\n%d FAILURES\n", failures);
        return 1;
    }
    printf("\nall tests passed\n");
    return 0;
}
