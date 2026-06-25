#pragma once
#include <cstdint>

// Xorshift32 — deterministic, no stdlib rand()
struct Xorshift32 {
    uint32_t state;

    explicit Xorshift32(uint32_t seed) : state(seed ? seed : 1) {}

    uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    // Returns float in [0, 1)
    float nextFloat() {
        return (next() & 0x7FFFFFFF) / float(0x7FFFFFFF);
    }

    // Returns int in [0, n)
    int nextInt(int n) {
        return int(nextFloat() * n);
    }
};
