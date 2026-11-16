# NZZL — Specification As Built

**Status:** feature-complete, awaiting a full manual pass in VCV Rack.
**Last updated:** 2026-11-16 (item 16, rules replace seeds, built)
**Source spec:** [HANDOFF_SPEC.md](HANDOFF_SPEC.md) — the original requirements, unedited.
**Design reasoning:** [DESIGN.md](DESIGN.md) — why each decision was made.
**Acceptance procedure:** [SYSTEM_TEST_GUIDE.md](SYSTEM_TEST_GUIDE.md) — 176 manual tests.

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

NZZL is a generative bassline/sequencer for the **4ms MetaModule**. △ **There
are no seeds** (item 16): every Generate — the RESEED jack or the RANDOM
button — rolls a fresh pattern, shaped by rules and precomputed for every
STYLE position, and the roll is saved with the patch. There is no runtime
randomness during playback; the only non-deterministic moment in the module
is the instant of a Generate.

△ **It is built against the MetaModule SDK's VCV Rack adaptor, not as a
native CoreProcessor plugin.** The same `src/nzzl.cc` compiles for VCV Rack 2
(local development) and for MetaModule ARM (CI cross-compile).

---

## Module identity

| | |
|---|---|
| Name / slug | NZZL |
| Panel width | 20 HP |
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
| RESEED | △ **Generate.** Rising edge rolls a fresh pattern (every STYLE row), which starts when the loop next wraps. Edge-triggered only; holding it high fires once. |
| CV SCALE | **Override.** 0–10 V spans all scale positions. Knob ignored while patched. |
| CV DENSITY | △ **Was CV SEED** (item 16). **Offset** added to the DENSITY knob; 10 V spans the full 1–16 range; **clamped** at both ends. |
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

Fourteen knobs and one button. △ Seven controls are new (STYLE, SHAPE,
PITCH OFFSET, SHIFT, SWING, GATE, ACCENT, plus the RANDOM button) and three
from the original requirements are gone (SCALE LOCK; GROUP and SUBGROUP,
item 16). SUBGROUP's panel position is held for PITCH LENGTH (item 7).

| Knob | Range | Default | Behaviour |
|---|---|---|---|
| STYLE | 6 detents | BASS | △ **New (item 16), replaces GROUP / SUBGROUP.** **BASS, RAND, ARP↑, ARP↓, ARP↕, ARP↔.** Each position is a row of the current roll: switching is instant, returning gives the same pattern until the next Generate. A change lands on the loop wrap. No CV. |
| DENSITY | 1–16 | 8 | Step threshold: a step plays when `weight <= density`. Exactly N of 16 steps play at density N, nested. CV DENSITY adds to it. |
| LENGTH | 2–16 | 16 | Loop length. |
| CLOCK DIV | 1–16 | 1 | Divides the incoming clock. |
| OCTAVE RANGE | 1–5 | 2 | How many octaves the sequence spans. |
| ROOT | C–B | C | Key centre. |
| SCALE | 0–12 | Natural Minor | △ **13 positions. Position 0 is unquantized**; 1–12 are the twelve scales. |
| SLIDE | 0.0–1.0 | 0 | Attenuator over the rolled slide flags. CCW = no slide regardless of pattern or CV. |
| SHAPE | 0.0–1.0 | 0.5 | △ **New.** Glide curve: 0 logarithmic, 0.5 linear, 1 exponential. |
| GATE | 1–200 % | 100 % | △ **New.** Scales the pattern's three note lengths together. |
| ACCENT | 0–100 % | 100 % | △ **New.** Scales the contrast between the three velocity layers. |
| PITCH OFFSET | −7…+7 | 0 | △ **New.** Moves the pattern by whole scale degrees, **pre-quantization**, so it stays in key. Overflow carries into the octave. |
| SHIFT | −8…+8 | 0 | △ **New.** Rotates the whole sixteen-step pattern before the LENGTH window. |
| SWING | 0–100 % | 0 | △ **New.** Scales the roll's own swing. Delays off-beat sixteenths only, capped at ⅓ of a step. |
| RANDOM | button | — | △ **New.** Generate — the same thing the RESEED jack does. |

---

## Roll system △ (was: Seed system)

△ **Rules replace seeds** (item 16, decided 2026-11-13, built 2026-11-16).
The original requirements' 1024 seeds on GROUP × SUBGROUP are gone.

**Generate** — the RESEED jack, the RANDOM button, or placing a fresh module
— draws fresh randomness once: one 32-bit value per **lane** (weight, pitch,
gate lengths, gate index, accent, slide, octave, swing, BASS shaping, ARP
accents). From it, a **roll** fills one pattern per STYLE position (six rows).
Everything after the draw is deterministic and harness-tested; nothing random
happens during playback. Lanes draw independently, so they are statistically
unrelated within a roll.

Per-step values in each row:

| Value | Range |
|---|---|
| `weight` | a permutation of 1–16 |
| `pitchIndex` | 0–15, mapped into the scale at playback |
| `gateIndex` | △ 0–2 — which of the pattern's three note lengths |
| `velLayer` | △ 0–2 — off / low / high |
| `slide` | boolean, weighted by style |
| `octaveJump` | △ explicit octave offset, or −1 meaning "spread across the range" |
| `octaveRaw` | 0.0–1.0 |
| `acidRole` | △ BASS only: an interval (0/3/5/7/10 semitones), else −1 |

Plus, per row: △ **three gate lengths** — short 0.12–0.30, mid 0.35–0.60,
long 0.65–0.95 of a clock period — and a swing amount.

The RAND row is the roll's **base pattern**; BASS and the ARPs are shaped
from it, so within one roll RAND and the ARPs share notes and rhythm.

△ **Changes land on the grid.** A Generate or a STYLE change is held until
the loop's last step and swapped in then, so the new pattern's own step 1 is
the first thing heard, rather than dropping in mid-phrase. (Until 2026-11-16
the swap came one step late — step 1 of the first new loop still played the
old pattern; fixed and harness-guarded by
`test_queued_pattern_starts_on_step_one`.) The display flashes amber for 350 ms on a Generate.

**Patch save:** △ `dataToJson` stores the **whole roll** — every row, not
just the one STYLE points at — so a reloaded patch plays what you heard and
STYLE still behaves the same. A Generate still waiting for the wrap is saved
as the roll. Plus the run state. The step position is deliberately *not*
saved. A damaged save, or a patch from before item 16, loads with a fresh
roll instead (breaking test-era patches was accepted).

---

## Density logic

```cpp
step_is_active = (step.weight <= density_knob_value)
```

Weights are a **permutation of 1–16** (one shuffled order per row), not
random draws, so density N activates exactly N of 16 steps in every roll,
the response is linear, turning it up only ever adds steps, and density 1
always plays something. Same roll + same density = identical output, always.
No probability at playback.

△ **Density ranks the weights inside the loop window**, not across all
sixteen steps. Comparing weight to density directly meant that at LENGTH 8
about half the knob's travel switched on steps the loop never reached and
nothing was heard. Ranking within the window makes every click audible at
every length, and is identical to the old behaviour at LENGTH 16.

---

## Style behaviour

△ Style comes from the **STYLE** knob (item 16), not the GROUP zone. Each
position is one precomputed row of the roll.

### BASS △ **303-style acid**

Not "notes on beats 1 and 3". A TB-303 line is runs of sixteenths broken by
rests, with slides welding notes together, the root hammered far more than
anything else, and the odd octave jump.

**Rhythm.** Each roll builds its own priority order of step positions, and
weights 1–16 are laid along it:

1. the downbeat leads — 85 % of rolls
2. four runs of 2–4 consecutive sixteenths at rolled start points
3. whatever is left, in order

Laying 1–16 along a permutation of positions is still a permutation, so
DENSITY stays exactly linear.

**Pitch.** Root-dominated vocabulary, nothing outside it. △ Stored as
**intervals in semitones** — root 0, third 3, fourth 5, fifth 7, seventh 10 —
and resolved to the nearest degree the selected scale actually has, at
playback. Stored as raw pitch indices (as it was first built) the same index
means a different degree depending on how many notes the scale has: on a
triad, "the fifth" landed on the chord's third. Interval-based roles make the
zone work on every scale and chord alike.

| Degree | Root | Fifth | Third | Seventh | Fourth |
|---|---|---|---|---|---|
| Weight | 55 % | 15 % | 12 % | 10 % | 8 % |

**Octave.** +1 jumps on 18 % of notes, and they stay +1 at any OCTAVE RANGE.

**Ties.** 32 % of steps. **Gates.** Short and mid only — ties do the work
long gates would otherwise do. **Accents.** 28 % on the high layer.

#### The shaping rates

Every one of these is a named constant at the top of `pattern.hh`. They are
the dials that decide whether the zone sounds like acid, and they were set by
reasoning rather than by listening — expect to move them.

| Constant | Value | What it does | Turn it up | Turn it down |
|---|---|---|---|---|
| `BASS_DOWNBEAT_CHANCE` | 0.85 | How often step 1 is the *first* note to appear as DENSITY rises | Every roll anchors on the one; safer, more samey | More rolls start off-beat; less grounded, more variety |
| `BASS_RUNS` | 4 | How many separate bursts of consecutive sixteenths each roll gets | Busier, more continuous sixteenth motion | Sparser, more rests, more space |
| `BASS_RUN_MIN` / `BASS_RUN_SPAN` | 2 / 3 → runs of 2–4 | How long each burst is | Longer unbroken sixteenth runs | Choppier, more stabs than runs |
| `BASS_SLIDE_CHANCE` | 0.32 | Share of steps that tie into the next note | More legato, more classic acid glide | More articulated and separate |
| `BASS_ACCENT_CHANCE` | 0.28 | Share of notes on the high velocity layer | More aggressive, more filter movement downstream | Flatter, more even |
| `BASS_OCTAVE_UP` | 0.18 | Share of notes jumped up an octave | Wilder, more range | Stays in the low register |
| Pitch weights | 55 / 15 / 12 / 10 / 8 % | Root / fifth / third / seventh / fourth | — | — |
| Gate mix | 65 % short, 35 % mid | Which of the three note lengths a bass step uses | — | — |

Measured over the BASS rows of 1024 harness rolls, the realized rates match:
86 % downbeat, 32 % ties, 18 % octave-up, 55 % root (the harness prints these
on every run). The earlier per-degree breakdown was measured over the 320
bass seeds before item 16; the shaping code is unchanged.

**Velocity layers** come out 27 % high / 53 % low / 20 % off, so roughly one
note in five is a quiet ghost note — which is where a lot of the groove lives.

#### How the rhythm is actually assembled

Worth understanding before touching the constants, because the ordering is
what makes DENSITY musical rather than arbitrary:

1. A **priority order** of the sixteen step positions is built per roll:
   the downbeat first (85 %), then the four runs in the order they were
   generated, then every position not yet claimed.
2. Weights 1–16 are laid along that order.
3. DENSITY N therefore plays **the first N entries of the priority order**.

So turning DENSITY up walks down the roll's own priority list: the downbeat,
then the heads and bodies of its runs, then the leftovers.

> **Known limitation.** The runs claim **8.7 of 16 positions on average**, so
> from roughly DENSITY 9 upward the remaining notes fill in **ascending step
> order** rather than musically. At those densities nearly everything is
> playing anyway, so it is hard to hear — but it is why the distinct-rhythm
> counts flatten out above density 6. If the high end ever sounds mechanical,
> this is the cause.

### RAND

The unshaped base pattern of the roll, untouched. Even distribution across
the scale, moderate octave spread, all three gate lengths, varied everything.
A test asserts the RAND row is exactly the base pattern in every roll.

### ARP↑ / ARP↓ / ARP↕ / ARP↔

The base pattern's sixteen pitches are sorted, then read out in the STYLE
position's direction: △ **up, down, up-down, down-up**. The rhythm and the
set of pitches are the RAND row's, so switching between RAND and an ARP in
one roll reorders the same notes. Even subdivisions, one gate length
throughout; the base's slide flags are kept, and each ARP row rolls its own
accents.

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

△ Active steps land on even clock subdivisions **unless SWING is up**. The
roll carries its own swing amount, so groove is part of a pattern's identity
rather than a separate global setting; the knob scales it. Only off-beat
sixteenths are delayed, never the downbeat, and never by more than a third of
a step. At SWING 0 the behaviour is exactly what the requirements describe.

The roll determines *which* steps are active, and also how hard they
swing — but not their timing within a step. Clock period is estimated from the time between accepted rising
edges, clamped to 10 ms – 4 s.

---

## Slide / portamento

Each step carries a rolled slide flag. When a step slides:

- pitch **glides linearly** from the previous note to this one
- △ **the gate is held through into the next note** — a slide is a *tie*

△ Arps slide too. Silencing them was an earlier decision of mine, not
something the requirements asked for.

△ The **SHAPE** knob morphs the curve from logarithmic (fast off the mark)
through linear at centre to exponential (slow start, rushing arrival). Every
curve is `pow(t, k)`, so `t = 1` maps to exactly 1 whatever the knob is
doing — the note still lands precisely on the quantized target within the
slide time, and the curve stays monotonic so it can never overshoot. Those
two guarantees are what made linear the right default in the first place, and
they survive the knob.

Glide duration = `SLIDE knob × 0.9 × step duration`, so it is tempo-relative
and always finishes before the next note starts.

The SLIDE knob is an attenuator over `flag + CV`, so CCW kills all glide
however much CV is arriving, and a patched CV can put glide on steps the roll
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

△ **Positions 13–17 are chords** — minor triad, major triad, minor 7th,
dominant 7th, minor 9th — where only chord tones play. These are
**placeholders** until the real chord list arrives; replacing them, or
dropping scales to make room, is expected.

A chord needs no special handling in the quantizer: it is just a small
interval set. What it *did* need was the acid vocabulary becoming
interval-based — see below.

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
| 1 | △ Effective density / length (item 16) | `8/16` |
| 2 | STYLE position | `BASS`, `RAND`, `ARP↑`, `ARP↓`, `ARP↕`, `ARP↔` |
| 3 | Root and scale | `C MINOR`, or `C RAW` when unquantized |

Lines 1 and 3 follow the **effective** value, CV included. Line 1's density
is clamped to LENGTH, as the engine plays it, so at LENGTH 8 a DENSITY of 12
reads `8/8`. When PITCH LENGTH (item 7) is set it will read `8/16 P5`. A
Generate flashes the display amber for 350 ms.

The text is built with no heap, no `<string>` and no stdio, and every
combination is verified. Only the rendering is hardware's problem, and it is
**unverified** — no physical MetaModule has run this yet.

---

## Architecture

```
src/
├── rng.hh       Xorshift32 PRNG. Pure.
├── pattern.hh   Roll entropy → one Pattern per STYLE position. Pure.
├── roll.hh      Roll player (changes on the wrap) + the save format. Pure.
├── scales.hh    Scale tables, degree/octave mapping, quantization. Pure.
├── engine.hh    Realtime state machine: triggers, clock, gate, slide. Pure.
├── cv.hh        CV input mapping. Pure.
├── display.hh   Panel text. Pure.
├── nzzl.cc      Thin adapter: params/jacks ↔ the headers above.
└── plugin.cc    Registration.
```

△ The file layout differs from the one the requirements sketched (`nzzl.hh`
was never needed; six more pure headers exist instead).

**The rule that shapes everything:** anything deterministic lives in a pure
header with no `rack.hpp` include, so the native harness can compile and test
it without Rack or the ARM toolchain. `nzzl.cc` holds no decisions.

Seven test suites under `tests/`, run by `./tests/run_tests.sh` and by CI on
every branch. △ Since item 16 they check properties over 1024 reproducible
rolls (`tests/rolls.hh`) instead of every one of 1024 seeds.

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
| A5 | 1024 recallable seeds on GROUP / SUBGROUP; CV SEED; display `g . s`; "all output derived from a seed" | **Rules replace seeds**: Generate rolls a fresh pattern for every STYLE position, saved with the patch; **STYLE** knob (6 detents) replaces GROUP / SUBGROUP; CV SEED → **CV DENSITY** (offset, clamped); display line 1 `8/16` | Gerardo has never wanted to recall a seed; Sting 2's fresh, rule-shaped rolls fit how he plays. Breaking test-era patches accepted | 2026-11-13 / 11-16 |

### B. Engineering decisions the requirements left open

| # | Requirement said | Built | Why |
|---|---|---|---|
| B1 | "Native MetaModule CoreProcessor plugin (not a VCV Rack port)" | Built against the **SDK's VCV Rack adaptor**; one source tree compiles for both targets | Lets the module be developed and heard in VCV Rack on a Mac, with hardware builds from the same source. Without it every iteration would need an SD card and the physical module |
| B2 | A file layout with `nzzl.hh` | **Six more pure headers** (`pattern`, `roll`, `scales`, `engine`, `cv`, `display`); no `nzzl.hh` | Everything deterministic lives in a header with no Rack dependency, so it can be machine-tested. This is why clock, gate, slide and quantization all have automated coverage |
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
| C1 | Zones: 1–10 / **11–21** / **22–32** | 1–10 / 11–22 / 23–32 | Correct until item 16 replaced zones with the STYLE knob (A5) |
| C2 | Arp subgroup zones: 1–8 up, 9–16 down, 17–24 up-down, 25–32 down-up | **Did not exist** — direction was chosen at random per 4-step block | Implemented, each with its own display arrow; since item 16, four STYLE positions |
| C3 | Twelve named scales in a given order | Wrong list — Phrygian Dominant and Blues present, Pentatonic Major and Whole Tone missing | The requirement's twelve, in its order, at positions 1–12 |
| C4 | CV SCALE and CV ROOT are **overrides**; CV SEED is an **offset**; CV SLIDE is summed **then** attenuated | All four were offsets; SLIDE was `knob + cv` | Each matches its stated semantics (CV SEED's offset now drives DENSITY, A5) |

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
| △ After item 16: bass, 1024 rolls | 16 | 29 | 67 | **201** | 285 | 366 |
| △ After item 16: RAND, 1024 rolls | 16 | 120 | 467 | 797 | 928 | 967 |

---

## Known issues — found in the first playtest (2026-09-21)

**Both fixed 2026-09-26.** Kept here because the failure modes are worth
remembering, and because both are now guarded by tests that did not exist
before.

### K1. The sequence is one sixteenth late against the clock

The first clock pulse plays step **2**, not step 1. The step counter starts at
0 and is incremented *before* the step fires, so pulse 1 → index 1, pulse 2 →
index 2, and step index 0 — the pattern's first note — plays on pulse 16.

That matters most in the bass zone, where 85 % of seeds are built to lead
with the downbeat: the note meant for beat 1 actually lands on the last
sixteenth of the bar, a pickup. Every pattern is shifted one sixteenth late.

With CLOCK DIV it is worse. At ÷4 the first step fires on pulse **4** (should
be 1), so a divided pattern is three pulses plus one step off the bar.

The harness missed it because `test_clock_advances_steps` checks that steps
advance, not *which* step fires on the first pulse.

**Fixed.** `test_first_pulse_plays_step_one` now asserts that pulse 1 fires
step index 0 at clock divisions 1 through 4, and that steps march in order
from there.

### K2. At short LENGTH, about half the DENSITY knob does nothing

DENSITY ranks all sixteen steps by weight, but LENGTH only plays the first L
of them. So raising DENSITY often switches on a step that sits outside the
loop — and nothing audible happens.

Measured: fraction of DENSITY clicks (d → d+1) that change nothing audible:

| Setting | BASS | RAND | ARP |
|---|---|---|---|
| LENGTH 16, SLIDE 0 | 0 % | 0 % | 0 % |
| LENGTH 16, SLIDE full | 7.3 % | 0.9 % | 0 % |
| LENGTH 16, GATE 200 % | 2.5 % | 0.9 % | 2.3 % |
| **LENGTH 8** | **52.9 %** | **49.8 %** | **50.2 %** |

`DESIGN.md` recorded this as an accepted trade-off ("the knob still feels
monotonic"). In practice it was the wrong call: at LENGTH 8 the knob is dead
half the time. The smaller bass/SLIDE effect is ties plus root repetition —
a new note arrives legato, on the same pitch as the one before it, and that
one is inherent to how 303 lines work rather than a bug.

**Fixed.** Density now ranks within the window.
`test_density_respects_length` asserts that at every LENGTH from 2 to 16,
each click adds exactly one more playing step.

---

## Future additions to consider

**Nothing here is built, and nothing here should be built until asked for.**
This section exists so ideas stop living in chat. Each entry records enough
design thinking that picking it up later does not mean re-deriving it.

### 1. PITCH OFFSET knob — ✅ **built 2026-09-26**, as scale degrees

**The gap.** OCTAVE RANGE controls how many octaves the pattern *spans*, but
nothing controls where that span *starts*. The pattern always begins at the
base octave. ROOT changes the key, not the register, and it only moves within
one octave. So there is currently no way to say "same pattern, higher up".

**The requirement:** the offset must be applied **pre-quantization**. That is
the important part — it means the offset shifts the raw pitch value *before*
the scale lookup, so the result is always still in key. A post-quantization
offset would add semitones to an already-quantized note and walk it out of
the scale, which is exactly the failure the ROOT invariant exists to prevent.

**One design question to settle before building it.** "Starting point" could
mean either of two things, and they feel different to play:

| | Offset in **octaves** | Offset in **scale degrees** |
|---|---|---|
| What it does | Moves the whole span up/down by whole octaves | Moves the pattern up/down *through the scale* |
| Quantization | Unaffected — octaves are scale-invariant | Must happen pre-quantization, as specified |
| Feels like | A register control | A melodic transposition that stays in key |
| Range | roughly −2 … +2 octaves | roughly −7 … +7 degrees |

The "pre-quantization" instruction points at **scale degrees**, since octaves
would not need the caveat. A combined coarse/fine (octaves + degrees) is also
possible but costs two panel positions.

**Implementation notes.** Applies in `scales.hh`, where `degreeForIndex()`
already turns a raw index into a degree — the offset would be added there,
before `intervals[]` is indexed, with the overflow rolling into the octave.
`octaveForStep()` handles the octave half. Needs a panel position and,
probably, a CV input to be worth having.

### 2. Slide slope — ✅ **built 2026-09-26** as the SHAPE knob

Slides are currently a **linear ramp**. That was a deliberate choice: linear
arrives exactly (so the note lands precisely in tune), cannot overshoot, and
"reaches its target within the slide time" is a property a test can assert.

A real 303's slide is not linear — it is the filter/VCO circuit's own curve,
closer to exponential. A knob morphing the curve from logarithmic (fast
attack, slow settle) through linear to exponential (slow start, rushing
arrival) would be a genuine feel control.

**What to preserve when building it.** The arrival guarantee. Whatever the
curve, it must still land *exactly* on the quantized target within the slide
time and never overshoot — otherwise notes end up out of tune and
`test_slide_reaches_target` stops being provable. A shaped interpolation of
the existing `slideProgress` (`pow(t, k)` with k from the knob) keeps both
properties, since `t = 1` maps to `1` for every k.

### 3. Swing within seeds — ✅ **built 2026-09-26**

All active steps currently land on even clock subdivisions — the handoff spec
says so explicitly, and the seed decides *which* steps play, never *when*.

Swing would delay every other sixteenth by a fraction of a step. Two possible
shapes:

- **A swing knob** — global, 50 % (straight) to ~66 % (hard shuffle). Simple,
  predictable, and what most sequencers do.
- **Seed-derived swing** — each seed carries its own swing amount, so groove
  becomes part of a pattern's identity rather than a separate setting. This
  is what "within seeds" suggests, and it is the more interesting version.

The two combine well: seed-derived swing with a knob that scales it, exactly
as SLIDE already attenuates the seed's own slide flags.

**Implementation notes.** This is the one idea here that touches the
determinism story. Swing is a *timing* offset, so it has to live in
`engine.hh` as a delay applied between the clock edge and the step firing —
which means the engine gains a pending-step timer it does not currently have.
Gate duration and slide time are both derived from the step duration, so both
would need to account for a shifted step boundary. Worth scoping carefully;
it is a bigger change than it sounds.

### 4. Chord positions on the SCALE knob — ✅ **mechanism built 2026-09-26**, chords are placeholders

Replace some of the scales with chords, so only chord tones play. Chords to
be supplied.

A chord drops straight into the existing quantizer — it is just a small
interval set, e.g. a minor triad `{0, 3, 7}` or a minor seventh
`{0, 3, 7, 10}` — and the proportional degree mapping spreads the sixteen
pitch indices across its notes like any other scale.

**One thing to fix alongside it.** The bass zone's acid vocabulary is defined
by pitch *index*, tuned for seven-note scales. On a four-note chord it still
lands correctly (root, third, fifth, seventh). On a **triad** it does not: the
index meant as "the fifth" maps to the chord's *third*, so a bassline on a
triad loses its fifth. The vocabulary needs to become degree-aware before
triads ship.

Replacing scales changes the knob's index order. That is fine before
release, but any patches saved during testing will retune.

### 5. SHIFT knob — ✅ **built 2026-09-26**

A bipolar knob that moves the whole pattern earlier or later by whole steps
without changing its content — the OFFSET idea from the reference module.
Named **SHIFT** here so it can't be confused with the pending **pitch** offset.

Open design question: rotate the full sixteen-step pattern *before* the
LENGTH window (so at LENGTH 8, shifting brings different parts of the
pattern into the loop), or rotate *within* the window (a pure phase shift of
what is already playing). The first is more useful and pairs naturally with
the K2 fix.

### 6. Seed changes land on the grid — ✅ **built 2026-09-26**

When a new seed arrives — knob turn, RESEED or CV SEED — it should start in
step with the bar rather than wherever the step counter happens to be.

Fixing **K1** puts step 1 back on the downbeat. On top of that, the new seed
could be held until the next loop boundary so it always starts from its own
step 1, the way clip launching is quantized in a DAW. There is no RESET
input, so how well it lines up with an *external* bar still depends on when
the module started.

### 7–11. Ideas from the TGB-420 Sequencer study — logged 2026-11-13

Taken from a static read of the TGB-420 Sequencer (Max for Live); the full
write-up is `x13f-dsp/docs/TGB_420.md` (private repo, so only behaviour is
recorded here, no code). Each is phrased to keep NZZL's invariants: nothing
random at playback, existing RNG streams untouched, new seed-derived values
on **new salts**, and every new control default-off so existing patches play
exactly as before.

#### 7. Separate pitch lane — **agreed 2026-11-13; builds after item 16 (decided 2026-11-16)**

**The gap.** `advanceStep` reads pitch from the same step as the rhythm, so
turning DENSITY up changes *which pitches* play as well as how many notes.
On a 303 the pitch sequence advances only when a note plays.

**The idea.** A pitch counter that advances **once per played note**, not
per step, reading the seed's sixteen pitches in order. DENSITY then becomes
a rhythm-only control: the melody keeps its order and just gets more or
fewer hits. An optional **PITCH LENGTH** (separate from LENGTH, 0 = linked)
wraps that counter at its own length, giving polymeter: e.g. 5 pitches
cycling over a 16-step rhythm. Slide and octave go with the pitch.

**Decided 2026-11-16.** Advance-per-note is **the default and always on**,
with no switch: breaking test-era patches is accepted (item 16).
**PITCH LENGTH takes one of the two panel positions freed by GROUP /
SUBGROUP**; 0 = linked to LENGTH, 1–16 = wraps on its own. It is built on the
roll (its own lane), after item 16, not on the seed system.

**Still to settle when building.** What resets the pitch counter (loop wrap,
Generate, RUN) and how SHIFT applies to it.

#### 8. Generative evolve — **agreed in principle 2026-11-13; deferred 2026-11-16**

TGB applies **one small mutation every N bars** (one stage's pitch, or its
mode / length / divider / gate). For NZZL it would have to be deterministic:
mutation k drawn from a new `SALT_EVOLVE` stream keyed on the seed and the
loop count, so the same patch drifts the same way from a reset. Open: NZZL
deliberately does not save the step position, so drift restarts on reload;
whether that is fine, what the controls are (rate, what may mutate), and
whether a reseed or RUN resets it. Gerardo wants to discuss the design when
this is implemented.

**Deferred 2026-11-16.** The seed-and-loop-count keying above no longer
works once item 16 removes seeds. Evolve waits until items 16 and 7 are
built and gets its own design conversation then. One candidate that keeps
"no randomness at playback": Generate also rolls a list of upcoming small
changes, saved with the roll.

#### 9. NOTES knob — a nested cap on distinct pitches

Rank the seed's pitches by first appearance and limit the line to the first
N distinct ones, so turning NOTES up only ever *adds* a pitch and never swaps
one out — the same nesting DENSITY's permutation gives. TGB does this by
re-rolling; NZZL's version is a pure read-side filter, no new stream.

#### 10. More interval vocabularies (pools)

TGB's pools are small interval sets chosen for character: Power (root + 5th),
Tension (root + minor 2nd), Hypnotic (root + m2 + m3), Dark (root + m2 + 5th
+ octave). NZZL's acid roles and the chord positions already resolve interval
sets through `nearestDegree`, so a vocabulary is just a new set. **Catch:**
`nearestDegree` picks the nearest degree (ties down), so a minor 2nd on a
scale without one collapses onto the root. TGB avoids that by moving a
missing pool note **up** to the next scale note when generating; such a
vocabulary would need that rule.

#### 11. Ratchets

TGB's "a note every pulse, thinned by a ÷1–÷4 divider" mode. For NZZL: a new
seed-derived per-step ratchet count (new salt) so a step fires 2–4 times
inside its slot, for fills. Open: how it interacts with GATE (per hit or per
step) and with slide.

### 12–16. Ideas from the Sting 2 and TB-3PO study — logged 2026-11-13

Taken from a static read of Sting 2 (Max for Live) and the TB-3PO source
(Phazerville O_C, MIT). The full write-up is
`x13f-dsp/docs/PATTERN_GENERATORS.md` (private repo, so only behaviour is
recorded here, no code or tables). Same rules as 7–11: nothing random at
playback, existing RNG streams untouched, new seed-derived values on **new
salts**, and every new control default-off.

**Already covered by NZZL, so not logged as new items:** exact-count,
nested DENSITY (Sting builds it from one shuffled order; NZZL's weight
permutation does the same), independent per-lane randomness (NZZL's
per-attribute streams), and a root-heavy acid bass (the bass zone).
Sting's 3–5 note **motif cell** belongs with item 9 (NOTES knob): a NOTES cap
of 3–5 gives the same "small set of notes, reused" identity.

#### 12. Chord input — notes from what is played

**The idea.** A polyphonic V/oct input. The held notes, reduced to pitch
classes, become the note set the pattern draws from, in place of the SCALE
position. Register still comes from the octave lane, so the voicing played
does not matter. Sting's Arp and Bass modes both work this way.

**Why it fits NZZL.** A held chord is just a small interval set, which is
what the chord positions on the SCALE knob (item 4) already are. The
quantizer and the interval-based acid roles handle it with no new pitch
logic: the seed keeps choosing degrees, and the chord decides what those
degrees are. The pattern stays deterministic for a given seed and chord.

**To settle when building.**
- **Debounce.** Sting waits about 100 ms after the last change before using
  a new chord, so rolling a chord in counts once. Also: whether a chord
  change lands at once or on the loop wrap, like seed changes (item 6).
- **Empty input.** Fall back to the SCALE knob, or hold the last chord.
- **Display.** Show the detected chord or root instead of the scale name.

#### 13. Root detection on the chord input, with NZZL's own twist

**The idea.** Detect the root of the held chord and feed it to ROOT, so the
acid bass resolves around the chord being played rather than the knob. Of
Sting's modes, only Bass does this.

Sting's method, in outline: every pitch class is scored as a candidate root,
from how strongly the other notes support it as intervals (fifth and thirds
strongest), a bonus for fitting a known chord shape (sevenths largest, sixths
tiny, so C6 reads as Am7), small bonuses for being the lowest or a doubled
note, and a heavy penalty for not being played. Ties go to the bass note.

**Where NZZL can go further.** Sting's version has four gaps, and closing
them is the twist:
- **Memory.** It judges each chord alone, so an ambiguous chord can flip
  roots from one moment to the next. Add hysteresis: keep the current root
  unless a new one wins by a clear margin.
- **Key context.** It ignores the scale. Use the SCALE position to break
  ties toward roots that are in key.
- **Confidence.** It returns one answer. Return a confidence too, so a clear
  C major and a coin-flip can behave differently (for example, only move
  ROOT when confident).
- **Our own weights.** Design and tune them ourselves, tested against a
  table of chords and inversions. Do not reuse Sting's numbers.

**To settle when building.** Whether the detected root overrides ROOT (as
CV ROOT does) or adds to it, and whether it needs a switch to turn it off.

#### 14. ROOT AMOUNT knob — busy to pure root, nested

**The idea.** One knob that pulls the line toward the root: at 0 the seed
plays as now, at full every step is the root. Sting's Type knob and Bass
mode both do a version of this.

**NZZL's version is nested.** Sting rolls each knob position separately, so
turning the knob changes every note. Instead, give each seed a per-step
order on a new salt (`SALT_ROOT_FORCE`) and force a step to the root when
its place in that order is below the knob. Turning it up then only ever
converts more steps to the root and never moves the ones already converted,
the same property DENSITY has.

**Interaction to check.** The bass zone is already about 55% root; this knob
adds to that rather than replacing it.

#### 15. Anti-clumping for slides and accents

**The gap.** NZZL rolls each step's slide and accent independently, at fixed
per-zone chances. TB-3PO makes a slide or accent less likely on the step
right after one, which spreads them out and sounds more like a played line.

**The catch, now lifted.** This changes every existing seed's slide and
accent pattern, which the old invariants forbade. With rules replacing seeds
(item 16) that no longer applies, and it can simply become the rule.

#### 16. Rules replace seeds — **decided 2026-11-13, ✅ built 2026-11-16** (awaiting user test)

**As built.** `pattern.hh` rolls a `RollEntropy` (one fresh draw per lane)
into a `Roll` of six rows; `roll.hh` holds it, lands Generates and STYLE
changes on the wrap, and saves it as a validated hex string next to the run
state. STYLE sits in GROUP's param slot; SUBGROUP's slot is held for PITCH
LENGTH. Not built here: PITCH LENGTH itself (item 7) and anti-clumping
(item 15), which come next.

**The decision.** Gerardo prefers Sting's approach: every Generate is a fresh
roll, shaped by rules, with no seed to dial or return to. He has never
wanted to recall a seed. The seed system and the determinism invariants
that protect it (`docs/DESIGN.md`) are to be **replaced**, and breaking saved
patches made during testing is accepted.

**What stays.**
- **No randomness at playback.** Rolling happens only at Generate (RESEED
  jack, RANDOM button). `process()` still only reads precomputed steps.
- **Exact, nested DENSITY.** One shuffled order per roll; density N plays
  exactly N steps and turning it up only adds steps.
- **All the rules.** The bass zone's 303 shaping, the arp directions, scales,
  chord positions, GATE / ACCENT layers, swing, slide: these are the "rules"
  and carry over unchanged in intent.

**What changes.**
- **Generate rolls, it does not pick a seed.** RESEED and RANDOM produce a
  new pattern from a non-deterministic source.
- **Knobs pick from precomputed rows.** Following Sting, each Generate fills
  every knob position up front, so turning a knob is instant and returning to
  a position gives the same pattern until the next Generate. This is what
  keeps the knobs stable without a seed.
- **GROUP and SUBGROUP go; a STYLE control replaces them** (decided
  2026-11-13). It selects BASS / RAND / ARP and the arp directions, as a knob
  or a menu selector. It does **not** need a CV input: Gerardo does not expect
  to modulate style. The freed panel position is open.
- **STYLE is a panel knob** (decided 2026-11-16) with six detented
  positions: BASS, RAND, ARP↑, ARP↓, ARP↕, ARP↔. It takes one of the two
  positions GROUP and SUBGROUP free; **PITCH LENGTH** (item 7) takes the other.
- **CV SEED becomes CV DENSITY** (decided 2026-11-16): an **offset** added to
  the DENSITY knob, 10 V = the full 1–16 range, clamped. Because DENSITY
  nests, a CV sweep only adds or removes hits and never reshuffles them.
- **Display line 1 shows density / length** (decided 2026-11-16): `8/16`,
  and `8/16 P5` when PITCH LENGTH is set. Effective values, CV included.
- **Hashed seeds and salted per-attribute streams go.** They existed so
  upgrades would not change saved seeds. Lanes still draw independently
  within a roll, so knobs on one lane never disturb another.
- **Tests change from "every one of 1024 seeds" to properties over many
  rolls**: density exactness, nesting, rhythm variety across rolls, arp
  direction, scale safety.

**The rolled pattern is saved with the patch** (decided 2026-11-13). As
Sting does, a saved patch plays what you heard when you saved it. NZZL stores
it through `dataToJson`, which already holds the run state; that means the
whole roll (every precomputed row), not just the rows the knobs point at, so
the knobs still behave the same after a reload.

**Effect on items 7–15.** Their "new salt" wording becomes "its own lane in
the roll". Item 15 (anti-clumping) loses its catch: with no saved seeds to
protect, it can simply become the rule.

---

## Open questions

**Settled 2026-11-13: rules replace seeds** (item 16). Generate rolls a
fresh pattern; knobs pick from rows precomputed at each roll. Also settled
the same day: the rolled pattern is saved with the patch, and a STYLE
control (no CV) replaces GROUP / SUBGROUP.

**Built 2026-11-16: item 16.** Awaiting the manual pass in VCV Rack
(SYSTEM_TEST_GUIDE § 16).

**Settled 2026-11-16: the rest of item 16, and the order.** Build item 16
first, then item 7 and item 15 on top of it. STYLE is a panel knob; PITCH
LENGTH takes the other freed position; CV SEED becomes CV DENSITY (offset);
display line 1 shows density / length. The pitch lane (item 7) is the
default, always on. Evolve (item 8) is deferred to its own design
conversation.

**Settled 2026-09-21: SCALE LOCK stays folded into the SCALE knob.** The
approach is confirmed; if it proves awkward in practice the switch can come
back, but it is no longer an open question.

Two things still want a human opinion rather than a test:

1. **Are the 303 shaping rates right?** See § The shaping rates above. They
   were set by reasoning, not by listening. Test T8.15.
2. **Are the GATE and ACCENT ranges right?** 1–200 % and 0–100 %. Test G.10.

And one thing genuinely blocked: **the display's rendering on hardware is
unverified.** The text content is tested for every combination; whether
`MetaModule::VCVTextDisplay` paints it needs the physical module.
