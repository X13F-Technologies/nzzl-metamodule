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

Fifteen knobs and one button. △ Six controls are new (SHAPE, PITCH
OFFSET, SHIFT, SWING, GATE, ACCENT, plus the RANDOM button) and one from
the original requirements is gone (SCALE LOCK).

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
| SHAPE | 0.0–1.0 | 0.5 | △ **New.** Glide curve: 0 logarithmic, 0.5 linear, 1 exponential. |
| GATE | 1–200 % | 100 % | △ **New.** Scales the pattern's three note lengths together. |
| ACCENT | 0–100 % | 100 % | △ **New.** Scales the contrast between the three velocity layers. |
| PITCH OFFSET | −7…+7 | 0 | △ **New.** Moves the pattern by whole scale degrees, **pre-quantization**, so it stays in key. Overflow carries into the octave. |
| SHIFT | −8…+8 | 0 | △ **New.** Rotates the whole sixteen-step pattern before the LENGTH window. |
| SWING | 0–100 % | 0 | △ **New.** Scales the seed's own swing. Delays off-beat sixteenths only, capped at ⅓ of a step. |
| RANDOM | button | — | △ **New.** Picks a random seed and drives GROUP/SUBGROUP to match — the same thing the RESEED jack does. |

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

**RESEED** — the jack or the △ **RANDOM button** — picks a new seed from a
non-deterministic source and **writes the GROUP and SUBGROUP knobs** so what
you hear stays reproducible by hand. The display flashes amber for 350 ms.

△ **Seed changes land on the grid.** However the seed changes — knob, button,
jack or CV — the new pattern is held until the loop wraps, so it always
starts from its own step 1 rather than dropping in mid-phrase.

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

△ **Density ranks the weights inside the loop window**, not across all
sixteen steps. Comparing weight to density directly meant that at LENGTH 8
about half the knob's travel switched on steps the loop never reached and
nothing was heard. Ranking within the window makes every click audible at
every length, and is identical to the old behaviour at LENGTH 16.

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
| `BASS_DOWNBEAT_CHANCE` | 0.85 | How often step 1 is the *first* note to appear as DENSITY rises | Every seed anchors on the one; safer, more samey | More seeds start off-beat; less grounded, more variety |
| `BASS_RUNS` | 4 | How many separate bursts of consecutive sixteenths each seed gets | Busier, more continuous sixteenth motion | Sparser, more rests, more space |
| `BASS_RUN_MIN` / `BASS_RUN_SPAN` | 2 / 3 → runs of 2–4 | How long each burst is | Longer unbroken sixteenth runs | Choppier, more stabs than runs |
| `BASS_SLIDE_CHANCE` | 0.32 | Share of steps that tie into the next note | More legato, more classic acid glide | More articulated and separate |
| `BASS_ACCENT_CHANCE` | 0.28 | Share of notes on the high velocity layer | More aggressive, more filter movement downstream | Flatter, more even |
| `BASS_OCTAVE_UP` | 0.18 | Share of notes jumped up an octave | Wilder, more range | Stays in the low register |
| Pitch weights | 55 / 15 / 12 / 10 / 8 % | Root / fifth / third / seventh / fourth | — | — |
| Gate mix | 65 % short, 35 % mid | Which of the three note lengths a bass step uses | — | — |

Measured over all 320 bass seeds (5120 notes), the realized rates match:
85.3 % downbeat, 32.6 % ties, 27.3 % accent, 17.7 % octave-up, 64.1 % short
gates, and 54.8 / 15.0 / 11.3 / 10.5 / 8.5 % on the five pitch degrees.

**Velocity layers** come out 27 % high / 53 % low / 20 % off, so roughly one
note in five is a quiet ghost note — which is where a lot of the groove lives.

#### How the rhythm is actually assembled

Worth understanding before touching the constants, because the ordering is
what makes DENSITY musical rather than arbitrary:

1. A **priority order** of the sixteen step positions is built per seed:
   the downbeat first (85 %), then the four runs in the order they were
   generated, then every position not yet claimed.
2. Weights 1–16 are laid along that order.
3. DENSITY N therefore plays **the first N entries of the priority order**.

So turning DENSITY up walks down the seed's own priority list: the downbeat,
then the heads and bodies of its runs, then the leftovers.

> **Known limitation.** The runs claim **8.7 of 16 positions on average**, so
> from roughly DENSITY 9 upward the remaining notes fill in **ascending step
> order** rather than musically. At those densities nearly everything is
> playing anyway, so it is hard to hear — but it is why the distinct-rhythm
> counts flatten out above density 6. If the high end ever sounds mechanical,
> this is the cause.

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

△ Active steps land on even clock subdivisions **unless SWING is up**. The
seed carries its own swing amount, so groove is part of a pattern's identity
rather than a separate global setting; the knob scales it. Only off-beat
sixteenths are delayed, never the downbeat, and never by more than a third of
a step. At SWING 0 the behaviour is exactly what the requirements describe.

The seed determines *which* steps are active, and now also how hard they
swing — but not their timing within a step. Clock period is estimated from the time between accepted rising
edges, clamped to 10 ms – 4 s.

---

## Slide / portamento

Each step carries a seed-derived slide flag. When a step slides:

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

---

## Open questions

**Settled 2026-09-21: SCALE LOCK stays folded into the SCALE knob.** The
approach is confirmed; if it proves awkward in practice the switch can come
back, but it is no longer an open question.

Two things still want a human opinion rather than a test:

1. **Are the 303 shaping rates right?** See § The shaping rates above. They
   were set by reasoning, not by listening. Test T8.15.
2. **Are the GATE and ACCENT ranges right?** 1–200 % and 0–100 %. Test G.10.

And one thing genuinely blocked: **the display's rendering on hardware is
unverified.** The text content is tested for all 1024 seeds; whether
`MetaModule::VCVTextDisplay` paints it needs the physical module.
