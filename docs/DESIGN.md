# NZZL — Design Record

This document is the authoritative record of design decisions. The source
spec now lives in the repo as **[HANDOFF_SPEC.md](HANDOFF_SPEC.md)** (copied
in 2026-09-20 — before that, three sessions were building against a one-line
paraphrase of it). That file is the requirements; this one records **how**
we're building them and **why**, including every place we deliberately
deviate — see § Deviations from the handoff spec.

---

## Architecture

```
src/
├── rng.hh       Xorshift32 PRNG. Pure, no deps.
├── pattern.hh   Seed → StepData[16], style zones, seed↔knob mapping. Pure.
├── scales.hh    Scale tables + degree/octave mapping + quantization. Pure.
├── engine.hh    The realtime state machine: triggers, clock, gate, slide,
│                sample-and-hold. Pure — fed fake time and fake voltages.
├── cv.hh        CV input → parameter mapping. Pure.
├── display.hh   Panel text building. Pure, no heap, no stdio.
├── nzzl.cc      Thin Rack adapter: params/jacks ↔ the headers above.
└── plugin.cc    Plugin registration.
tests/
├── test_pattern.cc  Pattern generation
├── test_scales.cc   Pitch quantizer
├── test_engine.cc   Realtime sequencer, driven by synthetic time/voltages
├── test_styles.cc   Style zones (aggregate, statistical)
├── test_cv.cc       Seed↔knob round trip + CV mappings
├── test_display.cc  Panel text
├── refgen.cc        Reference-data generator (not a test; run by hand)
└── run_tests.sh     Compiles and runs every test_*.cc (each has its own
                     main()). Use after every pure-logic change.
```

**Rule: anything deterministic lives in a pure header and gets tests.**
`nzzl.cc` only holds realtime concerns: triggers, timers, voltages, knob
reads. This is what makes the module testable by Claude without VCV Rack.

The same `nzzl.cc` compiles for both VCV Rack (local dev, `build-vcv/`) and
MetaModule (CI ARM cross-compile) via the SDK's rack-interface shim.

---

## Determinism invariants (do not break)

1. **Seed hashing.** `seed_index` (0–1023, from `(group-1)*32 + (subgroup-1)`)
   passes through `hashSeed()` (splitmix32) before entering xorshift.
   *Why:* raw sequential seeds in xorshift32 are heavily correlated — adjacent
   SUBGROUP clicks sounded nearly identical. Verified by the
   `test_seed_distinctness` test.

2. **Per-attribute RNG streams.** Each step attribute (weight, pitch, gate,
   velocity, slide, octave) draws from its own `Xorshift32` seeded with
   `hashSeed(seed_index ^ SALT_<ATTR>)`. *Why:* with one shared stream, any
   future change to how one attribute is generated (e.g. style-dependent
   slide logic in Task 8) would reshuffle every attribute after it — every
   user's saved patch would change sound on upgrade. With isolated streams,
   attributes can be reworked independently. **Never change an existing
   attribute's draw pattern; add new salts for new needs.** Verified by
   `test_attribute_isolation`.

3. **Weights are a permutation of 1–16**, not random draws. *Why:* the spec
   says density N should feel linear and density 1 must always play something.
   Random draws made density lumpy (possibly silent at 1, uneven elsewhere).
   A Fisher-Yates-shuffled permutation guarantees density N activates exactly
   N of 16 steps on every seed. Verified by `test_density_exact`.
   *Note:* with LENGTH < 16 the active count within the window is no longer
   exactly N — accepted; the knob still feels monotonic.

4. **No randomness at playback.** `process()` only reads precomputed
   `StepData`. The RESEED trigger (Task 12) is the sole permitted use of a
   non-deterministic source, and only at the trigger moment.

---

## Pitch mapping (Task 5)

**Position 0 is UNQUANTIZED, not a scale.** There is no separate SCALE LOCK
switch — it was folded into the SCALE knob (user decision, 2026-09-20).
Positions 1–12 are then the handoff spec's twelve scales in its order, so
folding the toggle in costs no scale: the knob simply has 13 positions.

*Why:* one control now answers the whole question "how are these notes
pitched?" instead of two that interact. It frees a panel position, which
matters on MetaModule, and it means CV SCALE (Task 10) can sweep into and out
of raw mode without a second CV input. **The cost, accepted knowingly:** you
can no longer A/B back to the scale you had — going to raw and back means
finding your scale again on the knob. Blues was dropped to make room.

**Scale order is an on-disk contract.** `SCALE_PARAM` stores an index, so
inserting a scale mid-list would retune every saved patch. **Append only,
never reorder.** The order is the spec's, with unquantized prepended:

| # | Scale | # | Scale |
|---|-------|---|-------|
| **0** | **Unquantized** | 7 | Harmonic Minor |
| 1 | Major | 8 | Melodic Minor |
| 2 | Natural Minor *(default)* | 9 | Pentatonic Major |
| 3 | Dorian | 10 | Pentatonic Minor |
| 4 | Phrygian | 11 | Chromatic |
| 5 | Mixolydian | 12 | Whole Tone |
| 6 | Lydian | | |

Verified against an independently written reference table in
`test_scale_tables`.

The RAW entry in the table carries `noteCount == 0` and no intervals.
`quantizedVoltage` falls back to `rawVoltage` if it is ever handed that entry,
so a caller that skips the `isUnquantized()` check degrades gracefully instead
of reading an empty interval array.

**Degree mapping is proportional, not modulo.**
`degree = pitchIndex * noteCount / 16`. *Why:* a modulo mapping wraps —
pitchIndex 14 and 15 would fold back to degrees 0 and 1 in a 7-note scale, so
sweeping SCALE across different note counts would scramble the melody. The
proportional map is monotonic, so the step that was the pattern's highest note
stays its highest note in every scale. Verified by `test_degree_mapping` and
`test_contour_monotonic`.

**Voltage formula (any real scale):**
`V = octave + (root + interval) / 12`, where `octave = int(octaveRaw × octaveRange)`
clamped to `[0, octaveRange-1]`. The octave spread is therefore exactly the
OCTAVE RANGE knob; ROOT adds a further 0–11/12 V on top, which is intended —
it transposes the whole pattern. Verified by `test_quantized_notes_in_scale`
(11.8M notes: every one lands on an exact semitone that belongs to the scale,
within the octave range) and `test_root_transposes`.

**Scale position 0 (unquantized)** divides each octave into 16 equal steps
(75 cents) rather than emitting a continuous voltage. *Why:* the module only
has 16 discrete pitch indices, so "raw" has to mean *off the semitone grid*,
not *smooth*. 74% of generated notes land audibly off-grid; range is 0–5 V at
OCTAVE RANGE 5. Verified by `test_raw_mode`.

**ROOT only ever adds a whole number of semitones.** This is an invariant, not
an implementation detail: transposing must never knock a quantized note off
the grid or out of key. `test_root_stays_quantized` asserts, for 256 seeds ×
11 scales × 5 octave ranges × all 12 roots, that every note stays on an exact
semitone, stays in key, **and lands on the same scale degree as it did at root
C** — proving ROOT transposes and does nothing else. It holds in raw mode too:
root there is an exact semitone offset applied to the microtonal pattern.

**Consequence for Task 10:** CV ROOT must quantize to integer semitones before
it reaches the formula. A smooth CV passed straight through would de-quantize
the output and break the invariant above. Same for CV SCALE — it must land on
discrete scale indices.

**Pitch is latched at gate onset, quantized every sample.** The step's
`pitchIndex` / `octaveRaw` are sample-and-held exactly like velocity (silent
steps don't retrigger a new note), but the quantizer re-runs continuously, so
turning ROOT / SCALE / OCTAVE RANGE re-pitches the currently-held note
immediately instead of waiting for the next gate. This half lives in
`nzzl.cc` and is **user-tested, not harness-tested.**

---

## Realtime behavior decisions

- **RUN is a toggle trigger** (user-requested change from the spec's gate
  behavior): rising edge flips running/stopped. Unpatched = always running.
  Stopping holds the step position; it never resets.
- **Clock period** is estimated from time between accepted rising edges,
  clamped to 10 ms–4 s. Gate duration = `gateLength × period × clockDiv`.
- **Velocity is sample-and-hold**: latched only when a gate fires, held
  through inactive steps. *Why:* downstream VCAs/filters shouldn't drift
  during silence.
- Schmitt trigger thresholds 0.1 V / 2.0 V for clock, run (and later reseed).

---

## Testing strategy

Three tiers. Claude runs tier 1 via `./tests/run_tests.sh` after every
pure-logic change; the user runs tier 2 in VCV Rack; tier 3 needs hardware.

**Current coverage honestly stated:** the `engine.hh` extraction happened at
Task 7, as planned, so the harness now covers realtime behaviour too — clock
edges and division, run-toggle semantics, gate duration, density gating,
sample-and-hold and slide are all driven with synthetic time and voltages.

**What the harness still cannot see**, and what therefore remains genuinely
user-tested:
- that params and jacks are wired to the right engine fields at all (the
  adapter is thin, but a swapped jack would pass every suite)
- anything visual: panel layout, labels, the display's pixels
- how any of it actually sounds — style zones are checked statistically, not
  musically
- MetaModule hardware behaviour of any kind

### Per-task test plan (Claude = native harness, User = VCV Rack)

| Task | Claude tests (add to test_pattern.cc) | User tests in Rack |
|------|----------------------------------------|--------------------|
| 4 gate/density | ✅ weight permutation ⇒ density exactness (done) | gates fire irregularly; DENSITY sweep adds one step per click; same seed+density = same pattern; gate length scales with tempo |
| 5 scales/pitch | ✅ all four done: semitone/voltage correctness over 10.8M cases, interval tables vs. independent reference, raw mode (scale position 0) 0–5 V and off-grid, octave never exceeds knob. Plus contour monotonicity, index clamping, and root-stays-quantized. | pitch output plays in-key through a VCO; root knob transposes; lock-off sounds unquantized |
| 6 velocity | ✅ range test (done) | velocity varies per note, holds during silence |
| 7 slide | ✅ done, in `engine.hh`: arrival exact, no overshoot, within one step, scales with knob and with tempo, slide=0 provably instantaneous | glide audible on flagged steps, SLIDE knob CCW kills it |
| 8 styles | ✅ done, aggregate over whole zones: bass beat bias, root/fifth bias, arp run structure, per-zone gate distributions — plus the random zone proven bit-identical to the pre-style generator | zones sound distinct by ear |
| 9 reseed | ✅ done: seed↔knob mapping proven bijective over all 1024 and clamping-safe; reseed edge semantics tested in `engine.hh` | trigger changes pattern, knobs jump to match |
| 10 CV inputs | ✅ done: every mapping's table, rounding, clamping and wrapping; unpatched is a no-op; a 14 000-point CV ROOT sweep never leaves the semitone grid | LFO into CV ROOT shifts key in tune |
| 11 display | ✅ text half done: all 1024 seeds render correct G##/S##, zone and root+scale, with clamping and truncation | display renders on hardware |
| 12 patch save | ✅ the seed rides on the params (reseed writes the knobs), and that round trip is the tested seed↔knob bijection | save/reload patch in Rack restores seed and run state |

Rule of thumb: **before implementing each task, factor the decision logic
into a pure function in a header, write its test, then wire it into
`nzzl.cc`.** The Rack adapter should stay too thin to hide bugs.

---

## Implementation status

| # | Task | Status |
|---|------|--------|
| 1 | Stub module, all jacks/knobs in VCV Rack | ✅ user-confirmed |
| 2 | Clock + Run (toggle) sequencer | ✅ user-confirmed |
| 3 | RNG + seed system | ✅ user-confirmed ("overall looks like it worked") |
| — | Design review: pattern.hh extraction, hashed seeds, per-attr streams, weight permutation, velocity latch, native test harness | ✅ tests pass |
| 4 | Gate output + density logic | 🔶 built, **awaiting user test** |
| 5 | Pitch output: scales.hh, root, octave, scale position 0 | 🔶 built, **awaiting user test** |
| 6 | Velocity output | ✅ done early (folded into 3/4) |
| 7 | Slide / portamento — now a 303 tie (+ `engine.hh` extraction) | 🔶 built, **awaiting user test** |
| 8 | Style zones — 303 bass / random / four arp directions | 🔶 built, **awaiting user test** |
| 9 | Reseed trigger + knob sync | 🔶 built, **awaiting user test** |
| 10 | CV inputs (scale/seed/root/slide) | 🔶 built, **awaiting user test** |
| 11 | Display (MetaModule TextDisplay) | 🔶 text built + tested; **rendering unverified on hardware** |
| 12 | Patch state save (dataToJson/dataFromJson) | 🔶 built, **awaiting user test** |
| — | GATE knob (three note lengths, 1–200% scaler) | 🔶 built, **awaiting user test** |
| — | ACCENT knob (three velocity layers, 0–100% scaler) | 🔶 built, **awaiting user test** |
| — | Panel layout + labels (VCV widget & MetaModule panel) | 🔶 built, **awaiting user test** |

**Task 4 test checklist (pending):** gates fire irregularly per seed; DENSITY
sweep 1→16 adds steps one at a time; same seed+density = identical pattern;
gate duration scales with clock tempo.

**Task 5 test checklist (pending):** pitch plays in key through a VCO; ROOT
transposes and stays in key; SCALE sweep changes colour without scrambling the
melodic shape; OCTAVE RANGE widens the spread; SCALE position 0 sounds
microtonal; knob turns re-pitch the held note immediately. Full procedure in
[SYSTEM_TEST_GUIDE.md](SYSTEM_TEST_GUIDE.md).

---

## Decisions that were deferred, and how they landed

All four open questions from the original deferral list are now settled.

- **Slide time constant** — `slideKnob × 0.9 × step duration`, linear ramp.
  Linear rather than exponential because it arrives *exactly* (so the note
  ends up precisely in tune), it cannot overshoot, and "reaches the target
  within the slide time" becomes a property the harness can assert. The 0.9
  cap means a glide always finishes before the next note starts.
- **Style rhythm weighting** — solved by *swapping* weights between positions
  rather than regenerating them. A swap preserves the 1–16 permutation, so
  DENSITY stays exactly linear (invariant 3) while the lowest weights get
  pulled onto strong beats.
- **CV SEED semantics** — **offset**, not absolute, and it wraps. Absolute CV
  would make the panel lie: GROUP/SUBGROUP could read 3/3 while something
  else plays, and it would fight the Task 9 knob sync, whose whole point is
  that what you hear is always reproducible from what you see. Wrapping (not
  clamping) lets an LFO sweep continuously instead of parking at an end.
- **Panel layout** — built. Three columns: controls, jacks, outputs, with the
  display across the top. The position freed by SCALE LOCK is left empty
  deliberately; an empty slot is cheaper than a control nobody uses.

### Still open

- **Whether folding SCALE LOCK into the SCALE knob was right.** Costs the
  ability to A/B back to your scale. Test T5.11b in the system test guide.
- **The 303 shaping constants** at the top of `pattern.hh` — run count, run
  length, root dominance, tie and octave-jump rates. These are the dials that
  decide whether the bass zone sounds right, and they were set by ear-free
  reasoning. Expect to move them after the first real listen.
- **Hardware display rendering.** The text is fully tested; whether
  `MetaModule::VCVTextDisplay` actually paints it is unverified and needs the
  physical module.

---

## Deviations from the handoff spec

Four places where the build knowingly differs from
[HANDOFF_SPEC.md](HANDOFF_SPEC.md). Each was a user decision, not an
oversight. Anything not listed here follows the spec.

| Spec says | We build | Why |
|---|---|---|
| RUN: "Gate high = runs, gate low = holds" | RUN is a **toggle trigger** — a rising edge flips running/stopped | User request, early on. A toggle works with a momentary button; a level needs a latching gate source. |
| SCALE LOCK: an on/off toggle beside a 12-scale knob | **Position 0 of the SCALE knob** is unquantized; 13 positions total | User request, 2026-09-20. One control answers "how are these notes pitched?" instead of two that can disagree; frees a panel position; lets CV SCALE reach raw mode without a second jack. Costs the ability to A/B back to your scale. |
| Bassline: "notes cluster on beats 1 and 3, sparse off-beat, gate mostly short" | **303 acid**: runs of sixteenths, ties, root-hammering, octave jumps | User request, 2026-09-20: *"ideally id want the bass algos to simulate 303 style sequences."* The spec's description is a dub/house bassline; the module is named after and aimed at acid. |
| Gate length: per-step, seed-derived | **Three note lengths per pattern** + a GATE knob (1–200%) scaling all three; velocity likewise becomes three layers + an ACCENT knob | User request, 2026-09-20, after a reference module. A small audible set you can scale beats sixteen unrelated numbers you cannot. |

**Everything else tracks the spec**, including the bits an earlier pass had
wrong: zone boundaries (1–10 / 11–21 / 22–32), the arp subgroup directions,
the twelve scales in the spec's order, and the CV jacks' override-vs-offset
semantics.

---

## The bass zone, and the bug that shaped it

**What went wrong.** The first cut of the bass style pulled the four lowest
weights onto a fixed `BEAT_PRIORITY` table — steps 0, 8, 4, 12. Since those
four weights are exactly what DENSITY 4 plays, **all 320 bass seeds played
the same four positions in the bar.** The zone had 320 seeds and one rhythm.
Nothing caught it because the test measured "percentage of notes on a beat",
which a collapsed zone scores *perfectly* on.

**The fix, and the test that would have caught it.** Bass now builds a
per-seed priority order and lays weights 1–16 along it:

1. the downbeat leads, 85% of seeds
2. four runs of 2–4 consecutive sixteenths, at seed-chosen start points
3. whatever is left, in order

Laying 1–16 along a permutation of positions is still a permutation, so
DENSITY stays exactly linear. `test_bass_rhythm_varies_between_seeds` counts
**distinct active-step sets across the whole zone** — the measurement that
actually asks the question — and asserts floors at every density:

| DENSITY | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| distinct bass rhythms (of 320 seeds) | 16 | 29 | 46 | **100** | 145 | 189 |
| distinct random rhythms (of 352) | 16 | 111 | 265 | 317 | 339 | 346 |

Bass stays deliberately more constrained than random — that is what makes it
a style — and the test asserts that direction too, so "fix" by making bass
uniform would fail just as loudly as the collapse did.

**Why 303 and not the spec's bassline.** A TB-303 line is not notes on beats
1 and 3. It is sixteenth-note runs broken by rests, with slides welding notes
into each other, the root hammered far more than anything else, and the odd
octave jump. Shaping constants live at the top of `pattern.hh`; the measured
result is 63% of notes immediately followed by another (random zone: 34%),
55% root, 18% octave-up, 32% tied.

**A slide is a tie.** On a 303 the slide button does not just glide the
pitch — it holds the gate through into the next note. `engine.hh` gives a
tied step a gate of `TIE_OVERHANG` (1.02) steps so the next step re-arms the
timer before it can fall. Without this, "slide" is two separate notes with a
portamento between them, which is not the sound.

---

## GATE and ACCENT

A pattern generates **three note lengths** (short 0.12–0.30, mid 0.35–0.60,
long 0.65–0.95 of a step) and each step indexes one. The **GATE** knob
(1–200%) scales all three together.

The interesting consequence is at the top of the range: at 200% the long
notes exceed a step and tie, while the short ones are still only 0.6 of a
step and stay staccato. So the knob **morphs** a pattern from staccato toward
partly legato rather than flipping everything at once — which is exactly what
you want for acid, and what a single continuous per-step gate length could
never do.

**ACCENT** (0–100%) works the same way over three velocity layers (off 0.22,
low 0.55, high 1.0). At 0 every note sits at the same mid level; at 100 the
layers are fully apart — 7.8 V of measured spread at the output.

---

## Style zones

GROUP splits the 1024 seeds into three zones, per the spec: **1–10 BASS**,
**11–21 RAND**, **22–32 ARP**. Inside the ARP zone the SUBGROUP knob picks
the direction — **1–8 up, 9–16 down, 17–24 up-down, 25–32 down-up**.

The style layer runs *after* the base pattern, from its own `SALT_STYLE`
stream, and **STYLE_RANDOM is a deliberate no-op**. That is what invariant 2
requires — new needs get new salts, existing draws never change — and it means
the middle zone stays bit-identical to the pre-Task-8 module.
`test_random_zone_unchanged` proves this for all 384 random-zone seeds, and it
is the single most important test in that suite: it is the guarantee that
adding character to the module didn't silently retune everything else.

| | BASS | RAND | ARP |
|---|---|---|---|
| Rhythm | per-seed runs of sixteenths, downbeat-led | untouched | untouched |
| Pitch | root-dominated acid vocabulary | untouched | sorted, read out in the subgroup's direction |
| Octave | +1 jumps on 18% of notes | untouched | spread across the range |
| Gate | short and mid only | all three lengths | one length throughout |
| Ties | 32% of steps | 25% | none |
| Measured | 63% of notes run into the next (random 34%), 55% root, 100 distinct rhythms at DENSITY 4 | at chance | every pattern monotonic in its direction |

---

## CV input conventions (Task 10)

The spec is explicit and the four jacks are **not** all the same. An earlier
pass made them all offsets; this matches the wording instead.

| Input | Spec wording | Behaviour |
|---|---|---|
| cvSCALE | "CV **override** for scale selection" | patched = the voltage names the position, knob ignored. 0–10 V spans all 13, clamped |
| cvROOT | "CV **override** for root note" | patched = the voltage names the key, knob ignored. V/oct, snapped to whole semitones, wraps |
| cvSEED | "CV **offset** for seed selection" | added to the knob seed. 10 V spans all 1024, wraps so an LFO sweeps continuously |
| cvSLIDE | "summed with seed-derived slide, **scaled by attenuator knob**" | `knob × clamp01(seedFlag + cv)` — see below |

**The slide chain's order matters.** Because the knob multiplies the sum
rather than adding to it, CCW kills slide outright however much CV is
arriving — which is what the spec means by *"CCW = no slide regardless of
seed"*. And because the CV is summed against each step's own flag, a patched
CV puts glide on steps the seed never flagged: sweep the jack and the line
goes from articulated to fully legato. The knob and the CV therefore cannot
be pre-mixed into one number; the engine needs both.

**cvROOT snapping to whole semitones is load-bearing, not cosmetic.** A smooth
CV passed straight into the pitch formula would de-quantize the output and
break the ROOT invariant. `test_cv_root_never_dequantizes` sweeps ~14 000
voltages and checks every resulting note is still on the semitone grid and
still in key.

---

## Patch state (Task 12)

The seed rides on the GROUP/SUBGROUP params, which Rack saves itself —
**including after a RESEED**, because `reseed()` writes the knobs rather than
shadowing them. So `dataToJson` only needs to carry state that is *not* a
param: whether RUN has stopped playback.

The step position is deliberately **not** saved. A reloaded patch should start
somewhere predictable rather than halfway through a phrase.

---

## Display (Task 11)

`display.hh` builds the three lines — `G## S##`, the zone name, and
`<root> <scale>` — with no heap, no `<string>` and no stdio, so all 1024 seeds
are harness-checkable and nothing can allocate on the audio thread. Only the
pixels are hardware's problem.

The widget derives from `MetaModule::VCVTextDisplay` when `NZZL_METAMODULE` is
defined and from `LightWidget` otherwise, and draws through `draw()` rather
than `drawLayer()` because `drawLayer` is a Rack-only lights-layer concept.
The TTF load is Rack-only and guarded, so a failed font load cannot skip
`nvgText` on hardware.
