// Native test harness for holding, switching and saving rolls (item 16):
// src/roll.hh. No Rack deps.
//
// What it proves: a STYLE position keeps its pattern until the next Generate;
// a new roll and a STYLE change both land on the loop wrap; the whole roll
// survives a save/load round trip bit-exactly; and a damaged save is refused
// rather than played.

#include "../src/roll.hh"
#include "../src/engine.hh"
#include "rolls.hh"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <initializer_list>

static int failures = 0;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            failures++;                                     \
            printf("FAIL %s:%d  ", __FILE__, __LINE__);     \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

using namespace nzzl;
using namespace nzzl_test;

static bool sameRoll(const Roll& a, const Roll& b) {
    for (int r = 0; r < NUM_STYLES; r++)
        if (!samePattern(a.rows[r], b.rows[r])) return false;
    return true;
}

// ── the player ──────────────────────────────────────────────────────────────

// The first pattern is taken at once — there is nothing to wait for.
static void test_first_pattern_is_immediate() {
    RollPlayer pl;
    pl.generate(entropyFor(1));
    CHECK(pl.update(STYLE_POS_ARP_DOWN, false), "first update did not pick a pattern");
    CHECK(pl.style == STYLE_POS_ARP_DOWN, "first update picked style %d", pl.style);
    Roll want; rollFor(1, want);
    CHECK(samePattern(pl.current, want.rows[STYLE_POS_ARP_DOWN]),
          "first pattern is not the generated roll's row");
    CHECK(!pl.hasPending, "generated roll still pending after it was taken");
    printf("player: the first roll and STYLE are taken immediately\n");
}

// Turning STYLE away and back gives the same pattern until the next Generate:
// the knob picks precomputed rows, it does not re-roll.
static void test_style_returns_same_pattern() {
    RollPlayer pl;
    pl.generate(entropyFor(2));
    pl.update(STYLE_POS_BASS, false);
    const Pattern first = pl.current;
    for (int pass = 0; pass < 3; pass++)
        for (int st = 0; st < NUM_STYLES; st++) {
            pl.update(st, true);
            Roll want; rollFor(2, want);
            CHECK(samePattern(pl.current, want.rows[st]),
                  "pass %d style %d: row changed without a Generate", pass, st);
        }
    pl.update(STYLE_POS_BASS, true);
    CHECK(samePattern(pl.current, first), "returning to BASS gave a different pattern");
    printf("player: STYLE picks fixed rows; returning to a position gives the same pattern\n");
}

// A STYLE change and a Generate both wait for the loop to wrap.
static void test_changes_land_on_the_wrap() {
    RollPlayer pl;
    pl.generate(entropyFor(3));
    pl.update(STYLE_POS_RAND, false);
    Roll r3; rollFor(3, r3);
    Roll r4; rollFor(4, r4);

    // STYLE change mid-loop: held
    CHECK(!pl.update(STYLE_POS_ARP_UP, false), "STYLE change applied mid-loop");
    CHECK(samePattern(pl.current, r3.rows[STYLE_POS_RAND]), "pattern moved mid-loop");
    CHECK(pl.update(STYLE_POS_ARP_UP, true), "STYLE change did not land on the wrap");
    CHECK(samePattern(pl.current, r3.rows[STYLE_POS_ARP_UP]), "wrong row after the wrap");

    // Generate mid-loop: held, and saved as the newest roll meanwhile
    pl.generate(entropyFor(4));
    for (int i = 0; i < 10; i++)
        CHECK(!pl.update(STYLE_POS_ARP_UP, false), "Generate applied mid-loop");
    CHECK(samePattern(pl.current, r3.rows[STYLE_POS_ARP_UP]), "pattern moved before the wrap");
    CHECK(sameRoll(pl.newest(), r4), "a pending roll is not what a save would store");
    CHECK(pl.update(STYLE_POS_ARP_UP, true), "Generate did not land on the wrap");
    CHECK(samePattern(pl.current, r4.rows[STYLE_POS_ARP_UP]), "new roll not playing after the wrap");
    CHECK(sameRoll(pl.active, r4) && !pl.hasPending, "new roll not adopted");

    // nothing pending, same STYLE: a wrap changes nothing
    CHECK(!pl.update(STYLE_POS_ARP_UP, true), "an idle wrap reported a change");
    printf("player: STYLE changes and Generates both wait for the loop wrap\n");
}

// Every Generate gives a different pattern at the same STYLE position —
// "rhythm varies across rolls" from the player's side.
static void test_generate_changes_the_pattern() {
    RollPlayer pl;
    int sameRhythm = 0;
    unsigned prevMask = 0;
    for (int k = 0; k < 256; k++) {
        pl.generate(entropyFor(k));
        pl.update(STYLE_POS_RAND, true);
        unsigned m = 0;
        for (int i = 0; i < MAX_STEPS; i++)
            if (stepActive(pl.current, i, 16, 4)) m |= 1u << i;
        if (k > 0 && m == prevMask) sameRhythm++;
        prevMask = m;
    }
    // 4 of 16 steps: 1820 possible rhythms; a repeat is rare, a run of them a bug
    CHECK(sameRhythm < 8, "%d of 255 Generates kept the same DENSITY 4 rhythm", sameRhythm);
    printf("player: successive Generates change the rhythm (%d/255 repeats at DENSITY 4)\n",
           sameRhythm);
}

// A patch load takes effect at once, replacing any pending Generate.
static void test_load_is_immediate() {
    RollPlayer pl;
    pl.generate(entropyFor(5));
    pl.update(STYLE_POS_BASS, false);
    pl.generate(entropyFor(6));                 // pending, not yet playing
    Roll saved; rollFor(7, saved);
    pl.load(saved);
    CHECK(!pl.hasPending, "load left a stale Generate pending");
    CHECK(pl.update(STYLE_POS_BASS, false), "loaded roll did not apply immediately");
    CHECK(samePattern(pl.current, saved.rows[STYLE_POS_BASS]), "not playing the loaded roll");
    printf("player: a patch load applies immediately and drops any pending Generate\n");
}

// ── player + engine together: a queued pattern starts on its own step 1 ────
//
// The bug this guards: the swap used to wait for Engine::loopWrapped, which
// is raised on the sample step 1 FIRES — so step 1 of the first new loop
// still played the old pattern and the new one was heard from step 2. Now
// the swap happens during the old loop's last step (Engine::atLoopEnd).
//
// Two patterns told apart by velocity alone: "old" has every step on the
// off layer (2.2 V), "new" every step on the accent layer (10 V). DENSITY 16
// and SLIDE 0, so every step plays. The change is requested mid-loop; the
// test records (step, velocity) every time a step fires. (Fires, not gate
// edges: with swing, a delayed last step's gate can overlap step 1.)
struct Onset { int step; float vel; };

static int runSwap(bool viaStyle, int length, int clockDiv, float swing,
                   Onset* log, int maxLog, int& requestedAt) {
    Roll oldR; rollFor(21, oldR);
    Roll newR = oldR;
    for (int r = 0; r < NUM_STYLES; r++)
        for (int i = 0; i < MAX_STEPS; i++) {
            oldR.rows[r].steps[i].velLayer = 0;
            newR.rows[r].steps[i].velLayer = 2;
        }
    // via STYLE: one roll, RAND quiet and ARP↑ loud
    if (viaStyle)
        oldR.rows[STYLE_POS_ARP_UP] = newR.rows[STYLE_POS_ARP_UP];

    RollPlayer pl;
    pl.load(oldR);
    Engine e;
    EngineParams p;
    p.density = 16; p.length = length; p.clockDiv = clockDiv;
    p.slideKnob = 0.f; p.gate = 1.f; p.accent = 1.f; p.swing = swing;

    const float SR = 48000.f, dt = 1.f / SR, period = 0.05f;
    float phase = 0.f;
    int lastStep = -1;
    bool lastStarted = false;
    int n = 0, style = STYLE_POS_RAND;
    requestedAt = -1;
    int onsets = 0;
    for (int i = 0; i < int(SR * period * clockDiv * 3 * MAX_STEPS) && n < maxLog; i++) {
        phase += dt; if (phase >= period) phase -= period;
        EngineInputs in;
        in.clock = phase < period * 0.5f ? 10.f : 0.f;
        pl.update(style, e.atLoopEnd);
        const EngineOutputs o = e.process(dt, in, pl.current, p);
        const bool fired = e.started && (!lastStarted || e.step != lastStep);
        lastStarted = e.started;
        lastStep = e.step;
        if (fired) {
            log[n++] = { e.step, o.velocity };
            // request the change halfway through the first loop
            if (++onsets == length / 2 + 1 && requestedAt < 0) {
                requestedAt = n - 1;
                if (viaStyle) style = STYLE_POS_ARP_UP;
                else { pl.pending = newR; pl.hasPending = true; }
            }
        }
    }
    return n;
}

static void test_queued_pattern_starts_on_step_one() {
    int cases = 0;
    for (int via = 0; via < 2; via++)
        for (int length : {16, 8, 7, 3})
            for (int div : {1, 3})
                for (float swing : {0.f, 1.f}) {
                    Onset log[64]; int req = -1;
                    const int n = runSwap(via == 1, length, div, swing, log, 64, req);
                    CHECK(req >= 0 && n > req + length + 1,
                          "via %d len %d: run too short (%d onsets)", via, length, n);
                    if (req < 0) continue;
                    // the rest of the requesting loop: old pattern
                    int k = req;
                    for (; k < n && !(k > req && log[k].step == 0); k++)
                        CHECK(log[k].vel < 5.f,
                              "%s len %d div %d swing %.0f: step %d after the request "
                              "played the NEW pattern before the loop ended",
                              via ? "STYLE" : "Generate", length, div, swing, log[k].step + 1);
                    // from the first step 0 after it: new pattern, step 1 included
                    CHECK(k < n && log[k].step == 0, "no loop wrap observed");
                    for (int j = k; j < n && j < k + length; j++)
                        CHECK(log[j].vel > 9.f,
                              "%s len %d div %d swing %.0f: step %d of the first new "
                              "loop played the OLD pattern",
                              via ? "STYLE" : "Generate", length, div, swing, log[j].step + 1);
                    cases++;
                }
    printf("grid: a Generate or STYLE change starts on the new pattern's own step 1 "
           "(%d cases: lengths, clock divides, swing)\n", cases);
}

// ── save / load ─────────────────────────────────────────────────────────────

static void test_roundtrip() {
    std::vector<char> hex(ROLL_HEX_LEN + 1);
    for (int k = 0; k < NUM_ROLLS; k++) {
        Roll a; rollFor(k, a);
        encodeRoll(a, hex.data());
        CHECK(std::strlen(hex.data()) == size_t(ROLL_HEX_LEN),
              "roll %d encoded to %zu chars, expected %d", k, std::strlen(hex.data()),
              ROLL_HEX_LEN);
        Roll b; rollFor(k + 1, b);              // something different to overwrite
        CHECK(decodeRoll(hex.data(), b), "roll %d: its own encoding was rejected", k);
        CHECK(sameRoll(a, b), "roll %d: did not survive the round trip bit-exactly", k);
        CHECK(patternValid(a.rows[0]) && patternValid(a.rows[NUM_STYLES - 1]),
              "roll %d: a generated row fails validation", k);
    }
    printf("save/load: all %d rolls round-trip bit-exactly (every row, %d chars)\n",
           NUM_ROLLS, ROLL_HEX_LEN);
}

// A damaged or foreign save must be refused, and must not touch the roll.
static void test_bad_saves_are_refused() {
    std::vector<char> hex(ROLL_HEX_LEN + 1);
    Roll good; rollFor(9, good);
    encodeRoll(good, hex.data());

    Roll keep; rollFor(10, keep);
    const Roll before = keep;
    auto refused = [&](const char* s, const char* why) {
        const bool ok = decodeRoll(s, keep);
        CHECK(!ok, "accepted a save that %s", why);
        CHECK(sameRoll(keep, before), "a refused save (%s) still changed the roll", why);
    };

    refused(nullptr, "is null");
    refused("", "is empty");
    {
        std::string s(hex.data()); s.pop_back();
        refused(s.c_str(), "is one char short");
    }
    {
        std::string s(hex.data()); s += "0";
        refused(s.c_str(), "is one char long");
    }
    {
        std::string s(hex.data()); s[100] = 'x';
        refused(s.c_str(), "has a non-hex char");
    }
    {
        // the first word is step 1's weight: 0 breaks the permutation
        std::string s(hex.data()); s.replace(0, 8, "00000000");
        refused(s.c_str(), "has weight 0");
    }
    {
        // duplicate weight: copy step 2's weight word over step 1's
        std::string s(hex.data());
        s.replace(0, 8, s.substr(STEP_WORDS * 8, 8));
        refused(s.c_str(), "has a duplicate weight");
    }
    {
        // a NaN octaveRaw (word 6 of step 1)
        std::string s(hex.data()); s.replace(6 * 8, 8, "7fc00000");
        refused(s.c_str(), "has a NaN octave");
    }
    {
        // slide flag 2 (word 4 of step 1)
        std::string s(hex.data()); s.replace(4 * 8, 8, "00000002");
        refused(s.c_str(), "has slide flag 2");
    }
    {
        // the last row's swing (last word) out of 0..1
        std::string s(hex.data());
        s.replace(ROLL_HEX_LEN - 8, 8, "40000000");   // 2.0f
        refused(s.c_str(), "has swing 2.0");
    }
    // and upper-case hex is accepted
    {
        std::string s(hex.data());
        for (char& c : s) if (c >= 'a' && c <= 'f') c = char(c - 'a' + 'A');
        Roll up; rollFor(11, up);
        CHECK(decodeRoll(s.c_str(), up) && sameRoll(up, good), "upper-case hex was refused");
    }
    printf("save/load: truncated, padded, non-hex, out-of-range and NaN saves are refused\n");
}

int main() {
    test_first_pattern_is_immediate();
    test_style_returns_same_pattern();
    test_changes_land_on_the_wrap();
    test_generate_changes_the_pattern();
    test_load_is_immediate();
    test_queued_pattern_starts_on_step_one();
    test_roundtrip();
    test_bad_saves_are_refused();

    if (failures) {
        printf("\n%d FAILURES\n", failures);
        return 1;
    }
    printf("\nall tests passed\n");
    return 0;
}
