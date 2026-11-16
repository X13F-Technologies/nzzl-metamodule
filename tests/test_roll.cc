// Native test harness for holding, switching and saving rolls (item 16):
// src/roll.hh. No Rack deps.
//
// What it proves: a STYLE position keeps its pattern until the next Generate;
// a new roll and a STYLE change both land on the loop wrap; the whole roll
// survives a save/load round trip bit-exactly; and a damaged save is refused
// rather than played.

#include "../src/roll.hh"
#include "rolls.hh"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
    test_roundtrip();
    test_bad_saves_are_refused();

    if (failures) {
        printf("\n%d FAILURES\n", failures);
        return 1;
    }
    printf("\nall tests passed\n");
    return 0;
}
