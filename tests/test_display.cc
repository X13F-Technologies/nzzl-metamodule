// Native test harness for the panel display text (Task 11). The pixels are
// hardware's problem; the strings are testable here, for every combination.

#include "../src/display.hh"
#include <cstdio>
#include <cstring>

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

// Line 1: every density x length renders "d/l" — the effective density,
// clamped to the loop the way the engine clamps it — with no leading zeros.
static void test_steps_line() {
    int cases = 0;
    for (int length = 2; length <= MAX_STEPS; length++)
        for (int density = 1; density <= MAX_STEPS; density++) {
            DisplayText d;
            buildDisplay(density, length, STYLE_POS_BASS, SCALE_NAT_MINOR, 0, d);
            CHECK(strnlen(d.steps, DISPLAY_LEN) < DISPLAY_LEN, "unterminated steps text");
            CHECK(strnlen(d.style, DISPLAY_LEN) < DISPLAY_LEN, "unterminated style text");
            CHECK(strnlen(d.scale, DISPLAY_LEN) < DISPLAY_LEN, "unterminated scale text");
            CHECK(d.steps[0] && d.style[0] && d.scale[0], "empty display line");

            const int eff = density < length ? density : length;
            char expect[DISPLAY_LEN];
            int n = 0;
            if (eff >= 10) expect[n++] = char('0' + eff / 10);
            expect[n++] = char('0' + eff % 10);
            expect[n++] = '/';
            if (length >= 10) expect[n++] = char('0' + length / 10);
            expect[n++] = char('0' + length % 10);
            expect[n] = '\0';
            CHECK(strcmp(d.steps, expect) == 0,
                  "density %d length %d: got \"%s\", expected \"%s\"",
                  density, length, d.steps, expect);
            cases++;
        }
    DisplayText d;
    buildDisplay(8, 16, 0, SCALE_NAT_MINOR, 0, d);
    CHECK(strcmp(d.steps, "8/16") == 0, "default shows \"%s\", expected \"8/16\"", d.steps);
    printf("display: line 1 renders density/length for all %d combinations\n", cases);
}

// Line 2: the STYLE position, one distinct label each.
static void test_style_labels() {
    DisplayText d;
    buildDisplay(8, 16, STYLE_POS_BASS, SCALE_NAT_MINOR, 0, d);
    CHECK(strcmp(d.style, "BASS") == 0, "BASS shows \"%s\"", d.style);
    buildDisplay(8, 16, STYLE_POS_RAND, SCALE_NAT_MINOR, 0, d);
    CHECK(strcmp(d.style, "RAND") == 0, "RAND shows \"%s\"", d.style);
    // The four arp directions must each get their own arrow, per the spec.
    for (int k = STYLE_POS_ARP_UP; k <= STYLE_POS_ARP_DOWNUP; k++) {
        buildDisplay(8, 16, k, SCALE_NAT_MINOR, 0, d);
        CHECK(strncmp(d.style, "ARP", 3) == 0, "STYLE %d shows \"%s\"", k, d.style);
        CHECK(strcmp(d.style, styleName(k)) == 0, "STYLE %d label mismatch", k);
        for (int j = STYLE_POS_ARP_UP; j < k; j++) {
            DisplayText e;
            buildDisplay(8, 16, j, SCALE_NAT_MINOR, 0, e);
            CHECK(strcmp(d.style, e.style) != 0,
                  "STYLE %d and %d share the label \"%s\"", j, k, d.style);
        }
    }
    printf("display: style label covers BASS, RAND and the four arp directions\n");
}

// Root and scale together, including the unquantized position.
static void test_scale_line() {
    DisplayText d;
    for (int root = 0; root < 12; root++) {
        for (int sc = 0; sc < NUM_SCALES; sc++) {
            buildDisplay(8, 16, 0, sc, root, d);
            char expect[DISPLAY_LEN * 2];
            int n = 0;
            for (const char* p = NOTE_NAMES[root]; *p; p++) expect[n++] = *p;
            expect[n++] = ' ';
            for (const char* p = getScale(sc).name; *p; p++) expect[n++] = *p;
            expect[n] = '\0';
            CHECK(strcmp(d.scale, expect) == 0,
                  "root %d scale %d: got \"%s\", expected \"%s\"",
                  root, sc, d.scale, expect);
        }
    }
    // position 0 must read RAW, not pretend to be a scale
    buildDisplay(8, 16, 0, SCALE_RAW, 7, d);
    CHECK(strcmp(d.scale, "G RAW") == 0,
          "unquantized shows \"%s\", expected \"G RAW\"", d.scale);
    printf("display: scale line covers all 12 roots x %d positions, RAW included\n",
           NUM_SCALES);
}

// Out-of-range inputs must clamp rather than write past the buffers.
static void test_clamping() {
    DisplayText d;
    buildDisplay(-50, -50, -3, -3, -9, d);
    CHECK(strcmp(d.steps, "1/2") == 0, "negative density/length rendered \"%s\"", d.steps);
    CHECK(strcmp(d.style, "BASS") == 0, "negative style rendered \"%s\"", d.style);
    CHECK(d.scale[0] == 'C', "negative root did not clamp: \"%s\"", d.scale);

    buildDisplay(99999, 99999, 99, 99, 99, d);
    CHECK(strcmp(d.steps, "16/16") == 0, "huge density/length rendered \"%s\"", d.steps);
    CHECK(strcmp(d.style, styleName(NUM_STYLES - 1)) == 0, "huge style rendered \"%s\"", d.style);
    CHECK(strnlen(d.scale, DISPLAY_LEN) < DISPLAY_LEN, "huge scale index overran");

    // a long label must truncate inside the buffer, never past it
    char buf[DISPLAY_LEN];
    int n = appendStr(buf, 0, "THIS-LABEL-IS-FAR-TOO-LONG-FOR-THE-BUFFER");
    CHECK(n == DISPLAY_LEN - 1, "appendStr wrote %d chars, expected %d",
          n, DISPLAY_LEN - 1);
    CHECK(buf[DISPLAY_LEN - 1] == '\0', "appendStr did not terminate at the limit");
    printf("display: out-of-range densities, styles, roots and long labels all clamp safely\n");
}

int main() {
    test_steps_line();
    test_style_labels();
    test_scale_line();
    test_clamping();

    if (failures) {
        printf("\n%d FAILURES\n", failures);
        return 1;
    }
    printf("\nall tests passed\n");
    return 0;
}
