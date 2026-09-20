// Native test harness for NZZL's pitch quantizer — no Rack/MetaModule deps.
// Build & run:  ./tests/run_tests.sh

#include "../src/scales.hh"
#include <cstdio>
#include <cmath>

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

static const float EPS = 1e-4f;

// Independently written reference tables. Deliberately NOT shared with
// scales.hh — a typo in the shipping table has to disagree with this one.
struct RefScale { const char* name; int n; int iv[12]; };
static const RefScale kRef[NUM_SCALES] = {
    { "RAW",      0, {} },                        // index 0 is not a scale
    { "CHROM",   12, {0,1,2,3,4,5,6,7,8,9,10,11} },
    { "MAJOR",    7, {0,2,4,5,7,9,11} },          // W W H W W W H
    { "MINOR",    7, {0,2,3,5,7,8,10} },          // W H W W H W W
    { "DORIAN",   7, {0,2,3,5,7,9,10} },          // minor with major 6th
    { "PHRYG",    7, {0,1,3,5,7,8,10} },          // minor with b2
    { "PHR DOM",  7, {0,1,4,5,7,8,10} },          // phrygian with major 3rd
    { "LYDIAN",   7, {0,2,4,6,7,9,11} },          // major with #4
    { "MIXO",     7, {0,2,4,5,7,9,10} },          // major with b7
    { "HARM MIN", 7, {0,2,3,5,7,8,11} },          // minor with major 7th
    { "MEL MIN",  7, {0,2,3,5,7,9,11} },          // minor with major 6+7
    { "MIN PENT", 5, {0,3,5,7,10} },
};

// The first real scale. Index 0 is RAW and has no intervals to check.
static const int FIRST_SCALE = 1;

static bool scaleContains(const Scale& sc, int semitone) {
    for (int i = 0; i < sc.noteCount; i++)
        if (sc.intervals[i] == semitone) return true;
    return false;
}

// Every shipped interval table matches music theory, is ascending,
// duplicate-free, and stays inside one octave.
static void test_scale_tables() {
    // Index 0 is the unquantized slot, not a scale.
    CHECK(isUnquantized(SCALE_RAW), "index 0 is not flagged unquantized");
    CHECK(getScale(SCALE_RAW).noteCount == 0, "the RAW slot must have no intervals");
    for (int s = FIRST_SCALE; s < NUM_SCALES; s++)
        CHECK(!isUnquantized(s), "scale index %d wrongly flagged unquantized", s);

    for (int s = FIRST_SCALE; s < NUM_SCALES; s++) {
        const Scale& sc = getScale(s);
        const RefScale& ref = kRef[s];

        CHECK(sc.noteCount == ref.n,
              "scale %d (%s) noteCount %d != %d", s, sc.name, sc.noteCount, ref.n);
        CHECK(sc.name != nullptr && sc.name[0] != '\0', "scale %d has no name", s);

        for (int i = 0; i < sc.noteCount; i++) {
            CHECK(sc.intervals[i] == ref.iv[i],
                  "scale %d (%s) interval %d = %d, expected %d",
                  s, sc.name, i, sc.intervals[i], ref.iv[i]);
            CHECK(sc.intervals[i] >= 0 && sc.intervals[i] < 12,
                  "scale %d (%s) interval %d = %d out of octave",
                  s, sc.name, i, sc.intervals[i]);
            if (i > 0)
                CHECK(sc.intervals[i] > sc.intervals[i - 1],
                      "scale %d (%s) not strictly ascending at %d", s, sc.name, i);
        }
        CHECK(sc.intervals[0] == 0, "scale %d (%s) does not start on the root", s, sc.name);
    }
    printf("scale tables: index 0 is RAW; all %d tables match reference, "
           "ascending, in-octave\n", NUM_SCALES - FIRST_SCALE);
}

// Out-of-range scale indices clamp instead of reading past the table.
static void test_scale_index_clamping() {
    CHECK(getScale(-5).noteCount   == getScale(0).noteCount, "negative index did not clamp low");
    CHECK(getScale(999).noteCount  == getScale(NUM_SCALES - 1).noteCount, "high index did not clamp");
    printf("scale index clamping: out-of-range indices clamp safely\n");
}

// pitchIndex -> degree: in range, monotonic (contour preserved), and every
// degree of every scale is reachable (no dead notes).
static void test_degree_mapping() {
    for (int s = FIRST_SCALE; s < NUM_SCALES; s++) {
        const Scale& sc = getScale(s);
        bool reached[MAX_SCALE_NOTES] = {};
        int prev = -1;
        for (int idx = 0; idx < MAX_STEPS; idx++) {
            int d = degreeForIndex(idx, sc.noteCount);
            CHECK(d >= 0 && d < sc.noteCount,
                  "scale %s index %d -> degree %d out of range", sc.name, idx, d);
            CHECK(d >= prev,
                  "scale %s degree mapping not monotonic at index %d", sc.name, idx);
            prev = d;
            reached[d] = true;
        }
        for (int d = 0; d < sc.noteCount; d++)
            CHECK(reached[d], "scale %s degree %d unreachable from any pitchIndex",
                  sc.name, d);
        CHECK(degreeForIndex(-3, sc.noteCount) == 0, "%s: negative index did not clamp", sc.name);
        CHECK(degreeForIndex(99, sc.noteCount) == sc.noteCount - 1,
              "%s: high index did not clamp", sc.name);
    }
    printf("degree mapping: monotonic, in range, fully reachable for all scales\n");
}

// octaveRaw -> octave never exceeds the knob setting, for any raw value.
static void test_octave_range() {
    for (int range = 1; range <= 5; range++) {
        for (int k = 0; k < 1000; k++) {
            float raw = float(k) / 1000.f;          // matches generator's [0,1)
            int o = octaveForRaw(raw, range);
            CHECK(o >= 0 && o < range,
                  "range %d raw %f -> octave %d out of bounds", range, raw, o);
        }
        // boundary / defensive values
        CHECK(octaveForRaw(0.f, range) == 0, "range %d: raw 0 -> nonzero octave", range);
        CHECK(octaveForRaw(1.f, range) == range - 1, "range %d: raw 1.0 did not clamp", range);
        CHECK(octaveForRaw(-1.f, range) == 0, "range %d: negative raw did not clamp", range);
    }
    CHECK(octaveForRaw(0.5f, 0) == 0, "octaveRange 0 did not clamp to a single octave");
    printf("octave range: octave stays within the knob setting, all ranges\n");
}

// Every quantized note lands on a real semitone that belongs to the scale,
// across all 1024 seeds x 12 scales x 12 roots x 5 octave ranges.
static void test_quantized_notes_in_scale() {
    long checked = 0;
    for (int seed = 0; seed < 1024; seed++) {
        StepData st[MAX_STEPS];
        generatePattern(seed, st);
        for (int s = FIRST_SCALE; s < NUM_SCALES; s++) {
            const Scale& sc = getScale(s);
            for (int root = 0; root < 12; root++) {
                for (int range = 1; range <= 5; range++) {
                    QuantizeParams p{ s, root, range };
                    for (int i = 0; i < MAX_STEPS; i++) {
                        float v = pitchVoltage(st[i], p);

                        // lands on an exact semitone
                        float semiF = v * 12.f;
                        int   semi  = int(std::lround(semiF));
                        CHECK(std::fabs(semiF - float(semi)) < EPS,
                              "seed %d scale %s root %d: %f V is not a semitone",
                              seed, sc.name, root, v);

                        // belongs to the scale, relative to the root
                        int rel = ((semi - root) % 12 + 12) % 12;
                        CHECK(scaleContains(sc, rel),
                              "seed %d step %d scale %s root %d: %d is not in scale",
                              seed, i, sc.name, root, rel);

                        // octave component respects the knob
                        int oct = (semi - root) / 12;
                        CHECK(oct >= 0 && oct < range,
                              "seed %d scale %s: octave %d exceeds range %d",
                              seed, sc.name, oct, range);
                        checked++;
                    }
                }
            }
        }
    }
    printf("quantized pitch: %ld notes all in-scale and in-range\n", checked);
}

// ROOT is a pure transpose: root r must equal root 0 shifted by r/12 V.
static void test_root_transposes() {
    for (int s = FIRST_SCALE; s < NUM_SCALES; s++) {
        for (int idx = 0; idx < MAX_STEPS; idx++) {
            for (int range = 1; range <= 5; range++) {
                float raw = 0.37f;
                QuantizeParams base{ s, 0, range };
                float v0 = quantizedVoltage(idx, raw, base);
                for (int root = 0; root < 12; root++) {
                    QuantizeParams p{ s, root, range };
                    float v = quantizedVoltage(idx, raw, p);
                    CHECK(std::fabs(v - (v0 + float(root) / 12.f)) < EPS,
                          "scale %d idx %d root %d: %f != %f + %d/12",
                          s, idx, root, v, v0, root);
                }
            }
        }
    }
    printf("root: transposes the whole pattern exactly, all scales\n");
}


// Shifting ROOT must never knock a note off the semitone grid or out of key.
// Root is a whole-semitone transpose, so for every root the pattern must use
// the SAME scale degrees — only the key moves.
static void test_root_stays_quantized() {
    for (int seed = 0; seed < 256; seed++) {
        StepData st[MAX_STEPS];
        generatePattern(seed, st);
        for (int sc = FIRST_SCALE; sc < NUM_SCALES; sc++) {
            const Scale& scale = getScale(sc);
            for (int range = 1; range <= 5; range++) {
                // degrees played at root C — the baseline to compare against
                int baseRel[MAX_STEPS];
                {
                    QuantizeParams p{ sc, 0, range };
                    for (int i = 0; i < MAX_STEPS; i++) {
                        int semi = int(std::lround(pitchVoltage(st[i], p) * 12.f));
                        baseRel[i] = ((semi - 0) % 12 + 12) % 12;
                    }
                }
                for (int root = 1; root < 12; root++) {
                    QuantizeParams p{ sc, root, range };
                    for (int i = 0; i < MAX_STEPS; i++) {
                        float v = pitchVoltage(st[i], p);

                        // still exactly on the semitone grid
                        float semiF = v * 12.f;
                        int   semi  = int(std::lround(semiF));
                        CHECK(std::fabs(semiF - float(semi)) < EPS,
                              "seed %d scale %s root %d: %f V left the semitone grid",
                              seed, scale.name, root, v);

                        // still in key, and on the SAME degree as at root C
                        int rel = ((semi - root) % 12 + 12) % 12;
                        CHECK(scaleContains(scale, rel),
                              "seed %d scale %s root %d step %d: %d out of key",
                              seed, scale.name, root, i, rel);
                        CHECK(rel == baseRel[i],
                              "seed %d scale %s root %d step %d: degree changed "
                              "from %d to %d — root did more than transpose",
                              seed, scale.name, root, i, baseRel[i], rel);
                    }
                }
            }
        }
    }
    // Unquantized mode: root must still be an exact semitone offset, so the
    // raw pattern shifts by a musical interval rather than drifting.
    for (int idx = 0; idx < MAX_STEPS; idx++) {
        StepData st{}; st.pitchIndex = idx; st.octaveRaw = 0.62f;
        QuantizeParams base{ SCALE_RAW, 0, 3 };
        float v0 = pitchVoltage(st, base);
        for (int root = 0; root < 12; root++) {
            QuantizeParams p{ SCALE_RAW, root, 3 };
            CHECK(std::fabs(pitchVoltage(st, p) - (v0 + float(root) / 12.f)) < EPS,
                  "unquantized: root %d is not an exact semitone offset", root);
        }
    }
    printf("root quantization: every root keeps notes on-grid, in key, "
           "and on the same degrees\n");
}

// Melodic contour survives a scale change: higher pitchIndex is never a
// lower note within the same octave.
static void test_contour_monotonic() {
    for (int s = FIRST_SCALE; s < NUM_SCALES; s++) {
        for (int root = 0; root < 12; root += 5) {
            QuantizeParams p{ s, root, 3 };
            float prev = -1e9f;
            for (int idx = 0; idx < MAX_STEPS; idx++) {
                float v = quantizedVoltage(idx, 0.f, p);   // fix octave at 0
                CHECK(v >= prev - EPS,
                      "scale %d root %d: contour dips at index %d (%f < %f)",
                      s, root, idx, v, prev);
                prev = v;
            }
        }
    }
    printf("contour: pitch is monotonic in pitchIndex for every scale\n");
}

// Scale lock OFF: unquantized, spans 0–5 V at max octave range, and is
// genuinely off the semitone grid (16 divisions/octave = 75 cents).
static void test_raw_mode() {
    float lo = 1e9f, hi = -1e9f;
    int offGrid = 0, total = 0;
    for (int seed = 0; seed < 1024; seed++) {
        StepData st[MAX_STEPS];
        generatePattern(seed, st);
        QuantizeParams p{ SCALE_RAW, 0, 5 };       // unquantized, root 0, 5 octaves
        for (int i = 0; i < MAX_STEPS; i++) {
            float v = pitchVoltage(st[i], p);
            CHECK(v >= 0.f && v < 5.f, "seed %d raw voltage %f outside 0–5 V", seed, v);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            float semiF = v * 12.f;
            if (std::fabs(semiF - std::lround(semiF)) > 0.02f) offGrid++;
            total++;
        }
    }
    CHECK(lo < 0.05f, "raw mode never reaches the bottom of its range (min %f)", lo);
    CHECK(hi > 4.5f,  "raw mode never reaches the top of its range (max %f)", hi);
    CHECK(offGrid > total / 2,
          "raw mode is not audibly unquantized: only %d of %d notes off-grid",
          offGrid, total);
    // position 0 must actually differ from a real scale
    QuantizeParams scale{ 3, 0, 5 }, raw{ SCALE_RAW, 0, 5 };
    int differs = 0;
    for (int idx = 0; idx < MAX_STEPS; idx++) {
        StepData st{}; st.pitchIndex = idx; st.octaveRaw = 0.4f;
        if (std::fabs(pitchVoltage(st, scale) - pitchVoltage(st, raw)) > EPS)
            differs++;
    }
    CHECK(differs > 8, "scale position 0 barely differs from a scale (%d of 16)", differs);

    // quantizedVoltage must never quantize against the empty RAW table
    for (int idx = 0; idx < MAX_STEPS; idx++) {
        StepData st{}; st.pitchIndex = idx; st.octaveRaw = 0.4f;
        CHECK(std::fabs(quantizedVoltage(idx, 0.4f, raw) - pitchVoltage(st, raw)) < EPS,
              "quantizedVoltage did not fall back to raw at index %d", idx);
    }
    printf("raw mode: scale position 0 is unquantized, 0–5 V, off-grid (%d/%d notes)\n",
           offGrid, total);
}

// Same seed + same params must always give the same voltage.
static void test_pitch_determinism() {
    for (int seed = 0; seed < 1024; seed++) {
        StepData a[MAX_STEPS], b[MAX_STEPS];
        generatePattern(seed, a);
        generatePattern(seed, b);
        QuantizeParams p{ seed % NUM_SCALES, seed % 12, 1 + (seed % 5) };
        for (int i = 0; i < MAX_STEPS; i++)
            CHECK(pitchVoltage(a[i], p) == pitchVoltage(b[i], p),
                  "seed %d step %d pitch not deterministic", seed, i);
    }
    printf("pitch determinism: identical voltages for identical seed+params\n");
}

int main() {
    test_scale_tables();
    test_scale_index_clamping();
    test_degree_mapping();
    test_octave_range();
    test_quantized_notes_in_scale();
    test_root_transposes();
    test_root_stays_quantized();
    test_contour_monotonic();
    test_raw_mode();
    test_pitch_determinism();

    if (failures) {
        printf("\n%d FAILURES\n", failures);
        return 1;
    }
    printf("\nall tests passed\n");
    return 0;
}
