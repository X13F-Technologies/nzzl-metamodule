#pragma once
#include "pattern.hh"
#include "scales.hh"
#include <cmath>

// Pure CV-input mapping (Task 10) — no Rack dependencies.
//
// The handoff spec is explicit about each jack, and they are NOT all the same:
//   CV SCALE  "CV override for scale selection"
//   CV ROOT   "CV override for root note"
//   CV SEED   "CV offset for seed selection"
//   CV SLIDE  "CV amount added to slide (summed with seed-derived slide,
//              scaled by attenuator knob)"
// So SCALE and ROOT take over from their knobs when patched, SEED offsets
// from its knobs, and SLIDE sums BEFORE the knob attenuates. An earlier pass
// made all four offsets; this matches the spec instead.
//
// Each mapping is a plain function of (knob, volts, connected) so the harness
// can check the tables and the clamping without a running module.

namespace nzzl {

// ── CV SCALE — override ─────────────────────────────────────────────────────
// 0–10 V spans the whole list, so a full-range LFO reaches every position
// including 0 (unquantized). Clamped: there is nothing past either end.
inline int cvToScale(float volts) {
    const float t = clampf(volts, 0.f, 10.f) / 10.f;
    return clampi(int(t * float(NUM_SCALES - 1) + 0.5f), 0, NUM_SCALES - 1);
}
inline int applyScaleCv(int knob, float volts, bool connected) {
    return connected ? cvToScale(volts) : clampi(knob, 0, NUM_SCALES - 1);
}

// ── CV ROOT — override ──────────────────────────────────────────────────────
// V/oct, so patching a pitch CV names the key directly. MUST land on whole
// semitones: a smooth CV passed straight through would de-quantize the output
// and break the ROOT invariant in docs/DESIGN.md § Pitch mapping.
inline int cvToRoot(float volts) {
    const int semis = int(std::lround(volts * 12.f));
    return ((semis % 12) + 12) % 12;
}
inline int applyRootCv(int knob, float volts, bool connected) {
    return connected ? cvToRoot(volts) : clampi(knob, 0, 11);
}

// ── CV SEED — offset ────────────────────────────────────────────────────────
// 10 V spans all 1024 seeds. WRAPS rather than clamps so an LFO sweeps
// continuously instead of parking at an end of the range. Offset, not
// override, so GROUP/SUBGROUP stay the base the player dialled in — and so it
// does not fight the reseed knob sync.
inline int cvSeedOffset(float volts) {
    return int(std::lround(volts * (float(NUM_SEEDS) / 10.f)));
}
inline int applySeedCv(int knobSeedIndex, float volts, bool connected) {
    const int v = knobSeedIndex + (connected ? cvSeedOffset(volts) : 0);
    return ((v % NUM_SEEDS) + NUM_SEEDS) % NUM_SEEDS;
}

// ── CV SLIDE — summed with the seed, THEN attenuated ────────────────────────
// The spec's wording is precise: the jack is "summed with seed-derived slide,
// scaled by attenuator knob", and "summed before attenuator scaling". So per
// step the amount is
//
//     knob x clamp01( (step has a slide flag ? 1 : 0) + cv )
//
// Two consequences worth knowing. CCW on the knob kills slide outright
// however much CV is arriving — that is what an attenuator is for, and the
// spec says so ("CCW = no slide regardless of seed"). And CV can put glide on
// steps the seed did NOT flag, which is the useful direction: sweep the jack
// and the line goes from articulated to fully legato.
//
// The knob and the CV therefore cannot be pre-mixed into one number: the
// engine needs both, because the sum happens per step against that step's
// flag. cvSlideAmount is the jack's half.
inline float cvSlideAmount(float volts, bool connected) {
    return connected ? clampf(volts, 0.f, 10.f) / 10.f : 0.f;
}

// The per-step result, given that step's seed flag.
inline float slideForStep(bool seedFlag, float cvAmount, float knob) {
    const float summed = (seedFlag ? 1.f : 0.f) + clampf(cvAmount, 0.f, 1.f);
    return clampf(knob, 0.f, 1.f) * clampf(summed, 0.f, 1.f);
}

} // namespace nzzl
