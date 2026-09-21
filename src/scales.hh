#pragma once
#include "pattern.hh"
#include <cstdint>

// Pure pitch quantization — no Rack dependencies, fully deterministic.
// Turns a StepData's raw pitchIndex/octaveRaw into a 1V/oct voltage.
//
// Design notes (see docs/DESIGN.md § Pitch mapping):
//  - Scale ORDER IS AN ON-DISK CONTRACT once patches exist: SCALE_PARAM stores
//    an index, so inserting a scale mid-list would retune every saved patch.
//    Append new scales at the end; never reorder.
//  - Index 0 is UNQUANTIZED, not a scale. It replaces what used to be a
//    separate SCALE LOCK switch: one knob now answers the whole question
//    "how are these notes pitched?", and CV SCALE (Task 10) can sweep into
//    and out of raw mode without needing a second CV input.
//  - The degree mapping is proportional, not modulo, so a step that was the
//    highest note of the pattern stays the highest when the scale changes.
//    Melodic contour survives a scale sweep.
//  - ROOT only ever adds a whole number of semitones, so transposing can
//    never knock a quantized note off the semitone grid.

namespace nzzl {

// The twelve scales named in the handoff spec, plus position 0 for
// unquantized — thirteen knob positions in all.
constexpr int NUM_SCALES      = 13;
constexpr int MAX_SCALE_NOTES = 12;

struct Scale {
    const char* name;                    // short label, for panel/display use
    int         noteCount;
    int8_t      intervals[MAX_SCALE_NOTES];  // semitones above root, ascending
};

// Index 0 means "no scale" — see SCALE_RAW below.
constexpr int SCALE_RAW = 0;

inline bool isUnquantized(int scaleIndex) { return scaleIndex <= SCALE_RAW; }

// Positions 1–12 are the handoff spec's twelve scales, in its order.
// Index order is frozen — append only. See note above.
inline const Scale& getScale(int index) {
    static const Scale table[NUM_SCALES] = {
        { "RAW",       0, {} },                 // unquantized — not a scale
        { "MAJOR",     7, {0,2,4,5,7,9,11} },   // ionian
        { "MINOR",     7, {0,2,3,5,7,8,10} },   // natural minor / aeolian
        { "DORIAN",    7, {0,2,3,5,7,9,10} },
        { "PHRYG",     7, {0,1,3,5,7,8,10} },
        { "MIXO",      7, {0,2,4,5,7,9,10} },
        { "LYDIAN",    7, {0,2,4,6,7,9,11} },
        { "HARM MIN",  7, {0,2,3,5,7,8,11} },
        { "MEL MIN",   7, {0,2,3,5,7,9,11} },   // ascending melodic minor
        { "PENT MAJ",  5, {0,2,4,7,9} },
        { "PENT MIN",  5, {0,3,5,7,10} },
        { "CHROM",    12, {0,1,2,3,4,5,6,7,8,9,10,11} },
        { "WHOLE",     6, {0,2,4,6,8,10} },     // whole tone
    };
    if (index < 0)           index = 0;
    if (index >= NUM_SCALES) index = NUM_SCALES - 1;
    return table[index];
}

// Natural minor — the default the SCALE knob starts on.
constexpr int SCALE_NAT_MINOR = 2;

// Raw pitchIndex (0–15) → scale degree (0 .. noteCount-1).
// Proportional and monotonic: preserves the pattern's melodic contour when
// noteCount changes. A modulo mapping would wrap and scramble it instead.
inline int degreeForIndex(int pitchIndex, int noteCount) {
    if (pitchIndex < 0)          pitchIndex = 0;
    if (pitchIndex >= MAX_STEPS) pitchIndex = MAX_STEPS - 1;
    int d = (pitchIndex * noteCount) / MAX_STEPS;
    if (d >= noteCount) d = noteCount - 1;
    return d;
}

// octaveRaw (0.0–1.0) → octave offset 0 .. octaveRange-1.
// Guarantees the pattern never spans more octaves than the knob asks for.
inline int octaveForRaw(float octaveRaw, int octaveRange) {
    if (octaveRange < 1) octaveRange = 1;
    int o = int(octaveRaw * float(octaveRange));
    if (o < 0)             o = 0;
    if (o >= octaveRange)  o = octaveRange - 1;
    return o;
}

// A step's octave. `octaveJump >= 0` is an explicit offset the style layer
// asked for (a 303 octave-up is +1 and must stay +1 whatever the OCTAVE
// RANGE knob says); -1 means "spread me across the range" and falls back to
// the uniform float.
inline int octaveForStep(int octaveJump, float octaveRaw, int octaveRange) {
    if (octaveRange < 1) octaveRange = 1;
    if (octaveJump >= 0)
        return octaveJump < octaveRange ? octaveJump : octaveRange - 1;
    return octaveForRaw(octaveRaw, octaveRange);
}

struct QuantizeParams {
    int scaleIndex  = SCALE_NAT_MINOR;  // 0 = unquantized, 1.. = a scale
    int root        = 0;       // 0 .. 11 semitones
    int octaveRange = 2;       // 1 .. 5
};

// Quantized: root transpose + octave + scale degree, in 1V/oct.
// The octave span is exactly octaveRange; root adds a further 0–11/12 V of
// transposition on top, which is intended (it moves the whole pattern in key).
inline float rawVoltage(int pitchIndex, float octaveRaw,
                        const QuantizeParams& p);

inline float quantizedVoltage(int pitchIndex, float octaveRaw,
                              const QuantizeParams& p) {
    const Scale& sc = getScale(p.scaleIndex);
    // The RAW entry has no intervals; never quantize against it.
    if (sc.noteCount <= 0)
        return rawVoltage(pitchIndex, octaveRaw, p);
    int root = p.root < 0 ? 0 : (p.root > 11 ? 11 : p.root);
    int degree   = degreeForIndex(pitchIndex, sc.noteCount);
    int octave   = octaveForRaw(octaveRaw, p.octaveRange);
    (void)0;
    int semitone = root + sc.intervals[degree];
    return float(octave) + float(semitone) / 12.f;
}

// Unquantized: 16 equal divisions per octave (75 cents apart) so the pattern
// reads as deliberately microtonal rather than as a broken scale.
inline float rawVoltage(int pitchIndex, float octaveRaw,
                        const QuantizeParams& p) {
    int root = p.root < 0 ? 0 : (p.root > 11 ? 11 : p.root);
    int idx  = pitchIndex < 0 ? 0 : (pitchIndex >= MAX_STEPS ? MAX_STEPS - 1
                                                             : pitchIndex);
    int octave = octaveForRaw(octaveRaw, p.octaveRange);
    return float(root) / 12.f + float(octave)
         + float(idx) / float(MAX_STEPS);
}

// A step's pitch, honouring an explicit style-set octave jump.
inline float pitchVoltage(const StepData& s, const QuantizeParams& p) {
    const int octave = octaveForStep(s.octaveJump, s.octaveRaw, p.octaveRange);
    // Re-express the chosen octave as a raw value the helpers below accept.
    const int range = p.octaveRange < 1 ? 1 : p.octaveRange;
    const float raw = (float(octave) + 0.5f) / float(range);
    return isUnquantized(p.scaleIndex) ? rawVoltage(s.pitchIndex, raw, p)
                                       : quantizedVoltage(s.pitchIndex, raw, p);
}

} // namespace nzzl
