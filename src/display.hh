#pragma once
#include "pattern.hh"
#include "scales.hh"

// Pure text building for the panel display (Task 11) — no Rack deps, no
// heap, no <string>, no stdio. The strings are built here so the harness can
// check all 1024 seeds; only the pixels are hardware's problem.

namespace nzzl {

constexpr int DISPLAY_LEN = 20;

inline const char* NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

struct DisplayText {
    char seed[DISPLAY_LEN];    // "4 . 7"  — the spec's group/subgroup format
    char zone[DISPLAY_LEN];    // "BASS", "RAND", "ARP\u2191", "ARP\u2193", "ARP\u2195", "ARP\u2194"
    char scale[DISPLAY_LEN];   // "C MINOR", or "C RAW" when unquantized
};

// Minimal fixed-width helpers — no stdio, so nothing can allocate or throw
// on the audio thread.
inline int appendStr(char* dst, int at, const char* src) {
    while (*src && at < DISPLAY_LEN - 1)
        dst[at++] = *src++;
    dst[at] = '\0';
    return at;
}

inline int append2Digit(char* dst, int at, int value) {
    value = clampi(value, 0, 99);
    if (at < DISPLAY_LEN - 1) dst[at++] = char('0' + value / 10);
    if (at < DISPLAY_LEN - 1) dst[at++] = char('0' + value % 10);
    dst[at] = '\0';
    return at;
}

// No leading zero — the spec shows "4 . 7", not "04 . 07".
inline int appendInt(char* dst, int at, int value) {
    value = clampi(value, 0, 99);
    if (value >= 10 && at < DISPLAY_LEN - 1) dst[at++] = char('0' + value / 10);
    if (at < DISPLAY_LEN - 1)                dst[at++] = char('0' + value % 10);
    dst[at] = '\0';
    return at;
}

inline void buildDisplay(int seedIndex, int scaleIndex, int root,
                         DisplayText& out) {
    seedIndex = clampi(seedIndex, 0, NUM_SEEDS - 1);
    root      = clampi(root, 0, 11);

    int group = 0, subgroup = 0;
    seedKnobsFor(seedIndex, group, subgroup);

    int n = appendInt(out.seed, 0, group);
    n = appendStr(out.seed, n, " . ");
    appendInt(out.seed, n, subgroup);

    appendStr(out.zone, 0, styleName(seedIndex));

    // Root is meaningful in raw mode too — it still transposes — so it is
    // always shown, with the scale slot reading RAW when unquantized.
    n = appendStr(out.scale, 0, NOTE_NAMES[root]);
    n = appendStr(out.scale, n, " ");
    appendStr(out.scale, n, getScale(clampi(scaleIndex, 0, NUM_SCALES - 1)).name);
}

} // namespace nzzl
