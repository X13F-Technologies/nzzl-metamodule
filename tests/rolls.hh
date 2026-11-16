#pragma once
// Test-only helpers: reproducible "many rolls" for the native harness.
//
// The module draws a roll's entropy from a non-deterministic source at
// Generate. The tests instead draw it from splitmix64 over a counter, so
// roll k is the same on every run and in CI, and a failure names a roll that
// can be reproduced. Nothing here is compiled into the plugin.

#include "../src/pattern.hh"
#include <cstdint>

namespace nzzl_test {

// How many rolls the property tests sweep. Replaces the old "all 1024 seeds".
constexpr int NUM_ROLLS = 1024;

inline uint64_t splitmix64(uint64_t& x) {
    uint64_t z = (x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

inline nzzl::RollEntropy entropyFor(int k) {
    uint64_t x = 0xC0FFEE00ull + uint64_t(k) * 0x10001ull;
    nzzl::RollEntropy e;
    for (int i = 0; i < nzzl::NUM_LANES; i++)
        e.lane[i] = uint32_t(splitmix64(x) >> 32);
    return e;
}

inline void rollFor(int k, nzzl::Roll& roll) {
    nzzl::rollPatterns(entropyFor(k), roll);
}

// One row of roll k.
inline void patternFor(int k, int stylePos, nzzl::Pattern& pat) {
    nzzl::Roll roll;
    rollFor(k, roll);
    pat = roll.rows[nzzl::clampStyle(stylePos)];
}

// A row of roll k, cycling through every STYLE position as k grows — for
// tests that used to sweep every seed and so every zone.
inline void anyPatternFor(int k, nzzl::Pattern& pat) {
    patternFor(k, k % nzzl::NUM_STYLES, pat);
}

inline bool samePattern(const nzzl::Pattern& a, const nzzl::Pattern& b) {
    for (int g = 0; g < nzzl::NUM_GATE_LENGTHS; g++)
        if (a.gateLengths[g] != b.gateLengths[g]) return false;
    if (a.swing != b.swing) return false;
    for (int i = 0; i < nzzl::MAX_STEPS; i++) {
        const nzzl::StepData& x = a.steps[i];
        const nzzl::StepData& y = b.steps[i];
        if (x.weight != y.weight || x.pitchIndex != y.pitchIndex
            || x.gateIndex != y.gateIndex || x.velLayer != y.velLayer
            || x.slide != y.slide || x.octaveJump != y.octaveJump
            || x.octaveRaw != y.octaveRaw || x.acidRole != y.acidRole)
            return false;
    }
    return true;
}

} // namespace nzzl_test
