# NZZL — Design Record

This document is the authoritative record of design decisions. The full
product spec (jacks, knobs, seed system, styles, scales) lives in the
original handoff requirements; this file records **how** we're building it
and **why**, so any session can pick up the work without re-deriving it.

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

*Why:* one control now answers the whole question "how are these notes
pitched?" instead of two that interact. It frees a panel position, which
matters on MetaModule, and it means CV SCALE (Task 10) can sweep into and out
of raw mode without a second CV input. **The cost, accepted knowingly:** you
can no longer A/B back to the scale you had — going to raw and back means
finding your scale again on the knob. Blues was dropped to make room.

**Scale order is an on-disk contract.** `SCALE_PARAM` stores an index, so
inserting a scale mid-list would retune every saved patch. **Append only,
never reorder.** The frozen order is:

| # | Scale | # | Scale |
|---|-------|---|-------|
| **0** | **Unquantized** | 6 | Phrygian Dominant |
| 1 | Chromatic | 7 | Lydian |
| 2 | Major | 8 | Mixolydian |
| 3 | Natural Minor *(default)* | 9 | Harmonic Minor |
| 4 | Dorian | 10 | Melodic Minor |
| 5 | Phrygian | 11 | Minor Pentatonic |

*Why this set:* twelve slots were fixed by the knob range, and position 0 now
spends one of them on raw mode. Locrian, major pentatonic and Blues are the
casualties. Verified against an independently written reference table in
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
| 7 | Slide / portamento (+ `engine.hh` extraction) | 🔶 built, **awaiting user test** |
| 8 | Style zones (bassline / random / arp) | 🔶 built, **awaiting user test** |
| 9 | Reseed trigger + knob sync | 🔶 built, **awaiting user test** |
| 10 | CV inputs (scale/seed/root/slide) | 🔶 built, **awaiting user test** |
| 11 | Display (MetaModule TextDisplay) | 🔶 text built + tested; **rendering unverified on hardware** |
| 12 | Patch state save (dataToJson/dataFromJson) | 🔶 built, **awaiting user test** |
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
- **Whether Blues should have stayed** in the scale list instead of Melodic
  Minor. Free to change until patches exist.
- **Hardware display rendering.** The text is fully tested; whether
  `MetaModule::VCVTextDisplay` actually paints it is unverified and needs the
  physical module.

---

## Style zones (Task 8)

GROUP splits the 1024 seeds into three zones: **1–10 BASS**, **11–22 RAND**,
**23–32 ARP**.

The style layer runs *after* the base pattern, from its own `SALT_STYLE`
stream, and **STYLE_RANDOM is a deliberate no-op**. That is what invariant 2
requires — new needs get new salts, existing draws never change — and it means
the middle zone stays bit-identical to the pre-Task-8 module.
`test_random_zone_unchanged` proves this for all 384 random-zone seeds, and it
is the single most important test in that suite: it is the guarantee that
adding character to the module didn't silently retune everything else.

| | BASS | RAND | ARP |
|---|---|---|---|
| Rhythm | lowest weights pulled onto strong beats | untouched | untouched |
| Pitch | anchored on root and fifth | untouched | sorted into 4-step runs |
| Gate | shorter, punchier (0.10–0.58) | as generated (0.10–0.90) | even, mid (0.25–0.49) |
| Measured | 78% of DENSITY-4 notes on the beat (chance 25%), 69% on root/fifth (chance 31%) | at chance | 100% of 4-step blocks are runs (random zone 12%) |

---

## CV input conventions (Task 10)

**Every CV input is an offset on its knob, never an absolute value** — see the
CV SEED note above for why.

| Input | Scaling | Out of range |
|---|---|---|
| cvSCALE | 1 V per scale position | clamps (nothing musical past either end) |
| cvROOT | V/oct — 1 V = 12 semitones | wraps (past B is just C again) |
| cvSEED | 10 V spans all 1024 seeds | wraps (so an LFO sweeps continuously) |
| cvSLIDE | 10 V spans the knob's 0–1 travel | clamps |

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
