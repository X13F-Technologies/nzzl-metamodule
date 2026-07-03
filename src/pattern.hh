#pragma once
#include "rng.hh"
#include <cstdint>

// Pure pattern generation — no Rack dependencies, fully deterministic.
// Everything here is testable outside VCV Rack / MetaModule.

namespace nzzl {

constexpr int MAX_STEPS = 16;

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
};

inline Xorshift32 attrRng(int seedIndex, uint32_t salt) {
    return Xorshift32(hashSeed(uint32_t(seedIndex) ^ salt));
}

inline void generatePattern(int seedIndex, StepData steps[MAX_STEPS]) {
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

} // namespace nzzl
