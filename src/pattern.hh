#pragma once
#include "rng.hh"
#include <cstdint>

// Pure pattern generation — no Rack dependencies. Deterministic given the
// roll's entropy; the entropy itself is drawn only at Generate.
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
    float    swing;      // 0..1, rolled; scaled by the SWING knob
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

// ── Rolls ───────────────────────────────────────────────────────────────────
//
// Item 16, "rules replace seeds" (decided 2026-11-13, built 2026-11-16).
// There is no seed to dial or return to. Generate (the RESEED jack or the
// RANDOM button) ROLLS: it draws fresh randomness once, and from it fills one
// precomputed pattern per STYLE position. Turning STYLE is then instant, and
// returning to a position gives the same pattern until the next Generate.
// process() still only reads precomputed steps — nothing random at playback.
//
// Lanes draw independently within a roll: every attribute has its own
// generator, started from its own fresh 32-bit draw. That is no longer about
// protecting saved seeds (the whole roll is saved with the patch instead); it
// keeps the lanes statistically unrelated, and it means a change to how one
// lane draws never shifts the values another lane gets.

enum Lane {
    LANE_WEIGHT = 0,   // the one shuffled order DENSITY reads, per roll
    LANE_PITCH,
    LANE_GATELEN,      // the roll's three note lengths
    LANE_GATE,         // which length each step uses
    LANE_ACCENT,       // per-step velocity layer
    LANE_SLIDE,
    LANE_OCTAVE,
    LANE_SWING,
    LANE_BASS,         // the BASS row's shaping
    LANE_ARP,          // the four ARP rows' accents
    NUM_LANES
};

// The randomness a roll is made from. The adapter fills it from a
// non-deterministic source at Generate; the harness fills it from a test
// generator so that "many rolls" are reproducible in CI.
struct RollEntropy {
    uint32_t lane[NUM_LANES];
};

inline Xorshift32 laneRng(const RollEntropy& e, Lane l) {
    return Xorshift32(e.lane[l]);
}

// The base pattern, before any style shaping. It is the RAND row as is, and
// what the BASS and ARP rows are shaped from — so within one roll, switching
// STYLE between RAND and an ARP keeps the same notes, just reordered.
inline void generateBasePattern(const RollEntropy& e, Pattern& pat) {
    StepData* steps = pat.steps;

    // Weights: Fisher-Yates shuffle of 1..16 so density is exactly linear
    {
        auto rng = laneRng(e, LANE_WEIGHT);
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
        auto rng = laneRng(e, LANE_PITCH);
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].pitchIndex = rng.nextInt(16);
    }
    // The pattern's three note lengths, short to long, and which one each
    // step uses.
    {
        auto rng = laneRng(e, LANE_GATELEN);
        pat.gateLengths[0] = 0.12f + rng.nextFloat() * 0.18f;   // 0.12–0.30
        pat.gateLengths[1] = 0.35f + rng.nextFloat() * 0.25f;   // 0.35–0.60
        pat.gateLengths[2] = 0.65f + rng.nextFloat() * 0.30f;   // 0.65–0.95
    }
    {
        auto rng = laneRng(e, LANE_GATE);
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].gateIndex = rng.nextInt(NUM_GATE_LENGTHS);
    }
    {
        auto rng = laneRng(e, LANE_ACCENT);
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].velLayer = rng.nextInt(NUM_VEL_LAYERS);
    }
    {
        auto rng = laneRng(e, LANE_SLIDE);
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].slide = rng.nextFloat() < 0.25f;
    }
    {
        auto rng = laneRng(e, LANE_OCTAVE);
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].octaveRaw = rng.nextFloat();
    }
    for (int i = 0; i < MAX_STEPS; i++) {
        steps[i].octaveJump = -1;           // default: spread across the range
        steps[i].acidRole   = -1;           // only the bass row sets roles
    }
    {
        // Groove is part of a pattern's identity, not just a global setting.
        auto rng = laneRng(e, LANE_SWING);
        pat.swing = rng.nextFloat();
    }
}

// ── STYLE ───────────────────────────────────────────────────────────────────
//
// A six-position panel knob (decided 2026-11-16), replacing GROUP/SUBGROUP:
// BASS, RAND, then the four arp directions. Each position is one row of the
// roll. No CV — style is not something to modulate.

enum Style {
    STYLE_BASS   = 0,   // 303-style acid lines
    STYLE_RANDOM = 1,   // the untouched base pattern
    STYLE_ARP    = 2,   // ordered runs, direction from the STYLE position
};

enum ArpMode {
    ARP_UP = 0, ARP_DOWN = 1, ARP_UPDOWN = 2, ARP_DOWNUP = 3,
};

// The STYLE knob's positions. Saved patches store the index: append only.
enum StylePos {
    STYLE_POS_BASS = 0,
    STYLE_POS_RAND,
    STYLE_POS_ARP_UP,
    STYLE_POS_ARP_DOWN,
    STYLE_POS_ARP_UPDOWN,
    STYLE_POS_ARP_DOWNUP,
    NUM_STYLES
};

inline int clampStyle(int pos) { return clampi(pos, 0, NUM_STYLES - 1); }

inline Style styleOf(int pos) {
    pos = clampStyle(pos);
    if (pos == STYLE_POS_BASS) return STYLE_BASS;
    if (pos == STYLE_POS_RAND) return STYLE_RANDOM;
    return STYLE_ARP;
}

// Only meaningful for the ARP positions.
inline ArpMode arpModeOf(int pos) {
    switch (clampStyle(pos)) {
        case STYLE_POS_ARP_DOWN:    return ARP_DOWN;
        case STYLE_POS_ARP_UPDOWN:  return ARP_UPDOWN;
        case STYLE_POS_ARP_DOWNUP:  return ARP_DOWNUP;
        default:                    return ARP_UP;
    }
}

inline const char* styleName(int pos) {
    switch (clampStyle(pos)) {
        case STYLE_POS_BASS:        return "BASS";
        case STYLE_POS_RAND:        return "RAND";
        case STYLE_POS_ARP_UP:      return "ARP\u2191";
        case STYLE_POS_ARP_DOWN:    return "ARP\u2193";
        case STYLE_POS_ARP_UPDOWN:  return "ARP\u2195";
        default:                    return "ARP\u2194";
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
constexpr int   BASS_RUNS            = 4;      // runs of consecutive 16ths per roll
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

// The BASS row: the base pattern reshaped into a 303 line.
inline void shapeBass(Xorshift32& rng, Pattern& pat) {
    StepData* steps = pat.steps;
    {
        // ── Rhythm: runs of sixteenths, per roll ────────────────────────────
        // Build a priority order of step positions, then lay weights 1..16
        // along it. Because the run positions are rolled, two bass rows do
        // NOT share a rhythm — which a fixed beat-priority table would have
        // forced on every one of them (see docs/DESIGN.md § The bass zone).
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
        // DENSITY stays exact and nested.
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
    }
}

// An ARP row: the base pattern's pitches sorted, then read out in the
// direction the STYLE position names.
inline void shapeArp(Xorshift32& rng, ArpMode mode, Pattern& pat) {
    StepData* steps = pat.steps;
    int sorted[MAX_STEPS];
    for (int i = 0; i < MAX_STEPS; i++) sorted[i] = steps[i].pitchIndex;
    for (int a = 0; a < MAX_STEPS; a++)
        for (int b = a + 1; b < MAX_STEPS; b++)
            if (sorted[b] < sorted[a]) {
                const int t = sorted[a]; sorted[a] = sorted[b]; sorted[b] = t;
            }

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

// ── The roll: one row per STYLE position ────────────────────────────────────

struct Roll {
    Pattern rows[NUM_STYLES];
};

// Fill every row from one draw of entropy. The RAND row is the base pattern
// itself; BASS and the ARPs are shaped from it. Each ARP row draws its accents
// independently, so the four directions are not just reorderings of one
// another's dynamics.
inline void rollPatterns(const RollEntropy& e, Roll& roll) {
    Pattern base;
    generateBasePattern(e, base);

    roll.rows[STYLE_POS_RAND] = base;

    roll.rows[STYLE_POS_BASS] = base;
    {
        auto rng = laneRng(e, LANE_BASS);
        shapeBass(rng, roll.rows[STYLE_POS_BASS]);
    }

    auto arpRng = laneRng(e, LANE_ARP);
    for (int pos = STYLE_POS_ARP_UP; pos <= STYLE_POS_ARP_DOWNUP; pos++) {
        roll.rows[pos] = base;
        shapeArp(arpRng, arpModeOf(pos), roll.rows[pos]);
    }
}

} // namespace nzzl
