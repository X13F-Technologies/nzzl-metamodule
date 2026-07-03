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
├── pattern.hh   Seed → StepData[16]. Pure, no rack.hpp. THE core logic.
├── scales.hh    (Task 5) Scale tables + quantization. Pure.
├── nzzl.cc      Thin Rack adapter: params/jacks ↔ pattern + realtime state.
└── plugin.cc    Plugin registration.
tests/
├── test_pattern.cc  Native harness — compiles WITHOUT Rack/MetaModule.
└── run_tests.sh     c++ -std=c++20 → run. Use after every pure-logic change.
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

**Current coverage honestly stated:** the harness covers pattern *generation*
only. Realtime sequencer behavior in `nzzl.cc` (clock edges, run toggle,
gate timers, output voltages) is NOT covered by the harness — it is
user-tested in Rack. If realtime logic grows complex, extract it into a pure
`engine.hh` (a step-machine struct fed fake time/voltage samples) to make it
Claude-testable too. Consider this at Task 7 (slide) when timing logic gets
harder to eyeball.

### Per-task test plan (Claude = native harness, User = VCV Rack)

| Task | Claude tests (add to test_pattern.cc) | User tests in Rack |
|------|----------------------------------------|--------------------|
| 4 gate/density | ✅ weight permutation ⇒ density exactness (done) | gates fire irregularly; DENSITY sweep adds one step per click; same seed+density = same pattern; gate length scales with tempo |
| 5 scales/pitch | quantizer maps every (scale, root, degree, octave) to correct semitone/voltage; all 12 scales' interval tables match music theory; scale-lock-off maps raw 0–5 V; octave range never exceeds knob setting | pitch output plays in-key through a VCO; root knob transposes; lock-off sounds unquantized |
| 6 velocity | ✅ range test (done) | velocity varies per note, holds during silence |
| 7 slide | if extracted to engine.hh: glide reaches target within slide time, no overshoot; slide=0 ⇒ instantaneous | glide audible on flagged steps, SLIDE knob CCW kills it |
| 8 styles | statistical: bassline seeds (groups 1–10) bias pitch toward degrees 0/4 and beats 1/3; random seeds ~uniform; per-style gate-length distributions differ. Assert on aggregate counts across all seeds in each zone | zones sound distinct by ear |
| 9 reseed | reseed maps any seed_index 0–1023 back to correct group/subgroup knob values (round-trip) | trigger changes pattern, knobs jump to match |
| 10 CV inputs | CV→value mapping functions (voltage → scale index, root, seed offset) are pure — test the mapping tables/clamping | LFO into CV ROOT shifts key smoothly |
| 11 display | zone-name lookup (group,subgroup → "BASS"/"ARP↑"/…) is pure — test all 1024 | display renders on hardware |
| 12 patch save | if state serialization is factored into a pure helper: round-trip seed_index through it | save/reload patch in Rack restores seed |

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
| 5 | Pitch output: scales.hh, root, octave, scale lock | ⬜ next |
| 6 | Velocity output | ✅ done early (folded into 3/4) |
| 7 | Slide / portamento | ⬜ |
| 8 | Style differentiation (bassline/random/arp weighting) | ⬜ |
| 9 | Reseed trigger + knob sync | ⬜ |
| 10 | CV inputs (scale/seed/root/slide) | ⬜ |
| 11 | Display (MetaModule TextDisplay) | ⬜ hardware needed |
| 12 | Patch state save (dataToJson/dataFromJson) | ⬜ |
| — | Panel layout + labels (VCV widget & MetaModule panel) | ⬜ deliberately last |

**Task 4 test checklist (pending):** gates fire irregularly per seed; DENSITY
sweep 1→16 adds steps one at a time; same seed+density = identical pattern;
gate duration scales with clock tempo.

---

## Deferred decisions

- **Panel layout/labels:** deliberately postponed until features are complete;
  current VCV widget is a plain testing harness (2-column knobs, jack rows).
- **Slide time constant** (spec says "derived from clock tempo feel") — exact
  formula TBD at Task 7.
- **Style rhythm weighting details** (bassline beats-1-and-3 clustering) —
  needs a weight-shaping approach that preserves the 1–16 permutation
  invariant, decide at Task 8. Likely: generate permutation, then bias which
  *positions* get low weights, keeping the permutation property.
- **CV SEED semantics** (offset vs absolute) — decide at Task 10.
