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

struct StepData {
    int   weight;       // permutation of 1–16: density N activates exactly N steps
    int   pitchIndex;   // raw index 0–15, mapped into scale at playback
    float gateLength;   // fraction of clock period (0.1–0.9)
    float velocity;     // 0.0–1.0
    bool  slide;
    float octaveRaw;    // 0.0–1.0, scaled by octave range at playback
};

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
};

inline Xorshift32 attrRng(int seedIndex, uint32_t salt) {
    return Xorshift32(hashSeed(uint32_t(seedIndex) ^ salt));
}

// The base pattern, before any style shaping. Exposed so tests can prove
// the STYLE_RANDOM zone is bit-identical to it.
inline void generateBasePattern(int seedIndex, StepData steps[MAX_STEPS]) {
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
    {
        auto rng = attrRng(seedIndex, SALT_GATE);
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].gateLength = 0.1f + rng.nextFloat() * 0.8f;
    }
    {
        auto rng = attrRng(seedIndex, SALT_VELOCITY);
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].velocity = rng.nextFloat();
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
}


// ── Style zones (Task 8) ─────────────────────────────────────────────────────
//
// The GROUP knob divides the 1024 seeds into three zones with different
// musical character. The style layer is applied AFTER the base pattern, from
// its own salted stream, and STYLE_RANDOM is a deliberate no-op — so every
// existing attribute stream is untouched and the middle zone is bit-identical
// to the pre-Task-8 module. That is what invariant 2 in docs/DESIGN.md
// requires: new needs get new salts, existing draws never change.

enum Style {
    STYLE_BASS   = 0,   // GROUP 1–10:  root/fifth heavy, notes on strong beats
    STYLE_RANDOM = 1,   // GROUP 11–22: the untouched base pattern
    STYLE_ARP    = 2,   // GROUP 23–32: stepwise runs, even gates
};

constexpr int SEEDS_PER_GROUP = 32;
constexpr int NUM_GROUPS       = 32;
constexpr int NUM_SEEDS        = SEEDS_PER_GROUP * NUM_GROUPS;   // 1024

inline int groupForSeed(int seedIndex) {
    return seedIndex / SEEDS_PER_GROUP + 1;     // 1..32
}

// Knobs <-> seed index. RESEED (Task 9) needs the reverse direction so it can
// drive the knobs to match the pattern it just picked — if the knobs stopped
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

inline Style styleForSeed(int seedIndex) {
    const int g = groupForSeed(seedIndex);
    if (g <= 10) return STYLE_BASS;
    if (g <= 22) return STYLE_RANDOM;
    return STYLE_ARP;
}

inline const char* styleName(Style s) {
    switch (s) {
        case STYLE_BASS: return "BASS";
        case STYLE_ARP:  return "ARP";
        default:         return "RAND";
    }
}

// Strong-to-weak beat order in a 16-step bar: beats 1 and 3 first, then 2
// and 4, then the off-beats, then the odd sixteenths.
constexpr int BEAT_PRIORITY[MAX_STEPS] = {
    0, 8, 4, 12, 2, 6, 10, 14, 1, 3, 5, 7, 9, 11, 13, 15
};

// pitchIndex values that land on the root or the fifth of a 7-note scale
// (degreeForIndex(idx, 7) == 0 or 4). Used to give basslines their anchor.
constexpr int BASS_ANCHOR[5] = { 0, 1, 2, 10, 11 };

constexpr float BASS_BEAT_BIAS   = 0.75f;   // chance a low weight is pulled onto a strong beat
constexpr float BASS_PITCH_BIAS  = 0.55f;   // chance a step snaps to root/fifth
constexpr int   BASS_BIASED_BEATS = 8;      // how many of the lowest weights get placed

inline void applyStyle(int seedIndex, StepData steps[MAX_STEPS]) {
    const Style style = styleForSeed(seedIndex);
    if (style == STYLE_RANDOM)
        return;                              // no-op, and no stream consumed

    auto rng = attrRng(seedIndex, SALT_STYLE);

    if (style == STYLE_BASS) {
        // Rhythm: pull the lowest weights onto the strongest beats, so the
        // first steps to appear as DENSITY rises are the ones on the beat.
        // Swapping two positions' weights keeps the 1..16 permutation intact,
        // which is what makes DENSITY exact (invariant 3).
        for (int k = 0; k < BASS_BIASED_BEATS; k++) {
            if (rng.nextFloat() >= BASS_BEAT_BIAS)
                continue;
            const int want = k + 1;
            int at = -1;
            for (int i = 0; i < MAX_STEPS; i++)
                if (steps[i].weight == want) { at = i; break; }
            const int dst = BEAT_PRIORITY[k];
            if (at >= 0 && at != dst) {
                const int tmp = steps[at].weight;
                steps[at].weight  = steps[dst].weight;
                steps[dst].weight = tmp;
            }
        }
        // Pitch: anchor a good share of steps on the root or the fifth.
        for (int i = 0; i < MAX_STEPS; i++) {
            if (rng.nextFloat() < BASS_PITCH_BIAS)
                steps[i].pitchIndex = BASS_ANCHOR[rng.nextInt(5)];
        }
        // Gate: punchier. Compresses 0.1–0.9 into 0.1–0.58.
        for (int i = 0; i < MAX_STEPS; i++)
            steps[i].gateLength = 0.1f + (steps[i].gateLength - 0.1f) * 0.6f;
        return;
    }

    // STYLE_ARP — sort each group of four steps into an ascending or
    // descending run, so the pattern moves stepwise instead of leaping.
    for (int block = 0; block < MAX_STEPS; block += 4) {
        const bool ascending = rng.nextFloat() < 0.6f;
        for (int a = 0; a < 4; a++) {
            for (int b = a + 1; b < 4; b++) {
                const int ia = block + a, ib = block + b;
                const bool swapNeeded = ascending
                    ? steps[ia].pitchIndex > steps[ib].pitchIndex
                    : steps[ia].pitchIndex < steps[ib].pitchIndex;
                if (swapNeeded) {
                    const int tmp = steps[ia].pitchIndex;
                    steps[ia].pitchIndex = steps[ib].pitchIndex;
                    steps[ib].pitchIndex = tmp;
                }
            }
        }
    }
    // Gate: even and mid-length, so runs read as a line rather than as stabs.
    // Compresses 0.1–0.9 into 0.25–0.49.
    for (int i = 0; i < MAX_STEPS; i++)
        steps[i].gateLength = 0.25f + (steps[i].gateLength - 0.1f) * 0.3f;
}

inline void generatePattern(int seedIndex, StepData steps[MAX_STEPS]) {
    generateBasePattern(seedIndex, steps);
    applyStyle(seedIndex, steps);
}

} // namespace nzzl
