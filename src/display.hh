#pragma once
#include "pattern.hh"
#include "scales.hh"

// Pure text building for the panel display (Task 11) — no Rack deps, no
// heap, no <string>, no stdio. The strings are built here so the harness can
// check every combination; only the pixels are hardware's problem.

namespace nzzl {

constexpr int DISPLAY_LEN = 20;

inline const char* NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

struct DisplayText {
    char steps[DISPLAY_LEN];   // "8/16" — effective density / length (item 16)
    char style[DISPLAY_LEN];   // "BASS", "RAND", "ARP\u2191", "ARP\u2193", "ARP\u2195", "ARP\u2194"
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

// No leading zero: "8/16", not "08/16".
inline int appendInt(char* dst, int at, int value) {
    value = clampi(value, 0, 99);
    if (value >= 10 && at < DISPLAY_LEN - 1) dst[at++] = char('0' + value / 10);
    if (at < DISPLAY_LEN - 1)                dst[at++] = char('0' + value % 10);
    dst[at] = '\0';
    return at;
}

// Line 1 shows what actually plays: DENSITY (CV included) can be turned past
// LENGTH, but the engine clamps it to the loop, so the display does too.
inline void buildDisplay(int density, int length, int stylePos, int scaleIndex,
                         int root, DisplayText& out) {
    length  = clampi(length, 2, MAX_STEPS);
    density = clampi(density, 1, length);
    root    = clampi(root, 0, 11);

    int n = appendInt(out.steps, 0, density);
    n = appendStr(out.steps, n, "/");
    appendInt(out.steps, n, length);

    appendStr(out.style, 0, styleName(stylePos));

    // Root is meaningful in raw mode too — it still transposes — so it is
    // always shown, with the scale slot reading RAW when unquantized.
    n = appendStr(out.scale, 0, NOTE_NAMES[root]);
    n = appendStr(out.scale, n, " ");
    appendStr(out.scale, n, getScale(clampi(scaleIndex, 0, NUM_SCALES - 1)).name);
}

} // namespace nzzl
