#pragma once
#include "pattern.hh"
#include "scales.hh"
#include <cmath>

// Pure CV-input mapping (Task 10) — no Rack dependencies.
//
// Every CV input is an OFFSET added to its knob, never an absolute value.
// *Why:* absolute CV would make the panel lie — GROUP/SUBGROUP could read 3/3
// while something else entirely plays — and it would fight the Task 9 knob
// sync, whose whole point is that what you hear is always reproducible from
// what you see. An offset keeps the knob as the base the player set.
//
// Each mapping is a plain function of (knob, volts, connected) so the harness
// can check the tables and the clamping without a running module.

namespace nzzl {

// ── CV SCALE ────────────────────────────────────────────────────────────────
// 1 V per scale position, CLAMPED. There is nothing musical past either end of
// the list, and wrapping would jump from Minor Pentatonic straight to
// unquantized mid-phrase.
inline int cvScaleOffset(float volts) {
    return int(std::lround(volts));
}
inline int applyScaleCv(int knob, float volts, bool connected) {
    const int v = knob + (connected ? cvScaleOffset(volts) : 0);
    return clampi(v, 0, NUM_SCALES - 1);
}

// ── CV ROOT ─────────────────────────────────────────────────────────────────
// V/oct: 1 V = 12 semitones, so patching a pitch CV transposes by that
// interval. MUST land on whole semitones — a smooth CV passed straight through
// would de-quantize the output and break the ROOT invariant in
// docs/DESIGN.md § Pitch mapping. Wraps, because transposing past B is
// musically just C again.
inline int cvRootOffset(float volts) {
    return int(std::lround(volts * 12.f));
}
inline int applyRootCv(int knob, float volts, bool connected) {
    const int v = knob + (connected ? cvRootOffset(volts) : 0);
    return ((v % 12) + 12) % 12;
}

// ── CV SEED ─────────────────────────────────────────────────────────────────
// 10 V spans the whole 1024-seed range. WRAPS rather than clamps so an LFO
// sweeps continuously instead of parking at an end of the range.
//
// This resolves the "offset vs absolute" question left open in DESIGN.md:
// offset, for the reason at the top of this file.
inline int cvSeedOffset(float volts) {
    return int(std::lround(volts * (float(NUM_SEEDS) / 10.f)));
}
inline int applySeedCv(int knobSeedIndex, float volts, bool connected) {
    const int v = knobSeedIndex + (connected ? cvSeedOffset(volts) : 0);
    return ((v % NUM_SEEDS) + NUM_SEEDS) % NUM_SEEDS;
}

// ── CV SLIDE ────────────────────────────────────────────────────────────────
// 10 V spans the knob's full 0..1 travel, clamped.
inline float applySlideCv(float knob, float volts, bool connected) {
    const float v = knob + (connected ? volts / 10.f : 0.f);
    return clampf(v, 0.f, 1.f);
}

} // namespace nzzl
