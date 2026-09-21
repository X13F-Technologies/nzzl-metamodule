# NZZL — Specification As Built

**Status:** feature-complete, awaiting a full manual pass in VCV Rack.
**Last updated:** 2026-09-21
**Source spec:** [HANDOFF_SPEC.md](HANDOFF_SPEC.md) — the original requirements, unedited.
**Design reasoning:** [DESIGN.md](DESIGN.md) — why each decision was made.
**Acceptance procedure:** [SYSTEM_TEST_GUIDE.md](SYSTEM_TEST_GUIDE.md) — 144 manual tests.

This document describes the module **as it actually exists**, in the same
order as the original handoff requirements so the two can be read side by
side. Where the build differs from the handoff, the difference is marked
**△** inline and explained in full in [§ Differences from the original
requirements](#differences-from-the-original-requirements) at the end.

> **Keep this file current.** When behaviour changes, update the relevant
> section here *and* the differences table. `HANDOFF_SPEC.md` is frozen — it
> records what was asked for, not what was built, and editing it would
> destroy the only record of the original intent.

---

## Context

NZZL is a deterministic generative bassline/sequencer for the **4ms
MetaModule**. All output is derived from a seed; there is no runtime
randomness during playback. The only non-deterministic moment in the module
is the instant a RESEED trigger fires.

△ **It is built against the MetaModule SDK's VCV Rack adaptor, not as a
native CoreProcessor plugin.** The same `src/nzzl.cc` compiles for VCV Rack 2
(local development) and for MetaModule ARM (CI cross-compile).

---

## Module identity

| | |
|---|---|
| Name / slug | NZZL |
| Panel width | 16 HP |
| VCV Rack output | `plugin.dylib` |
| MetaModule output | `NZZL.mmplugin` |
| Repo | https://github.com/X13F-Technologies/nzzl-metamodule |

---

## Jacks

### Inputs

| Jack | Behaviour |
|---|---|
| CLOCK | Advances the sequencer one step per rising edge (after CLOCK DIV). Schmitt trigger at 0.1 V / 2.0 V. |
| RUN | △ **Trigger, not a gate.** A rising edge toggles running/stopped. Position is held, never reset. Unpatched = always running. |
| RESEED | Rising edge picks a new random seed and drives GROUP/SUBGROUP to match. Edge-triggered only; holding it high fires once. |
| CV SCALE | **Override.** 0–10 V spans all 13 scale positions. Knob ignored while patched. |
| CV SEED | **Offset.** Added to the knob seed; 10 V spans all 1024 seeds; wraps at both ends. |
| CV ROOT | **Override.** V/oct — 1 V = 12 semitones — snapped to whole semitones and wrapped to 0–11. Knob ignored while patched. |
| CV SLIDE | Summed with the step's own slide flag, then scaled by the SLIDE knob: `knob × clamp01(flag + cv)`. 10 V = full. |

Every input is a no-op when unpatched.

### Outputs

| Jack | Behaviour |
|---|---|
| CV PITCH | V/Oct. Latched at gate onset, re-quantized every sample so knob turns re-pitch the held note immediately. |
| GATE | 0 V / 10 V. Duration = one of the pattern's three note lengths × GATE knob × clock period × clock divide. |
| VELOCITY | 0–10 V. One of three layers, scaled by ACCENT. Sample-and-held through silent steps. |

---

## Knobs

Eleven controls. △ Two are new (GATE, ACCENT) and one from the original
requirements is gone (SCALE LOCK).

| Knob | Range | Default | Behaviour |
|---|---|---|---|
| GROUP | 1–32 | 1 | Seed group. Zones: **1–10 Bassline, 11–21 Random, 22–32 Arp**. |
| SUBGROUP | 1–32 | 1 | Seed within group. In the Arp zone: **1–8 up, 9–16 down, 17–24 up-down, 25–32 down-up**. |
| DENSITY | 1–16 | 8 | Step threshold: a step plays when `weight <= density`. Exactly N of 16 steps play at density N. |
| LENGTH | 2–16 | 16 | Loop length. |
| CLOCK DIV | 1–16 | 1 | Divides the incoming clock. |
| OCTAVE RANGE | 1–5 | 2 | How many octaves the sequence spans. |
| ROOT | C–B | C | Key centre. |
| SCALE | 0–12 | Natural Minor | △ **13 positions. Position 0 is unquantized**; 1–12 are the twelve scales. |
| SLIDE | 0.0–1.0 | 0 | Attenuator over the seed-derived slide. CCW = no slide regardless of seed or CV. |
| GATE | 1–200 % | 100 % | △ **New.** Scales the pattern's three note lengths together. |
| ACCENT | 0–100 % | 100 % | △ **New.** Scales the contrast between the three velocity layers. |

---

## Seed system

**1024 seeds** = 32 groups × 32 subgroups.

```
seed_index = (group - 1) * 32 + (subgroup - 1)     // 0–1023
```

The mapping is a proven bijection in both directions, so any pattern the
module can produce is reachable by hand from the knobs.

Per-step values generated deterministically at pattern initialisation, each
from its **own salted xorshift32 stream** so a change to one attribute never
reshuffles the others:

| Value | Range |
|---|---|
| `weight` | a permutation of 1–16 |
| `pitchIndex` | 0–15, mapped into the scale at playback |
| `gateIndex` | △ 0–2 — which of the pattern's three note lengths |
| `velLayer` | △ 0–2 — off / low / high |
| `slide` | boolean, weighted by style |
| `octaveJump` | △ explicit octave offset, or −1 meaning "spread across the range" |
| `octaveRaw` | 0.0–1.0 |

Plus, per pattern: △ **three gate lengths** — short 0.12–0.30, mid 0.35–0.60,
long 0.65–0.95 of a clock period.

**RESEED** picks a new seed from a non-deterministic source, regenerates the
pattern, and **writes the GROUP and SUBGROUP knobs** so what you hear stays
reproducible by hand. The display flashes amber for 350 ms.

**Patch save:** the seed rides on the GROUP/SUBGROUP params, which the host
saves — including after a reseed, because reseed writes the knobs rather than
shadowing them. `dataToJson` additionally stores the run state. △ The step
position is deliberately *not* saved.

---

## Density logic

```cpp
step_is_active = (step.weight <= density_knob_value)
```

Weights are a **permutation of 1–16**, not random draws, so density N
activates exactly N of 16 steps on every seed, the response is linear, and
density 1 always plays something. Same seed + same density = identical
output, always. No probability at playback.

*(With LENGTH < 16 the count inside the window is no longer exactly N. This
is accepted; the knob still feels monotonic.)*

---

## Style behaviour

Style comes from the GROUP zone, not a separate knob.

### Bassline — groups 1–10 △ **303-style acid**

Not "notes on beats 1 and 3". A TB-303 line is runs of sixteenths broken by
rests, with slides welding notes together, the root hammered far more than
anything else, and the odd octave jump.

**Rhythm.** Each seed builds its own priority order of step positions, and
weights 1–16 are laid along it:

1. the downbeat leads — 85 % of seeds
2. four runs of 2–4 consecutive sixteenths at seed-chosen start points
3. whatever is left, in order

Laying 1–16 along a permutation of positions is still a permutation, so
DENSITY stays exactly linear.

**Pitch.** Root-dominated vocabulary, nothing outside it:

| Degree | Root | Fifth | Third | Seventh | Fourth |
|---|---|---|---|---|---|
| Weight | 55 % | 15 % | 12 % | 10 % | 8 % |

**Octave.** +1 jumps on 18 % of notes, and they stay +1 at any OCTAVE RANGE.

**Ties.** 32 % of steps. **Gates.** Short and mid only — ties do the work
long gates would otherwise do. **Accents.** 28 % on the high layer.

Measured across the zone: 63 % of notes are immediately followed by another
(random zone: 34 %), 55 % root, and **100 distinct rhythms across 320 seeds at
DENSITY 4**.

### Random — groups 11–21

The unshaped generator, untouched. Even distribution across the scale,
moderate octave spread, all three gate lengths, varied everything.

This zone is **bit-identical to what the generator produced before styles
existed**, and a test asserts it for all 352 of its seeds.

### Arp — groups 22–32

The sixteen pitches are sorted, then read out in the direction the SUBGROUP
knob selects: △ **up, down, up-down, down-up**. Even subdivisions, one gate
length throughout, no glide.

---

## Gate and Accent △ **new**

A pattern generates **three note lengths**; each step indexes one; the GATE
knob (1–200 %) scales all three together.

The interesting consequence is at the top of the range: at 200 % the long
notes exceed a step and **tie**, while the short ones are still only ~0.6 of
a step and stay staccato. The knob therefore *morphs* a pattern from staccato
toward partly legato rather than flipping everything at once.

**ACCENT** works the same way over three velocity layers (off 0.22, low 0.55,
high 1.0). At 0 % every note sits at the same mid level; at 100 % the layers
are fully apart — 7.8 V of measured spread at the output.

---

## Rhythm behaviour

All active steps land on even clock subdivisions — no swing, no syncopation
within a step. The seed determines *which* steps are active, not their
timing. Clock period is estimated from the time between accepted rising
edges, clamped to 10 ms – 4 s.

---

## Slide / portamento

Each step carries a seed-derived slide flag. When a step slides:

- pitch **glides linearly** from the previous note to this one
- △ **the gate is held through into the next note** — a slide is a *tie*

Linear rather than exponential so the glide arrives *exactly* (the note ends
up precisely in tune), cannot overshoot, and "reaches its target within the
slide time" is a property a test can assert.

Glide duration = `SLIDE knob × 0.9 × step duration`, so it is tempo-relative
and always finishes before the next note starts.

The SLIDE knob is an attenuator over `flag + CV`, so CCW kills all glide
however much CV is arriving, and a patched CV can put glide on steps the seed
never flagged.

---

## Pitch and scale system

**Quantized (SCALE positions 1–12):**

```
V = octave + (root + interval) / 12
```

The octave spread is exactly the OCTAVE RANGE knob; ROOT adds a further
0–11/12 V of transposition on top. **ROOT only ever adds whole semitones** —
transposing can never knock a note off the grid or out of key, and CV ROOT
snaps for the same reason.

**Unquantized (SCALE position 0):** each octave divided into 16 equal steps
(75 cents), 0–5 V at OCTAVE RANGE 5. The module has only 16 discrete pitch
indices, so "raw" means *off the semitone grid*, not *smooth*.

**Degree mapping** is proportional, not modulo: `degree = pitchIndex ×
noteCount / 16`. Monotonic, so the step that was the pattern's highest note
stays its highest note in every scale — melodic contour survives a scale
sweep.

### The thirteen SCALE positions △

| # | | # | |
|---|---|---|---|
| **0** | **Unquantized** | 7 | Harmonic Minor |
| 1 | Major | 8 | Melodic Minor |
| 2 | Natural Minor *(default)* | 9 | Pentatonic Major |
| 3 | Dorian | 10 | Pentatonic Minor |
| 4 | Phrygian | 11 | Chromatic |
| 5 | Mixolydian | 12 | Whole Tone |
| 6 | Lydian | | |

**Index order is an on-disk contract — append only, never reorder.** A saved
patch stores the index; inserting a scale mid-list would retune every patch
in existence.

---

## Run input

△ **Trigger, not a gate.** A rising edge toggles running/stopped. Stopping
holds the step position and never resets. Unpatched = always running — and
unpatching while stopped resumes, so a removed cable can never leave the
module stuck.

---

## Display

Three lines:

| Line | Content | Example |
|---|---|---|
| 1 | Group and subgroup | `4 . 7` |
| 2 | Style zone | `BASS`, `RAND`, `ARP↑`, `ARP↓`, `ARP↕`, `ARP↔` |
| 3 | Root and scale | `C MINOR`, or `C RAW` when unquantized |

Lines 1 and 3 follow the **effective** value, CV included. A reseed flashes
the display amber for 350 ms.

The text is built with no heap, no `<string>` and no stdio, and all 1024
seeds are verified. Only the rendering is hardware's problem, and it is
**unverified** — no physical MetaModule has run this yet.

---

## Architecture

```
src/
├── rng.hh       Xorshift32 PRNG. Pure.
├── pattern.hh   Seed → Pattern, style zones, seed↔knob mapping. Pure.
├── scales.hh    Scale tables, degree/octave mapping, quantization. Pure.
├── engine.hh    Realtime state machine: triggers, clock, gate, slide. Pure.
├── cv.hh        CV input mapping. Pure.
├── display.hh   Panel text. Pure.
├── nzzl.cc      Thin adapter: params/jacks ↔ the headers above.
└── plugin.cc    Registration.
```

△ The file layout differs from the one the requirements sketched (`nzzl.hh`
was never needed; five more pure headers exist instead).

**The rule that shapes everything:** anything deterministic lives in a pure
header with no `rack.hpp` include, so the native harness can compile and test
it without Rack or the ARM toolchain. `nzzl.cc` holds no decisions.

Six test suites under `tests/`, run by `./tests/run_tests.sh` and by CI on
every branch.

---

## Build and deploy

```bash
# native tests — no Rack, no ARM toolchain needed
./tests/run_tests.sh

# VCV Rack 2 (local development)
cmake --build build-vcv && cmake --install build-vcv

# MetaModule ARM — CI builds .mmplugin on EVERY branch △
git push
gh run download --repo X13F-Technologies/nzzl-metamodule
```

ARM toolchain pinned to GCC 12.3. C++20. No iostream/fstream/stringstream,
no exceptions.

---

## Differences from the original requirements

Everything below differs from [HANDOFF_SPEC.md](HANDOFF_SPEC.md). Each is a
deliberate decision with a reason; none is an oversight. **Anything not
listed here follows the original requirements.**

### A. Changes the user asked for

| # | Requirement said | Built | Why | When |
|---|---|---|---|---|
| A1 | RUN: "Gate high = runs, gate low = holds" | **Toggle trigger** — rising edge flips running/stopped | Works with a momentary button; a level needs a latching gate source | Early |
| A2 | SCALE LOCK as a separate on/off toggle beside a 12-scale knob | **Position 0 of the SCALE knob** is unquantized; 13 positions total | One control answers "how are these notes pitched?" instead of two that can disagree. Frees a panel position, and lets CV SCALE reach raw mode without a second jack. **Costs** the ability to A/B back to your scale | 2026-09-20 |
| A3 | Bassline: "notes cluster on beats 1 and 3, sparse off-beat, gate mostly short" | **303-style acid**: runs of sixteenths, ties, root hammering, octave jumps | The requirement describes a dub/house bassline. The module is aimed at acid. User: *"ideally id want the bass algos to simulate 303 style sequences"* | 2026-09-20 |
| A4 | Gate length: per-step, seed-derived. Velocity: per-step, seed-derived | **Three note lengths + a GATE knob (1–200 %)**; **three velocity layers + an ACCENT knob (0–100 %)** | A small audible set you can scale beats sixteen unrelated numbers you cannot. Also gives the 303 zone its accent behaviour | 2026-09-20 |

### B. Engineering decisions the requirements left open

| # | Requirement said | Built | Why |
|---|---|---|---|
| B1 | "Native MetaModule CoreProcessor plugin (not a VCV Rack port)" | Built against the **SDK's VCV Rack adaptor**; one source tree compiles for both targets | Lets the module be developed and heard in VCV Rack on a Mac, with hardware builds from the same source. Without it every iteration would need an SD card and the physical module |
| B2 | A file layout with `nzzl.hh` | **Five more pure headers** (`pattern`, `scales`, `engine`, `cv`, `display`); no `nzzl.hh` | Everything deterministic lives in a header with no Rack dependency, so it can be machine-tested. This is why clock, gate, slide and quantization all have automated coverage |
| B3 | "Slide time is constant (derived from clock tempo feel)" | `SLIDE knob × 0.9 × step duration`, **linear ramp** | Tempo-relative, so the feel survives a tempo change. Linear arrives exactly, cannot overshoot, and is assertable. The 0.9 cap guarantees a glide finishes before the next note |
| B4 | Slide = "CV output glides from previous pitch to current pitch" | Glide **plus the gate is held into the next note** | On a real 303 the slide button ties the notes. A glide across a gap is two notes with portamento, which is not the sound |
| B5 | Seed values include "step octave offset (0 to octave_range-1)" | `octaveJump` (explicit) **or** `octaveRaw` (spread across the range), per style | A 303 octave-up must stay +1 whether OCTAVE RANGE is 2 or 5. A proportional value cannot express that |
| B6 | CI "runs on every push to `main`" | CI builds **every branch**, and runs the native tests | The ARM cross-compile is the only proof a change works on hardware. That has to happen *before* a merge, not after |

### C. Things the requirements specified that were built wrong, then fixed

All four were discovered on 2026-09-20, when the requirements document was
finally copied into the repo. Before that, three sessions had been building
against a one-line paraphrase of it.

| # | Requirement | Was | Now |
|---|---|---|---|
| C1 | Zones: 1–10 / **11–21** / **22–32** | 1–10 / 11–22 / 23–32 | Correct |
| C2 | Arp subgroup zones: 1–8 up, 9–16 down, 17–24 up-down, 25–32 down-up | **Did not exist** — direction was chosen at random per 4-step block | Implemented, each with its own display arrow |
| C3 | Twelve named scales in a given order | Wrong list — Phrygian Dominant and Blues present, Pentatonic Major and Whole Tone missing | The requirement's twelve, in its order, at positions 1–12 |
| C4 | CV SCALE and CV ROOT are **overrides**; CV SEED is an **offset**; CV SLIDE is summed **then** attenuated | All four were offsets; SLIDE was `knob + cv` | Each matches its stated semantics |

### D. A bug the requirements did not cause, worth recording

The first bassline implementation pulled the four lowest weights onto a fixed
beat-priority table. Those four weights are exactly what DENSITY 4 plays, so
**all 320 bass seeds played the same four positions in the bar** — 320 seeds,
one rhythm.

The test that was supposed to guard it measured *"percentage of notes landing
on a beat"*, which a fully collapsed zone scores **100 %** on. The
measurement confirmed the design instead of interrogating it.

The replacement test counts **distinct active-step sets across the whole
zone** and asserts a floor at every density — and also asserts that bass
stays *more* constrained than the random zone, so over-correcting fails too.

| DENSITY | 1 | 2 | 3 | **4** | 5 | 6 |
|---|---|---|---|---|---|---|
| Distinct bass rhythms (320 seeds) | 16 | 29 | 46 | **100** | 145 | 189 |
| Distinct random rhythms (352 seeds) | 16 | 111 | 265 | 317 | 339 | 346 |

---

## Open questions

Three things want a human opinion rather than a test:

1. **Was folding SCALE LOCK into the SCALE knob right?** It costs the ability
   to A/B back to your scale. Test T5.11b.
2. **Are the 303 shaping rates right?** Run count, run length, root
   dominance, tie rate and octave-jump rate are all constants at the top of
   `pattern.hh`. They were set by reasoning, not by listening. Test T8.15.
3. **Are the GATE and ACCENT ranges right?** 1–200 % and 0–100 %. Test G.10.

And one thing genuinely blocked: **the display's rendering on hardware is
unverified.** The text content is tested for all 1024 seeds; whether
`MetaModule::VCVTextDisplay` paints it needs the physical module.
