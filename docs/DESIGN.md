# NZZL — Design Record

This document is the authoritative record of **why** each decision was made.
Two companions: **[SPEC_AS_BUILT.md](SPEC_AS_BUILT.md)** describes **what** the
module currently is, and **[HANDOFF_SPEC.md](HANDOFF_SPEC.md)** is the frozen
original requirements.

The source spec lives in the repo as **[HANDOFF_SPEC.md](HANDOFF_SPEC.md)** (copied
in 2026-09-20 — before that, three sessions were building against a one-line
paraphrase of it). That file is the requirements; this one records **how**
we're building them and **why**, including every place we deliberately
deviate — see § Deviations from the handoff spec.

---

## Architecture

```
src/
├── rng.hh       Xorshift32 PRNG. Pure, no deps.
├── pattern.hh   Roll entropy → one Pattern per STYLE position. Pure.
├── roll.hh      Which row is playing, when it changes, the save format. Pure.
├── scales.hh    Scale tables + degree/octave mapping + quantization. Pure.
├── engine.hh    The realtime state machine: triggers, clock, gate, slide,
│                sample-and-hold. Pure — fed fake time and fake voltages.
├── cv.hh        CV input → parameter mapping. Pure.
├── display.hh   Panel text building. Pure, no heap, no stdio.
├── nzzl.cc      Thin Rack adapter: params/jacks ↔ the headers above.
└── plugin.cc    Plugin registration.
tests/
├── rolls.hh         Test helper: reproducible "many rolls" (not compiled in)
├── test_pattern.cc  Roll generation: density, ranges, lanes, row sharing
├── test_roll.cc     Roll player (STYLE / Generate on the wrap) + save/load
├── test_scales.cc   Pitch quantizer
├── test_engine.cc   Realtime sequencer, driven by synthetic time/voltages
├── test_styles.cc   STYLE rows (aggregate, statistical)
├── test_cv.cc       CV mappings
├── test_display.cc  Panel text
├── refgen.cc        Example-roll table generator (not a test; run by hand)
└── run_tests.sh     Compiles and runs every test_*.cc (each has its own
                     main()). Use after every pure-logic change.
```

**Rule: anything deterministic lives in a pure header and gets tests.**
`nzzl.cc` only holds realtime concerns: triggers, timers, voltages, knob
reads. This is what makes the module testable by Claude without VCV Rack.

The same `nzzl.cc` compiles for both VCV Rack (local dev, `build-vcv/`) and
MetaModule (CI ARM cross-compile) via the SDK's rack-interface shim.

---

## Roll invariants (do not break)

**Rules replace seeds** (item 16, decided 2026-11-13, built 2026-11-16).
There is no seed to dial or return to: every Generate is a fresh roll,
shaped by the rules (the STYLE rows), and the whole roll is saved with the
patch. The two seed invariants this file used to carry — hashed seed indices
and salted per-attribute streams that must never change how they draw — are
**gone**; they existed only so upgrades would not change a saved seed's
sound, and a saved patch now stores the pattern itself.

1. **Randomness only at Generate.** The RESEED jack's rising edge and the
   RANDOM button (and placing a fresh module) draw a `RollEntropy` — one
   fresh 32-bit value per lane — from `random::u32()` in `nzzl.cc`. Everything
   after that draw is pure: `rollPatterns()` in `pattern.hh` turns it into a
   `Roll`, which the harness can reproduce exactly. `process()` only reads
   precomputed `StepData`; nothing random happens at playback.

2. **Every knob position is precomputed at the roll.** A `Roll` holds one
   `Pattern` per STYLE position, so turning STYLE is instant and returning to
   a position gives the same pattern until the next Generate. A knob never
   re-rolls. Verified by `test_style_returns_same_pattern`.

3. **DENSITY is exact and nested: one shuffled order per row.** Weights are a
   Fisher-Yates permutation of 1–16 (the BASS row re-lays 1–16 along its run
   order, still a permutation), so density N plays exactly N steps and
   turning it up only ever adds steps. *Why:* the spec says density should
   feel linear and density 1 must always play something. Verified by
   `test_density_exact_and_nested` over every row of 1024 rolls, at every
   LENGTH and SHIFT.

4. **Lanes draw independently within a roll.** Each attribute (weight, pitch,
   gate lengths, gate index, accent, slide, octave, swing, BASS shaping, ARP
   accents) runs its own generator from its own fresh draw. Not for patch
   stability any more — for statistical independence, and so a change to how
   one lane draws never shifts another's values. Verified by
   `test_lanes_independent`.

5. **The roll survives a save exactly.** `dataToJson` stores every row as a
   hex string of 32-bit words (floats by bit pattern) plus `rollFormat`;
   `dataFromJson` validates it (`patternValid`) and refuses anything
   malformed, keeping the fresh roll instead. Bump `ROLL_FORMAT` when the
   layout changes. Verified by `test_roundtrip` (1024 rolls, bit-exact) and
   `test_bad_saves_are_refused`. Test-era patches from before item 16 load
   with a fresh roll — accepted.

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
the grid or out of key. `test_root_stays_quantized` asserts, for 256 rolls ×
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
- Schmitt trigger thresholds 0.1 V / 2.0 V for clock, run and reseed.

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
| 4 gate/density | ✅ weight permutation ⇒ density exact and nested, every row of 1024 rolls | gates fire irregularly; DENSITY sweep adds one step per click; same roll+density = same pattern; gate length scales with tempo |
| 5 scales/pitch | ✅ all four done: semitone/voltage correctness over 10.8M cases, interval tables vs. independent reference, raw mode (scale position 0) 0–5 V and off-grid, octave never exceeds knob. Plus contour monotonicity, index clamping, and root-stays-quantized. | pitch output plays in-key through a VCO; root knob transposes; lock-off sounds unquantized |
| 6 velocity | ✅ range test (done) | velocity varies per note, holds during silence |
| 7 slide | ✅ done, in `engine.hh`: arrival exact, no overshoot, within one step, scales with knob and with tempo, slide=0 provably instantaneous | glide audible on flagged steps, SLIDE knob CCW kills it |
| 8 styles | ✅ done, aggregate over the same STYLE row of 1024 rolls: bass run structure, downbeat and root bias, arp direction in every row, per-style gate distributions, RAND row = the unshaped base | styles sound distinct by ear |
| 9 reseed → Generate | ✅ edge semantics in `engine.hh`; roll player: Generate and STYLE changes land on the wrap, STYLE returns the same row | trigger/button changes the pattern on the next loop |
| 10 CV inputs | ✅ done: every mapping's table, rounding, clamping and wrapping; unpatched is a no-op; a 14 000-point CV ROOT sweep never leaves the semitone grid; CV DENSITY clamped and monotonic | LFO into CV ROOT shifts key in tune; CV DENSITY adds/removes hits |
| 11 display | ✅ text half done: every density × length renders `d/l`, every STYLE its label, root+scale, with clamping and truncation | display renders on hardware |
| 12 patch save | ✅ the whole roll round-trips bit-exactly through the save format; damaged saves are refused | save/reload patch in Rack restores the exact pattern and run state |
| 16 rules replace seeds | ✅ density exact+nested, rhythm varies across rolls, arp direction, scale safety, save/load round trip, lanes independent | Generate gives a new line; STYLE switches rows instantly and returns; saved patch reloads identically |

Rule of thumb: **before implementing each task, factor the decision logic
into a pure function in a header, write its test, then wire it into
`nzzl.cc`.** The Rack adapter should stay too thin to hide bugs.

---

## Implementation status

| # | Task | Status |
|---|------|--------|
| 1 | Stub module, all jacks/knobs in VCV Rack | ✅ user-confirmed |
| 2 | Clock + Run (toggle) sequencer | ✅ user-confirmed |
| 3 | RNG + seed system | ✅ user-confirmed ("overall looks like it worked") — **replaced by item 16** |
| — | Design review: pattern.hh extraction, hashed seeds, per-attr streams, weight permutation, velocity latch, native test harness | ✅ tests pass |
| 4 | Gate output + density logic | 🔶 built, **awaiting user test** |
| 5 | Pitch output: scales.hh, root, octave, scale position 0 | 🔶 built, **awaiting user test** |
| 6 | Velocity output | ✅ done early (folded into 3/4) |
| 7 | Slide / portamento — now a 303 tie (+ `engine.hh` extraction) | 🔶 built, **awaiting user test** |
| 8 | Style zones — 303 bass / random / four arp directions | 🔶 built, **awaiting user test** |
| 9 | Reseed trigger + knob sync — knob sync gone with item 16; RESEED now = Generate | 🔶 built, **awaiting user test** |
| 10 | CV inputs (scale/seed→density/root/slide) | 🔶 built, **awaiting user test** |
| 11 | Display (MetaModule TextDisplay) | 🔶 text built + tested; **rendering unverified on hardware** |
| 12 | Patch state save (dataToJson/dataFromJson) | 🔶 built, **awaiting user test** |
| — | GATE knob (three note lengths, 1–200% scaler) | 🔶 built, **awaiting user test** |
| — | ACCENT knob (three velocity layers, 0–100% scaler) | 🔶 built, **awaiting user test** |
| — | Panel layout + labels (VCV widget & MetaModule panel) | 🔶 built, **awaiting user test** |
| 16 | Rules replace seeds: rolls, STYLE knob, CV DENSITY, `8/16` display, roll saved with the patch | 🔶 built 2026-11-16, **awaiting user test** |

**Task 4 test checklist (pending):** gates fire irregularly per roll; DENSITY
sweep 1→16 adds steps one at a time; same roll+density = identical pattern;
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
- **CV SEED semantics** — superseded by item 16: the jack is now **CV
  DENSITY**, an **offset** added to the DENSITY knob, 10 V = the full 1–16
  range, **clamped** (decided 2026-11-16). Offset rather than override so the
  knob stays the base you dialled in; clamped rather than wrapped because
  wrapping 16 → 1 would empty the bar in one step. Because DENSITY nests, a
  CV sweep only adds or removes hits and never reshuffles them. (The old
  CV SEED was an offset that wrapped across the 1024 seeds.)
- **Panel layout** — built. Three columns: controls, jacks, outputs, with the
  display across the top. The position freed by SCALE LOCK is left empty
  deliberately; an empty slot is cheaper than a control nobody uses. Item 16
  put STYLE where GROUP was and left SUBGROUP's position (and its param id)
  for PITCH LENGTH, item 7.

**Settled 2026-09-21:** folding SCALE LOCK into the SCALE knob is confirmed
as the approach. It can be revisited if it proves awkward in practice, but it
is no longer an open question.

Ideas parked for later — pitch offset, slide slope, swing — live in
[SPEC_AS_BUILT.md § Future additions to consider](SPEC_AS_BUILT.md#future-additions-to-consider),
not here, so this file stays about decisions already made.

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

Five places where the build knowingly differs from
[HANDOFF_SPEC.md](HANDOFF_SPEC.md) on a **user decision**. The complete
difference list — these five plus the engineering choices and the four
requirements that were implemented wrong and later fixed — is in
[SPEC_AS_BUILT.md § Differences from the original requirements](SPEC_AS_BUILT.md#differences-from-the-original-requirements).

| Spec says | We build | Why |
|---|---|---|
| RUN: "Gate high = runs, gate low = holds" | RUN is a **toggle trigger** — a rising edge flips running/stopped | User request, early on. A toggle works with a momentary button; a level needs a latching gate source. |
| SCALE LOCK: an on/off toggle beside a 12-scale knob | **Position 0 of the SCALE knob** is unquantized; 13 positions total | User request, 2026-09-20. One control answers "how are these notes pitched?" instead of two that can disagree; frees a panel position; lets CV SCALE reach raw mode without a second jack. Costs the ability to A/B back to your scale. |
| Bassline: "notes cluster on beats 1 and 3, sparse off-beat, gate mostly short" | **303 acid**: runs of sixteenths, ties, root-hammering, octave jumps | User request, 2026-09-20: *"ideally id want the bass algos to simulate 303 style sequences."* The spec's description is a dub/house bassline; the module is named after and aimed at acid. |
| Seeds: 1024 recallable seeds on GROUP / SUBGROUP, CV SEED offset, "all output derived from a seed" | **Rules replace seeds** (item 16): Generate rolls a fresh pattern, precomputed for every STYLE position and saved with the patch; a six-position **STYLE** knob replaces GROUP / SUBGROUP; CV SEED becomes **CV DENSITY**; display line 1 shows `8/16` | Gerardo, 2026-11-13 / 11-16, after studying Sting 2 and TB-3PO: he has never wanted to recall a seed. Breaking test-era patches accepted. |
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
per-roll priority order and lays weights 1–16 along it:

1. the downbeat leads, 85% of rolls
2. four runs of 2–4 consecutive sixteenths, at rolled start points
3. whatever is left, in order

Laying 1–16 along a permutation of positions is still a permutation, so
DENSITY stays exactly linear. `test_bass_rhythm_varies_across_rolls` counts
**distinct active-step sets across the BASS row of 1024 rolls** — the
measurement that actually asks the question — and asserts floors at every
density (measured 2026-11-16, after item 16):

| DENSITY | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| distinct bass rhythms (of 1024 rolls) | 16 | 29 | 67 | **201** | 285 | 366 |
| distinct random rhythms (of 1024) | 16 | 120 | 467 | 797 | 928 | 967 |

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

## STYLE

Item 16 replaced the GROUP zones and SUBGROUP arp directions with one
**STYLE** knob, six detents: **BASS, RAND, ARP↑, ARP↓, ARP↕, ARP↔**. Each
position is one row of the roll. No CV: Gerardo does not expect to modulate
style (decided 2026-11-13). The position index is saved in patches: append
only.

Every roll generates one **base pattern**. The RAND row *is* the base; BASS
and the four ARPs are shaped from it, so within one roll RAND and the ARPs
share notes and rhythm (the ARPs reorder the pitches), while BASS rebuilds
rhythm and pitch in the acid vocabulary. `test_rows_share_a_base` proves the
RAND row equals the base and that every ARP row keeps the base's pitch set and
rhythm. The old "RAND zone bit-identical to the pre-style generator" property
is gone with the seeds it protected.

| | BASS | RAND | ARP |
|---|---|---|---|
| Rhythm | per-roll runs of sixteenths, downbeat-led | untouched | untouched |
| Pitch | root-dominated acid vocabulary | untouched | sorted, read out in the STYLE position's direction |
| Octave | +1 jumps on 18% of notes | untouched | spread across the range |
| Gate | short and mid only | all three lengths | one length throughout |
| Ties | 32% of steps | 25% | the base's flags (25%) |
| Measured | 63% of notes run into the next (RAND 33%), 55% root, 201 distinct rhythms at DENSITY 4 over 1024 rolls | at chance | every row monotonic in its direction |

---

## CV input conventions (Task 10)

The spec is explicit and the four jacks are **not** all the same. An earlier
pass made them all offsets; this matches the wording instead.

| Input | Spec wording | Behaviour |
|---|---|---|
| cvSCALE | "CV **override** for scale selection" | patched = the voltage names the position, knob ignored. 0–10 V spans all 13, clamped |
| cvROOT | "CV **override** for root note" | patched = the voltage names the key, knob ignored. V/oct, snapped to whole semitones, wraps |
| cvDENSITY (was cvSEED) | "CV **offset** for seed selection" — item 16 kept the offset, moved it to DENSITY | added to the DENSITY knob. 10 V spans 1–16, clamped |
| cvSLIDE | "summed with seed-derived slide, **scaled by attenuator knob**" | `knob × clamp01(stepFlag + cv)` — see below |

**The slide chain's order matters.** Because the knob multiplies the sum
rather than adding to it, CCW kills slide outright however much CV is
arriving — which is what the spec means by *"CCW = no slide regardless of
seed"*. And because the CV is summed against each step's own flag, a patched
CV puts glide on steps the roll never flagged: sweep the jack and the line
goes from articulated to fully legato. The knob and the CV therefore cannot
be pre-mixed into one number; the engine needs both.

**cvROOT snapping to whole semitones is load-bearing, not cosmetic.** A smooth
CV passed straight into the pitch formula would de-quantize the output and
break the ROOT invariant. `test_cv_root_never_dequantizes` sweeps ~14 000
voltages and checks every resulting note is still on the semitone grid and
still in key.

---

## Patch state (Task 12, reworked by item 16)

`dataToJson` stores the run state and **the whole roll** — every STYLE row,
not just the one playing — so a reloaded patch plays what you heard and the
STYLE knob still behaves the same. If a Generate is still waiting for the loop
to wrap, that pending roll is what gets saved, since it is what was about to
play. The format and its validation live in `roll.hh` (invariant 5).

The step position is deliberately **not** saved. A reloaded patch should
start somewhere predictable rather than halfway through a phrase.

---

## Display (Task 11)

`display.hh` builds the three lines — `8/16` (effective density / length,
item 16), the STYLE name, and `<root> <scale>` — with no heap, no `<string>`
and no stdio, so every combination is harness-checkable and nothing can
allocate on the audio thread. Line 1 shows density clamped to the loop, the
way the engine plays it, and includes CV DENSITY. When PITCH LENGTH (item 7)
lands it will read `8/16 P5`. Only the
pixels are hardware's problem.

The widget derives from `MetaModule::VCVTextDisplay` when `NZZL_METAMODULE` is
defined and from `LightWidget` otherwise, and draws through `draw()` rather
than `drawLayer()` because `drawLayer` is a Rack-only lights-layer concept.
The TTF load is Rack-only and guarded, so a failed font load cannot skip
`nvgText` on hardware.
