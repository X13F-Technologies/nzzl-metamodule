#pragma once
#include "pattern.hh"
#include <cstdint>
#include <cstring>

// Holding, switching and saving rolls (item 16) — pure, no Rack deps.
//
// The adapter owns one RollPlayer. Generate rolls into it; the STYLE knob
// picks a row from it; the patch saves it. Every decision about WHEN the
// playing pattern changes lives here so the harness can test it.

namespace nzzl {

// ── Which pattern is playing ────────────────────────────────────────────────
//
// A new roll and a STYLE change both wait for the loop boundary, so a pattern
// always starts on the grid, from its own step 1, instead of dropping in
// mid-phrase (item 6, kept). The boundary is Engine::atLoopEnd: the stretch
// where the next step to fire is step 0. Until the first pattern is chosen
// there is nothing playing, so that one is taken at once.
struct RollPlayer {
    Roll    active  = {};     // the roll the knobs pick from
    Roll    pending = {};     // a Generate waiting for the loop to wrap
    bool    hasPending = false;
    int     style = -1;       // STYLE row now playing; -1 = nothing chosen yet
    Pattern current = {};     // the row the engine reads

    // Generate: roll into `pending`. Takes effect at the next loop boundary.
    void generate(const RollEntropy& e) {
        rollPatterns(e, pending);
        hasPending = true;
    }

    // Patch load: replaces everything, effective immediately.
    void load(const Roll& r) {
        active     = r;
        hasPending = false;
        style      = -1;
    }

    // What a patch save stores: what you are hearing, or about to hear.
    const Roll& newest() const { return hasPending ? pending : active; }

    // Call once per sample, before the engine, with Engine::atLoopEnd.
    // Returns true if `current` changed.
    bool update(int styleKnob, bool atLoopEnd) {
        styleKnob = clampStyle(styleKnob);
        if (!hasPending && styleKnob == style)
            return false;
        if (style >= 0 && !atLoopEnd)
            return false;                  // hold it for the grid
        if (hasPending) {
            active     = pending;
            hasPending = false;
        }
        style   = styleKnob;
        current = active.rows[style];
        return true;
    }
};

// ── Saving a roll with the patch ────────────────────────────────────────────
//
// The whole roll — every row, not just the one STYLE points at — so the knob
// behaves the same after a reload. Encoded as one hex string of 32-bit words
// (floats by their bit pattern, so the round trip is exact), with no stdio
// and no heap: the caller provides the buffer.
//
// Bump ROLL_FORMAT if the layout changes; a mismatched or invalid string is
// rejected and the module keeps the roll it already has.

constexpr int ROLL_FORMAT        = 1;
constexpr int STEP_WORDS         = 8;
constexpr int PATTERN_WORDS      = MAX_STEPS * STEP_WORDS + NUM_GATE_LENGTHS + 1;
constexpr int ROLL_WORDS         = NUM_STYLES * PATTERN_WORDS;
constexpr int ROLL_HEX_LEN       = ROLL_WORDS * 8;   // excluding the terminator

inline uint32_t floatBits(float f) {
    uint32_t u; std::memcpy(&u, &f, sizeof u); return u;
}
inline float bitsFloat(uint32_t u) {
    float f; std::memcpy(&f, &u, sizeof f); return f;
}

// out must hold ROLL_HEX_LEN + 1 chars.
inline void encodeRoll(const Roll& roll, char* out) {
    static const char HEX[] = "0123456789abcdef";
    int at = 0;
    auto put = [&](uint32_t w) {
        for (int shift = 28; shift >= 0; shift -= 4)
            out[at++] = HEX[(w >> shift) & 0xF];
    };
    for (int r = 0; r < NUM_STYLES; r++) {
        const Pattern& p = roll.rows[r];
        for (int i = 0; i < MAX_STEPS; i++) {
            const StepData& s = p.steps[i];
            put(uint32_t(s.weight));
            put(uint32_t(s.pitchIndex));
            put(uint32_t(s.gateIndex));
            put(uint32_t(s.velLayer));
            put(s.slide ? 1u : 0u);
            put(uint32_t(int32_t(s.octaveJump)));
            put(floatBits(s.octaveRaw));
            put(uint32_t(int32_t(s.acidRole)));
        }
        for (int g = 0; g < NUM_GATE_LENGTHS; g++)
            put(floatBits(p.gateLengths[g]));
        put(floatBits(p.swing));
    }
    out[at] = '\0';
}

inline bool isAcidInterval(int r) {
    return r == -1 || r == ACID_ROOT || r == ACID_THIRD || r == ACID_FOURTH
        || r == ACID_FIFTH || r == ACID_SEVENTH;
}

inline bool in01(float f) { return f >= 0.f && f <= 1.f; }   // false for NaN

// Everything the engine relies on, checked before a loaded roll is trusted.
inline bool patternValid(const Pattern& p) {
    bool seen[MAX_STEPS + 1] = {};
    for (int i = 0; i < MAX_STEPS; i++) {
        const StepData& s = p.steps[i];
        if (s.weight < 1 || s.weight > MAX_STEPS || seen[s.weight]) return false;
        seen[s.weight] = true;
        if (s.pitchIndex < 0 || s.pitchIndex > 15)            return false;
        if (s.gateIndex < 0 || s.gateIndex >= NUM_GATE_LENGTHS) return false;
        if (s.velLayer < 0 || s.velLayer >= NUM_VEL_LAYERS)    return false;
        if (s.octaveJump < -1 || s.octaveJump > 4)             return false;
        if (!in01(s.octaveRaw))                                return false;
        if (!isAcidInterval(s.acidRole))                       return false;
    }
    for (int g = 0; g < NUM_GATE_LENGTHS; g++)
        if (!(p.gateLengths[g] > 0.f && p.gateLengths[g] <= 1.f)) return false;
    return in01(p.swing);
}

// Returns false (and leaves `roll` untouched) on anything malformed.
inline bool decodeRoll(const char* in, Roll& roll) {
    if (!in) return false;
    int len = 0;
    while (len <= ROLL_HEX_LEN && in[len]) len++;
    if (len != ROLL_HEX_LEN) return false;

    int at = 0;
    bool ok = true;
    auto get = [&]() -> uint32_t {
        uint32_t w = 0;
        for (int k = 0; k < 8; k++) {
            const char c = in[at++];
            uint32_t v;
            if (c >= '0' && c <= '9')      v = uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v = uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v = uint32_t(c - 'A' + 10);
            else { ok = false; v = 0; }
            w = (w << 4) | v;
        }
        return w;
    };

    Roll tmp;
    for (int r = 0; r < NUM_STYLES; r++) {
        Pattern& p = tmp.rows[r];
        for (int i = 0; i < MAX_STEPS; i++) {
            StepData& s = p.steps[i];
            s.weight     = int(int32_t(get()));
            s.pitchIndex = int(int32_t(get()));
            s.gateIndex  = int(int32_t(get()));
            s.velLayer   = int(int32_t(get()));
            const uint32_t sl = get();
            if (sl > 1) ok = false;
            s.slide      = sl == 1;
            s.octaveJump = int(int32_t(get()));
            s.octaveRaw  = bitsFloat(get());
            s.acidRole   = int(int32_t(get()));
        }
        for (int g = 0; g < NUM_GATE_LENGTHS; g++)
            p.gateLengths[g] = bitsFloat(get());
        p.swing = bitsFloat(get());
        if (!ok || !patternValid(p)) return false;
    }
    roll = tmp;
    return true;
}

} // namespace nzzl
