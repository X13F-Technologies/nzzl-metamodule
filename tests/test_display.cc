// Native test harness for the panel display text (Task 11). The pixels are
// hardware's problem; the strings are testable here, for every seed.

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

// Every one of the 1024 seeds must render a correct, terminated, non-empty
// seed and zone label.
static void test_all_seeds_render() {
    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        DisplayText d;
        buildDisplay(seed, 3, 0, d);

        CHECK(strnlen(d.seed, DISPLAY_LEN) < DISPLAY_LEN, "seed %d: unterminated seed text", seed);
        CHECK(strnlen(d.zone, DISPLAY_LEN) < DISPLAY_LEN, "seed %d: unterminated zone text", seed);
        CHECK(strnlen(d.scale, DISPLAY_LEN) < DISPLAY_LEN, "seed %d: unterminated scale text", seed);
        CHECK(d.seed[0] && d.zone[0] && d.scale[0], "seed %d: empty display line", seed);

        int g = 0, sg = 0;
        seedKnobsFor(seed, g, sg);
        char expect[DISPLAY_LEN];
        expect[0] = 'G';
        expect[1] = char('0' + g / 10);
        expect[2] = char('0' + g % 10);
        expect[3] = ' ';
        expect[4] = 'S';
        expect[5] = char('0' + sg / 10);
        expect[6] = char('0' + sg % 10);
        expect[7] = '\0';
        CHECK(strcmp(d.seed, expect) == 0,
              "seed %d: got \"%s\", expected \"%s\"", seed, d.seed, expect);

        const char* zone = styleName(styleForSeed(seed));
        CHECK(strcmp(d.zone, zone) == 0,
              "seed %d: zone \"%s\", expected \"%s\"", seed, d.zone, zone);
    }
    printf("display: all %d seeds render a correct G##/S## and zone label\n",
           NUM_SEEDS);
}

// The zone label must actually track the three style zones.
static void test_zone_labels() {
    DisplayText d;
    buildDisplay(0, 3, 0, d);
    CHECK(strcmp(d.zone, "BASS") == 0, "group 1 shows \"%s\"", d.zone);
    buildDisplay(10 * 32, 3, 0, d);
    CHECK(strcmp(d.zone, "RAND") == 0, "group 11 shows \"%s\"", d.zone);
    buildDisplay(22 * 32, 3, 0, d);
    CHECK(strcmp(d.zone, "ARP") == 0, "group 23 shows \"%s\"", d.zone);
    printf("display: zone label follows the BASS/RAND/ARP boundaries\n");
}

// Root and scale together, including the unquantized position.
static void test_scale_line() {
    DisplayText d;
    for (int root = 0; root < 12; root++) {
        for (int sc = 0; sc < NUM_SCALES; sc++) {
            buildDisplay(0, sc, root, d);
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
    buildDisplay(0, SCALE_RAW, 7, d);
    CHECK(strcmp(d.scale, "G RAW") == 0,
          "unquantized shows \"%s\", expected \"G RAW\"", d.scale);
    printf("display: scale line covers all 12 roots x %d positions, RAW included\n",
           NUM_SCALES);
}

// Out-of-range inputs must clamp rather than write past the buffers.
static void test_clamping() {
    DisplayText d;
    buildDisplay(-50, -3, -9, d);
    CHECK(strcmp(d.seed, "G01 S01") == 0, "negative seed rendered \"%s\"", d.seed);
    CHECK(d.scale[0] == 'C', "negative root did not clamp: \"%s\"", d.scale);

    buildDisplay(99999, 99, 99, d);
    CHECK(strcmp(d.seed, "G32 S32") == 0, "huge seed rendered \"%s\"", d.seed);
    CHECK(strnlen(d.scale, DISPLAY_LEN) < DISPLAY_LEN, "huge scale index overran");

    // a long label must truncate inside the buffer, never past it
    char buf[DISPLAY_LEN];
    int n = appendStr(buf, 0, "THIS-LABEL-IS-FAR-TOO-LONG-FOR-THE-BUFFER");
    CHECK(n == DISPLAY_LEN - 1, "appendStr wrote %d chars, expected %d",
          n, DISPLAY_LEN - 1);
    CHECK(buf[DISPLAY_LEN - 1] == '\0', "appendStr did not terminate at the limit");
    printf("display: out-of-range seeds, roots and long labels all clamp safely\n");
}

int main() {
    test_all_seeds_render();
    test_zone_labels();
    test_scale_line();
    test_clamping();

    if (failures) {
        printf("\n%d FAILURES\n", failures);
        return 1;
    }
    printf("\nall tests passed\n");
    return 0;
}
