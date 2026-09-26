#pragma once
#include "rng.hh"
#include <cstdint>

// Pure pattern generation — no Rack dependencies, fully deterministic.
// Everything here is testable outside VCV Rack / MetaModule.

namespace nzzl {

constexpr int MAX_STEPS = 16;

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Three note lengths and three velocity layers per pattern, not a continuous
// value per step — the GATE and ACCENT knobs then scale a small, audible set
// rather than smearing sixteen unrelated numbers.
constexpr int NUM_GATE_LENGTHS = 3;
constexpr int NUM_VEL_LAYERS   = 3;   // 0 = off, 1 = low, 2 = high (accent)

struct StepData {
    int   weight;       // permutation of 1–16: density N activates exactly N steps
    int   pitchIndex;   // raw index 0–15, mapped into scale at playback
    int   gateIndex;    // 0–2: which of the pattern's three note lengths
    int   velLayer;     // 0–2: off / low / high, scaled by ACCENT at playback
    bool  slide;        // 303-style tie: gate holds through to the next note
    int   octaveJump;   // >= 0 = explicit octave offset; -1 = use octaveRaw
    float octaveRaw;    // 0.0–1.0, spread across octave range at playback
    // The acid vocabulary as a musical INTERVAL IN SEMITONES (0 root, 3
    // third, 5 fourth, 7 fifth, 10 seventh) rather than a raw pitch index,
    // resolved against the actual scale at playback. -1 = use pitchIndex.
    // Without this, "the fifth" means a different degree on a triad than on
    // a seven-note scale, and chord positions lose the fifth entirely.
    int   acidRole;
};

// Is this step switched on at the given DENSITY?
//
// The weight permutation runs across all sixteen steps, but the loop only
// plays the first `length` of them. Comparing weight to density directly
// means raising DENSITY often switches on a step outside the loop and
// nothing is heard — at LENGTH 8 that was about half of the knob's travel.
// Ranking the weights WITHIN the window keeps every click audible at any
// length, and is identical to the old behaviour at LENGTH 16.
struct Pattern;
inline bool stepActive(const Pattern& pat, int step, int length, int density,
                       int shift = 0);

// One pattern: the sixteen steps plus the three note lengths they index into.
struct Pattern {
    StepData steps[MAX_STEPS];
    float    gateLengths[NUM_GATE_LENGTHS];   // fractions of a clock period
    float    swing;      // 0..1, seed-derived; scaled by the SWING knob
};

inline bool stepActive(const Pattern& pat, int step, int length, int density,
                       int shift) {
    length  = clampi(length, 2, MAX_STEPS);
    density = clampi(density, 1, length);
    if (step < 0 || step >= length)
        return false;
    auto rot = [shift](int i) {
        return ((i + shift) % MAX_STEPS + MAX_STEPS) % MAX_STEPS;
    };
    const int w = pat.steps[rot(step)].weight;
    int rank = 1;                              // 1 = quietest-weighted step
    for (int i = 0; i < length; i++)
        if (i != step && pat.steps[rot(i)].weight < w)
            rank++;
    return rank <= density;
}

// Velocity for a layer, with ACCENT (0–100) scaling the contrast between
// layers. At 0 every note is the same mid level; at 100 the layers are fully
// apart. This is what makes a 303 line breathe.
constexpr float VEL_LAYER[NUM_VEL_LAYERS] = { 0.22f, 0.55f, 1.0f };
constexpr float VEL_FLAT = 0.55f;

inline float velocityFor(int layer, float accent01) {
    const int l = clampi(layer, 0, NUM_VEL_LAYERS - 1);
    const float a = clampf(accent01, 0.f, 1.f);
    return VEL_FLAT + (VEL_LAYER[l] - VEL_FLAT) * a;
}

// splitmix32 — decorrelates sequential seed indices before they enter
// xorshift, so adjacent seeds produce unrelated patterns.
inline uint32_t hashSeed(uint32_t x) {
    x += 0x9E3779B9u;
    x ^= x >> 16;
    x *= 0x21F0AAADu;
    x ^= x >> 15;
    x *= 0x735A2D97u;
    x ^= x >> 15;
    return x ? x : 1;
}

// Each attribute gets its own RNG stream so future changes to one
// attribute's generation never reshuffle the others (patch stability
// across plugin versions).
enum AttrSalt : uint32_t {
    SALT_WEIGHT   = 0x57454948,
    SALT_PITCH    = 0x50495443,
    SALT_GATE     = 0x47415445,
    SALT_VELOCITY = 0x56454C4F,
    SALT_SLIDE    = 0x534C4944,
    SALT_OCTAVE   = 0x4F435456,
    SALT_STYLE    = 0x5354594C,   // Task 8 — new attribute, new salt
    SALT_GATELEN  = 0x474C454E,   // the pattern's three note lengths
    SALT_ACCENT   = 0x41434354,   // per-step velocity layer
    SALT_OCTJUMP  = 0x4F4A4D50,   // per-step octave jump
    SALT_SWING    = 0x53574E47,   // per-pattern swing amount
};

inline Xorshift32 attrRng(int seedIndex, uint32_t salt) {
    return Xorshift32(hashSeed(uint32_t(seedIndex) ^ salt));
}

// The base pattern, before any style shaping. Exposed so tests can prove
// the STYLE_RANDOM zone is bit-identical to it.
inline void generateBasePattern(int seedIndex, Pattern& pat) {
    StepData* steps = pat.steps;

    // Weights: Fisher-Yates shuffle of 1..16 so density is exactly linear
    {
        auto rng = attrRng(seedIndex, SALT_WEIGHT);
        int order[MAX_STEPS];
        for (int i = 0; i < MAX_STEPS; i++)
            order[i] = i + 1;
        for (int i = MAX_STEPS - 1; i > 0; i--) {
            int j = rng.nextInt(i + 1);
            int tmp = order[i];
            order[i] = order[j];
            order[j] = tmp;
        }
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].weight = order[i];
    }

    {
        auto rng = attrRng(seedIndex, SALT_PITCH);
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].pitchIndex = rng.nextInt(16);
    }
    // The pattern's three note lengths, short to long, and which one each
    // step uses.
    {
        auto rng = attrRng(seedIndex, SALT_GATELEN);
        pat.gateLengths[0] = 0.12f + rng.nextFloat() * 0.18f;   // 0.12–0.30
        pat.gateLengths[1] = 0.35f + rng.nextFloat() * 0.25f;   // 0.35–0.60
        pat.gateLengths[2] = 0.65f + rng.nextFloat() * 0.30f;   // 0.65–0.95
    }
    {
        auto rng = attrRng(seedIndex, SALT_GATE);
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].gateIndex = rng.nextInt(NUM_GATE_LENGTHS);
    }
    {
        auto rng = attrRng(seedIndex, SALT_ACCENT);
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].velLayer = rng.nextInt(NUM_VEL_LAYERS);
    }
    {
        auto rng = attrRng(seedIndex, SALT_SLIDE);
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].slide = rng.nextFloat() < 0.25f;
    }
    {
        auto rng = attrRng(seedIndex, SALT_OCTAVE);
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].octaveRaw = rng.nextFloat();
    }
    {
        auto rng = attrRng(seedIndex, SALT_OCTJUMP);
        for (int i = 0; i < MAX_STEPS; i++) {
            (void)rng.nextFloat();          // reserved; styles overwrite this
            steps[i].octaveJump = -1;       // default: spread across the range
        }
    }
    for (int i = 0; i < MAX_STEPS; i++)
        steps[i].acidRole = -1;             // only the bass zone sets roles
    {
        // Groove is part of a pattern's identity, not just a global setting.
        auto rng = attrRng(seedIndex, SALT_SWING);
        pat.swing = rng.nextFloat();
    }
}

// ── Style zones ─────────────────────────────────────────────────────────────
//
// The GROUP knob divides the 1024 seeds into three zones, per the handoff
// spec: 1–10 BASS, 11–21 RAND, 22–32 ARP. Inside the ARP zone the SUBGROUP
// knob picks the direction: 1–8 up, 9–16 down, 17–24 up-down, 25–32 down-up.
//
// The style layer runs AFTER the base pattern, from its own salted stream,
// and STYLE_RANDOM is a deliberate no-op — new needs get new salts, existing
// draws never change, and the middle zone stays exactly what the unshaped
// generator produced.

enum Style {
    STYLE_BASS   = 0,   // GROUP 1–10:  303-style acid lines
    STYLE_RANDOM = 1,   // GROUP 11–21: the untouched base pattern
    STYLE_ARP    = 2,   // GROUP 22–32: ordered runs, direction from SUBGROUP
};

enum ArpMode {
    ARP_UP = 0, ARP_DOWN = 1, ARP_UPDOWN = 2, ARP_DOWNUP = 3,
};

constexpr int SEEDS_PER_GROUP = 32;
constexpr int NUM_GROUPS      = 32;
constexpr int NUM_SEEDS       = SEEDS_PER_GROUP * NUM_GROUPS;   // 1024

inline int groupForSeed(int seedIndex) {
    return seedIndex / SEEDS_PER_GROUP + 1;     // 1..32
}
inline int subgroupForSeed(int seedIndex) {
    return seedIndex % SEEDS_PER_GROUP + 1;     // 1..32
}

inline Style styleForSeed(int seedIndex) {
    const int g = groupForSeed(seedIndex);
    if (g <= 10) return STYLE_BASS;
    if (g <= 21) return STYLE_RANDOM;
    return STYLE_ARP;
}

// Only meaningful in the ARP zone.
inline ArpMode arpModeForSeed(int seedIndex) {
    const int sg = subgroupForSeed(seedIndex);          // 1..32
    if (sg <= 8)  return ARP_UP;
    if (sg <= 16) return ARP_DOWN;
    if (sg <= 24) return ARP_UPDOWN;
    return ARP_DOWNUP;
}

// Knobs <-> seed index. RESEED needs the reverse direction so it can drive
// the knobs to match the pattern it just picked — if the knobs stopped
// agreeing with what is playing, the seed would no longer be reproducible by
// hand, which is the whole promise of the module.
inline int seedIndexFor(int group, int subgroup) {
    group    = clampi(group,    1, NUM_GROUPS);
    subgroup = clampi(subgroup, 1, SEEDS_PER_GROUP);
    return (group - 1) * SEEDS_PER_GROUP + (subgroup - 1);
}

inline void seedKnobsFor(int seedIndex, int& group, int& subgroup) {
    seedIndex = clampi(seedIndex, 0, NUM_SEEDS - 1);
    group     = seedIndex / SEEDS_PER_GROUP + 1;
    subgroup  = seedIndex % SEEDS_PER_GROUP + 1;
}

inline const char* styleName(int seedIndex) {
    switch (styleForSeed(seedIndex)) {
        case STYLE_BASS: return "BASS";
        case STYLE_ARP:
            switch (arpModeForSeed(seedIndex)) {
                case ARP_UP:     return "ARP\u2191";
                case ARP_DOWN:   return "ARP\u2193";
                case ARP_UPDOWN: return "ARP\u2195";
                default:         return "ARP\u2194";
            }
        default: return "RAND";
    }
}

// ── 303 shaping constants ───────────────────────────────────────────────────
//
// A TB-303 line is not "notes on beats 1 and 3". It is runs of sixteenths
// broken by rests, with ties (slides) welding notes together, the root
// hammered far more than anything else, and the odd octave jump. The handoff
// doc's "cluster on beats 1 and 3, sparse off-beat" describes a dub bassline;
// the module wants acid, so this is what got built instead.

constexpr float BASS_DOWNBEAT_CHANCE = 0.85f;  // how often step 1 leads the pattern
constexpr int   BASS_RUNS            = 4;      // runs of consecutive 16ths per seed
constexpr int   BASS_RUN_MIN         = 2;
constexpr int   BASS_RUN_SPAN        = 3;      // run length = MIN .. MIN+SPAN-1
constexpr float BASS_SLIDE_CHANCE    = 0.32f;  // ties are common in acid
constexpr float BASS_ACCENT_CHANCE   = 0.28f;  // accented (high-layer) notes
constexpr float BASS_OCTAVE_UP       = 0.18f;  // the 303's octave-up button

// The acid vocabulary, as INTERVALS IN SEMITONES. Stored this way, not as
// pitch indices, so each one can be resolved against whatever scale or chord
// is selected — see nearestDegree() in scales.hh.
constexpr int ACID_ROOT    = 0;
constexpr int ACID_THIRD   = 3;
constexpr int ACID_FOURTH  = 5;
constexpr int ACID_FIFTH   = 7;
constexpr int ACID_SEVENTH = 10;

// Cumulative weights: root 55%, fifth 15%, third 12%, seventh 10%, fourth 8%.
// The root dominance is what gives an acid line its hypnotic repetition.
inline int acidRole(float r) {
    if (r < 0.55f) return ACID_ROOT;
    if (r < 0.70f) return ACID_FIFTH;
    if (r < 0.82f) return ACID_THIRD;
    if (r < 0.92f) return ACID_SEVENTH;
    return ACID_FOURTH;
}

inline void applyStyle(int seedIndex, Pattern& pat) {
    const Style style = styleForSeed(seedIndex);
    if (style == STYLE_RANDOM)
        return;                              // no-op, and no stream consumed

    StepData* steps = pat.steps;
    auto rng = attrRng(seedIndex, SALT_STYLE);

    if (style == STYLE_BASS) {
        // ── Rhythm: runs of sixteenths, per seed ────────────────────────────
        // Build a priority order of step positions, then lay weights 1..16
        // along it. Because the run positions come from the seed, two bass
        // seeds do NOT share a rhythm — which a fixed beat-priority table
        // would have forced on all 320 of them.
        int  pri[MAX_STEPS];
        bool used[MAX_STEPS] = {};
        int  n = 0;

        if (rng.nextFloat() < BASS_DOWNBEAT_CHANCE) {
            pri[n++] = 0; used[0] = true;    // acid lines usually land on 1
        }
        for (int r = 0; r < BASS_RUNS && n < MAX_STEPS; r++) {
            const int start = rng.nextInt(MAX_STEPS);
            const int len   = BASS_RUN_MIN + rng.nextInt(BASS_RUN_SPAN);
            for (int k = 0; k < len && n < MAX_STEPS; k++) {
                const int idx = (start + k) % MAX_STEPS;
                if (!used[idx]) { pri[n++] = idx; used[idx] = true; }
            }
        }
        for (int i = 0; i < MAX_STEPS && n < MAX_STEPS; i++)
            if (!used[i]) { pri[n++] = i; used[i] = true; }

        // Laying 1..16 along a permutation of positions IS a permutation, so
        // DENSITY stays exactly linear (invariant 3).
        for (int k = 0; k < MAX_STEPS; k++)
            steps[pri[k]].weight = k + 1;

        // ── Pitch: root-dominated acid vocabulary ───────────────────────────
        for (int i = 0; i < MAX_STEPS; i++) {
            steps[i].acidRole = acidRole(rng.nextFloat());
            // Fallback index for unquantized mode, which has no degrees.
            steps[i].pitchIndex = (steps[i].acidRole * MAX_STEPS) / 12;
        }

        // ── Octave: mostly home, occasional octave-up ───────────────────────
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].octaveJump = (rng.nextFloat() < BASS_OCTAVE_UP) ? 1 : 0;

        // ── Ties and accents ────────────────────────────────────────────────
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].slide = rng.nextFloat() < BASS_SLIDE_CHANCE;
        for (int i = 0; i < MAX_STEPS; i++) {
            const float r = rng.nextFloat();
            steps[i].velLayer = (r < BASS_ACCENT_CHANCE) ? 2 : (r < 0.80f ? 1 : 0);
        }
        // Gates: short and medium dominate — staccato, with ties doing the
        // work that long gates would otherwise do.
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].gateIndex = rng.nextFloat() < 0.65f ? 0 : 1;
        return;
    }

    // ── STYLE_ARP ───────────────────────────────────────────────────────────
    // Direction comes from the SUBGROUP knob, per the spec. Sort the whole
    // sixteen-step pitch set, then read it out in the chosen shape.
    int sorted[MAX_STEPS];
    for (int i = 0; i < MAX_STEPS; i++) sorted[i] = steps[i].pitchIndex;
    for (int a = 0; a < MAX_STEPS; a++)
        for (int b = a + 1; b < MAX_STEPS; b++)
            if (sorted[b] < sorted[a]) {
                const int t = sorted[a]; sorted[a] = sorted[b]; sorted[b] = t;
            }

    const ArpMode mode = arpModeForSeed(seedIndex);
    for (int i = 0; i < MAX_STEPS; i++) {
        int pick;
        switch (mode) {
            case ARP_UP:   pick = i; break;
            case ARP_DOWN: pick = MAX_STEPS - 1 - i; break;
            case ARP_UPDOWN: {
                // up over 8, then back down over 8
                const int half = MAX_STEPS / 2;
                pick = (i < half) ? i * 2 : (MAX_STEPS - 1) - (i - half) * 2;
                break;
            }
            default: {  // ARP_DOWNUP
                const int half = MAX_STEPS / 2;
                pick = (i < half) ? (MAX_STEPS - 1) - i * 2 : (i - half) * 2;
                break;
            }
        }
        steps[i].pitchIndex = sorted[clampi(pick, 0, MAX_STEPS - 1)];
    }
    // Even subdivisions and consistent gates — the arp feel.
    for (int i = 0; i < MAX_STEPS; i++) {
        steps[i].gateIndex  = 1;                    // one length for all
        steps[i].octaveJump = -1;                   // spread across the range
        // Arps keep the base pattern's slide flags. Silencing them was an
        // earlier choice of mine, not something the spec asked for, and the
        // glides were missed in testing.
        steps[i].velLayer   = rng.nextFloat() < 0.25f ? 2 : 1;
    }
}

inline void generatePattern(int seedIndex, Pattern& pat) {
    generateBasePattern(seedIndex, pat);
    applyStyle(seedIndex, pat);
}

} // namespace nzzl
