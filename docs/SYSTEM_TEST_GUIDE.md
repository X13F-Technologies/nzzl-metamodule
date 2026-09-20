# NZZL — System Test Guide

The acceptance procedure for the whole module. Work through it in VCV Rack 2
(Parts 1–6), then on MetaModule hardware (Part 7).

**What this guide is for.** The native harness (`./tests/run_tests.sh`) proves
that pattern *generation* and pitch *quantization* are correct — it checks
millions of cases that no human could audit by ear. It cannot see a single
volt of what actually leaves the module: clock edges, gate timers, output
voltages, knob response and the panel itself all live in `nzzl.cc`, which
needs a real Rack. **That gap is exactly what this guide covers.**

| | Verified by | Where |
|---|---|---|
| Pattern generation, quantizer math, style statistics | `./tests/run_tests.sh` | automated |
| Clock, gate timing, run toggle, slide, CV maths | `./tests/run_tests.sh` (via `engine.hh`) | automated |
| **That jacks and knobs are wired to the right things** | **this guide** | manual |
| **Panel, labels, display pixels** | **this guide** | manual |
| **How any of it actually sounds** | **this guide** | manual |

The engine extraction at Task 7 moved a lot of this guide's old burden into
the harness. What is left is the part a machine genuinely cannot judge: the
adapter is thin, but a swapped jack would still pass every suite; and no test
can tell you whether the arp zone sounds like an arp.

Sections are marked **[NOW]** (built, test it) or **[PENDING]** (not built
yet — acceptance criteria recorded in advance, skip for now).

---

## 0. Setup

### 0.1 Build and install

```bash
cd ~/Documents/nzzl-metamodule
./tests/run_tests.sh                                   # must print "all suites passed"
cmake --build build-vcv && cmake --install build-vcv
```

Then **fully quit and restart VCV Rack 2** — it only scans plugins at launch.
If NZZL doesn't appear in the browser, check
`~/Library/Application Support/Rack2/log.txt` for a load error.

### 0.2 The reference patch

Build this once and save it as `nzzl-test.vcv`. Everything below assumes it.

```
  [LFO-1]  sq out ──────────────► NZZL  CLOCK
                                       RUN      (leave unpatched for now)

  NZZL  CV PITCH ───────────────► [VCO-1]  V/OCT
  NZZL  GATE     ──┬────────────► [ADSR]   GATE
                   └────────────► [Scope]  CH1
  NZZL  VELOCITY ──┬────────────► [VCA-1]  CV
                   └────────────► [Scope]  CH2

  [VCO-1] SAW ────► [VCA-1] IN ──► [Audio-8] L/R
  [ADSR]  ENV ────► [VCA-1] CV     (if your VCA has two CV inputs; otherwise
                                    patch ADSR → VCA CV and velocity → Scope only)
```

Modules are all from **VCV Fundamental**: LFO-1, VCO-1, ADSR, VCA-1, Scope,
Audio-8.

**Settings to start with**
- LFO-1: FREQ ≈ 2 Hz (≈120 BPM at 1 step per pulse), square output
- ADSR: fast A, short D, low S, short R — so you hear gate length clearly
- VCO-1: octave/tune so the pattern sits in a bass register
- Scope: TIME knob set so ~4 seconds fit on screen

**NZZL knobs**
`GROUP 1 · SUBGROUP 1 · DENSITY 8 · LENGTH 16 · CLK DIV 1 · OCT RANGE 2 ·
ROOT C · SCALE Natural Minor · SLIDE 0`

The panel is three columns — controls left, jacks middle, outputs bottom
right — with a three-line display across the top showing the seed, the zone
and the root+scale.

> **There is no SCALE LOCK switch.** Unquantized is **position 0 of the SCALE
> knob**. One control, no interaction to reason about.

### 0.3 Useful Rack habits for this guide

- **Right-click a knob → type an exact value.** Essential wherever the guide
  says "set DENSITY to 4".
- **Hover a knob** to read its tooltip. ROOT and SCALE report real names
  (`Natural Minor`, `G#`, `Unquantized`), not raw numbers.
- **Scope** is your voltmeter — it prints the measured voltage per channel.
- Ctrl/Cmd-click a cable to delete it; drag from an output to duplicate.

### 0.4 How to record results

Every test has an ID (e.g. **T5.3**). Log outcomes in the sign-off sheet in
Part 8. A test is **PASS** only if the stated expected result is what you
actually observe — "close enough" is a FAIL with a note, because these are
the behaviours the automated suite cannot re-check for you.

---

## 1. Smoke test — 5 minutes **[NOW]**

Stop here and report if any of these fail; the rest of the guide won't be
meaningful.

| ID | Do | Expect |
|---|---|---|
| S.1 | Add NZZL to a rack | Module appears, no Rack crash, no red "failed to load" panel |
| S.2 | Look at the panel | 9 knobs, 7 input jacks, 3 output jacks, all labelled, nothing overlapping or off the edge. The knob position to the right of SLIDE is intentionally empty |
| S.2b | Look at the display | Three lines: `G01 S01`, `BASS`, `C MINOR`. Legible, not clipped |
| S.3 | Start the LFO clock | Gate LED activity on the Scope; you hear notes |
| S.4 | Let it run 60 s | No dropouts, no clicks on every note, no runaway CPU (right-click module → CPU meter stays low) |
| S.5 | Remove and re-add the module | No crash |

---

## 2. Clock, Run, Divide, Length **[NOW]** — Tasks 1–2

| ID | Do | Expect |
|---|---|---|
| T2.1 | Unpatch CLOCK entirely | Sequencer freezes. Gate stays at its last state and no new notes fire |
| T2.2 | Re-patch CLOCK | Playback resumes from where it stopped, not from step 1 |
| T2.3 | Sweep LFO FREQ 0.5 Hz → 8 Hz | Pattern speeds up smoothly; the *rhythm* (which steps play) is unchanged |
| T2.4 | Set CLK DIV = 2 | Steps advance half as often — one step per 2 clock pulses |
| T2.5 | Set CLK DIV = 4, then 16 | Divides correctly; at 16 the pattern is very slow but still advances |
| T2.6 | Return CLK DIV = 1. Set LENGTH = 4 | Only the first 4 steps of the pattern loop |
| T2.7 | Sweep LENGTH 2 → 16 | Loop grows one step at a time; no silence or stall at any setting |
| T2.8 | Patch an LFO/button into RUN and send one trigger | Playback **stops** (RUN is a toggle, not a gate) |
| T2.9 | Send a second trigger | Playback **resumes from the held step**, not from step 1 |
| T2.10 | Hold RUN input high continuously (constant 10 V, no edges) | Nothing toggles — only *rising edges* matter |
| T2.11 | Unpatch RUN while stopped | Module runs again (unpatched = always running). *This used to leave the module stuck stopped; fixed at Task 7 and now covered by the harness* |

**Known design decision:** RUN is deliberately a toggle trigger, not a gate.
If it feels wrong in use, say so — it's a documented deviation from the
original spec, not a bug.

---

## 3. Seed system **[NOW]** — Task 3

| ID | Do | Expect |
|---|---|---|
| T3.1 | GROUP 1, step SUBGROUP 1 → 2 → 3 → 4 | Each click gives a clearly **different** pattern — different notes *and* different rhythm. Not a subtle variation |
| T3.2 | SUBGROUP 1, step GROUP 1 → 2 → 3 → 4 | Same: each is its own pattern |
| T3.3 | Set GROUP 7 / SUBGROUP 19, listen, note it. Move both knobs away, then set 7/19 again | **Identical** pattern returns — same notes, same rhythm, same accents |
| T3.4 | Sweep SUBGROUP fast across its whole range while running | No clicks, stalls or stuck gates; pattern changes cleanly on each value |
| T3.5 | Try ~10 scattered seeds | None are silent; none are a single repeated note; all sound like patterns |

**T3.3 is the determinism guarantee users depend on.** If a seed ever comes
back different, stop and report it — it means a saved patch could change
sound on reload.

---

## 4. Gate output & density **[NOW]** — Task 4

Watch GATE on Scope CH1 and listen.

| ID | Do | Expect |
|---|---|---|
| T4.1 | DENSITY 8, default seed | Gates fire **irregularly** — not every step, not a steady pulse |
| T4.2 | Set DENSITY 1 | Exactly **one** step in the 16 plays. Never silent |
| T4.3 | Set DENSITY 16 | **Every** step plays |
| T4.4 | Step DENSITY 1 → 2 → 3 … → 16, pausing at each | Each click **adds exactly one** new gate and **keeps all the previous ones**. Nothing that was playing drops out |
| T4.5 | Step back down 16 → 1 | Steps drop out one at a time, in reverse of the order they appeared |
| T4.6 | Note the pattern at seed 5/5 density 6. Change seed away and back | Identical gate pattern |
| T4.7 | Halve the LFO clock rate | Gates get **proportionally longer** — the shape of the rhythm stays the same, it just stretches |
| T4.8 | Set CLK DIV = 4 | Gates lengthen to match the slower step rate — they don't stay short and stabby |
| T4.9 | Compare several seeds at DENSITY 8 | Gate *lengths* vary step to step within a pattern (some stabs, some sustains), not all identical |
| T4.10 | Set LENGTH 4, sweep DENSITY | Within a 4-step loop the count is no longer exactly N — **this is expected and documented**; just confirm it still feels monotonic (more knob = more notes) |

---

## 5. Velocity output **[NOW]** — Task 6

Watch VELOCITY on Scope CH2.

| ID | Do | Expect |
|---|---|---|
| T6.1 | Run at DENSITY 8 | Velocity output steps between different levels in the 0–10 V range |
| T6.2 | Watch during a silent step (no gate) | Velocity **holds its last value** — it does not drop to 0 or drift |
| T6.3 | Patch VELOCITY into VCA CV | Notes have audibly different loudness |
| T6.4 | Set DENSITY 1 | Velocity changes only once per loop, at that one note |
| T6.5 | Recall a seed used earlier | Same velocity sequence as before |

**Why it holds:** sample-and-hold is deliberate, so downstream VCAs and
filters don't jump around during silence.

---

## 6. Pitch, scales, root, octave **[NOW]** — Task 5

This is the section to focus on this round. The quantizer math is already
machine-verified over 11.8 million cases; what you're testing is that the
**right knob is wired to the right thing and the result is musical.**

### 6.1 Basic pitch output

| ID | Do | Expect |
|---|---|---|
| T5.1 | Patch CV PITCH → VCO V/OCT, run at DENSITY 8 | A melody plays. Pitch changes from note to note |
| T5.2 | Watch CV PITCH on the Scope | Voltage is a **staircase** — flat during each note, stepping at gate onsets. No glide, no noise (slide is Task 7) |
| T5.3 | Watch through a silent step | Pitch **holds** — it doesn't jump on steps where no gate fires |

### 6.2 The SCALE knob

Twelve positions. **Position 0 is Unquantized** — it replaced the old SCALE
LOCK switch, so this one knob covers raw mode *and* every scale.

| # | Position | # | Position |
|---|----------|---|----------|
| **0** | **Unquantized** | 6 | Phrygian Dominant |
| 1 | Chromatic | 7 | Lydian |
| 2 | Major | 8 | Mixolydian |
| 3 | Natural Minor *(default)* | 9 | Harmonic Minor |
| 4 | Dorian | 10 | Melodic Minor |
| 5 | Phrygian | 11 | Minor Pentatonic |

| ID | Do | Expect |
|---|---|---|
| T5.4 | SCALE = Natural Minor, ROOT = C | Every note is in C minor. Play a C minor chord against it — nothing clashes |
| T5.5 | SCALE = **position 0 (Unquantized)** | Notes go audibly **microtonal** — clearly between the keys, sour on purpose. Not silence, not chaos |
| T5.6 | Sweep position 0 → 1 → 0 repeatedly | Snaps cleanly between detuned and in-tune. Same rhythm either way |
| T5.7 | Step SCALE through all 12 positions | Positions 1–11 are all in tune. Tooltips read exactly as the table above |
| T5.8 | Listen for character as you sweep | Major bright, Minor dark, Phrygian Dominant "Spanish", Chromatic anything-goes. They should be **distinguishable by ear** |
| T5.9 | **Contour check.** Note the melodic *shape* at Natural Minor — where it rises, where it falls. Now switch to Minor Pentatonic, then Major | The **shape survives** — the same steps are still the high points and low points. Only the colour changes. *(This is why the mapping is proportional rather than modulo; if the melody scrambles on a scale change, that's a real bug.)* |
| T5.10 | SCALE = Minor Pentatonic | Fewer distinct pitches, more repeated notes than a 7-note scale. Expected — it only has 5 degrees |
| T5.11 | SCALE = Chromatic | All 12 semitones available; sounds least "composed" of the quantized positions |
| T5.11b | **Judgement call, not pass/fail.** Live with the folded-in knob for a few minutes | Does losing the ability to A/B straight back to your scale bother you in practice? If it does, the switch can come back — say so |

### 6.3 Root

| ID | Do | Expect |
|---|---|---|
| T5.12 | Natural Minor. Step ROOT C → C# → D … → B | Each click transposes the **whole pattern up one semitone**. The melody is otherwise identical |
| T5.13 | ROOT C then ROOT B, checking CV PITCH on the Scope | The B reading is exactly **11/12 V ≈ 0.917 V higher** than the C reading on the same step |
| T5.14 | Change ROOT **while a note is sustaining** | The held note transposes **immediately** — you don't wait for the next gate |
| T5.15 | **Stays quantized.** Step ROOT through all 12 with a scale selected, playing a chord in the new key against it each time | **Every root is still perfectly in tune and in key** — nothing drifts sharp or flat, nothing falls between the keys. Root is a whole-semitone transpose and nothing else |
| T5.16 | Sweep ROOT rapidly while running | No intermediate out-of-tune notes; it jumps key to key |
| T5.17 | SCALE = position 0 (Unquantized), sweep ROOT | Still transposes by exact semitones — the microtonal pattern moves as a block rather than smearing |

### 6.4 Octave range

| ID | Do | Expect |
|---|---|---|
| T5.18 | OCT RANGE = 1 | Pattern is confined to a **single octave** — narrow, almost monotone in span |
| T5.19 | OCT RANGE = 2 | Notes spread over two octaves |
| T5.20 | OCT RANGE = 5 | Wide, leaping pattern across five octaves |
| T5.21 | At OCT RANGE 5, watch the Scope over a full loop | Total pitch span is **under 5 V** above the lowest note (plus the ROOT offset). It must not exceed the knob setting |
| T5.22 | Sweep OCT RANGE 1 → 5 → 1 | Returns to the same narrow pattern. No stuck high notes |
| T5.23 | Change OCT RANGE while a note sustains | Held note jumps immediately, same as ROOT |

### 6.5 Known-good reference data

Set **GROUP 1 · SUBGROUP 1 · SCALE Natural Minor (position 3) · ROOT C ·
OCT RANGE 2 · SLIDE 0**, then use CLK DIV or a slow clock to walk the pattern
one step at a time. Read CV PITCH on the Scope.

> GROUP 1 is in the **BASS** zone, so these numbers include Task 8's style
> shaping — that is why weights 2, 3 and 4 sit on beats and so many notes are
> C or G. Regenerate the table whenever the generator changes.

These are the exact values the code produces for seed index 0. `●` = the step
plays at that DENSITY. Note names assume 0 V = C4, VCO at default tune.

| Step | Weight | On @ D4 | On @ D8 | Gate len | Vel (V) | **Pitch (V)** | Note |
|---|---|---|---|---|---|---|---|
| 1 | 8 | · | ● | 0.34 | 5.70 | +0.583 | G4 |
| 2 | 1 | ● | ● | 0.39 | 9.04 | +1.000 | C5 |
| 3 | 5 | · | ● | 0.13 | 0.19 | +1.000 | C5 |
| 4 | 9 | · | · | 0.27 | 6.95 | +1.000 | C5 |
| 5 | 3 | ● | ● | 0.52 | 2.36 | +0.167 | D4 |
| 6 | 11 | · | · | 0.49 | 5.97 | +1.000 | C5 |
| 7 | 14 | · | · | 0.57 | 3.08 | +1.167 | D5 |
| 8 | 15 | · | · | 0.31 | 5.19 | +1.583 | G5 |
| 9 | 2 | ● | ● | 0.21 | 2.98 | +1.000 | C5 |
| 10 | 16 | · | · | 0.12 | 2.55 | +1.583 | G5 |
| 11 | 7 | · | ● | 0.25 | 5.91 | +1.250 | D#5 |
| 12 | 12 | · | · | 0.22 | 9.17 | +1.000 | C5 |
| 13 | 4 | ● | ● | 0.18 | 6.68 | +1.667 | G#5 |
| 14 | 10 | · | · | 0.11 | 2.56 | +1.417 | F5 |
| 15 | 6 | · | ● | 0.41 | 4.70 | +1.583 | G5 |
| 16 | 13 | · | · | 0.37 | 9.82 | +1.000 | C5 |

Regenerate this table any time the generator changes — the tool prints it as
markdown, ready to paste back in, plus two more seeds:

```bash
c++ -std=c++20 -O1 -o /tmp/nzzl_refgen tests/refgen.cc && /tmp/nzzl_refgen
```

---

## 7. Cross-feature and stress tests **[NOW]**

| ID | Do | Expect |
|---|---|---|
| X.1 | Wiggle every knob at once while running for 30 s | No crash, no stuck gate, no silence that doesn't recover |
| X.2 | Set LENGTH 2, DENSITY 1, CLK DIV 16 | Extreme but functional — something still plays eventually |
| X.3 | Set LENGTH 16, DENSITY 16, CLK DIV 1, LFO at 20 Hz | Fast machine-gun playback, no dropouts, CPU stays sane |
| X.4 | Run two NZZL instances from one clock, both at seed 3/3 | They stay **perfectly in sync and identical** — proves nothing hidden is per-instance random |
| X.5 | Same two instances, different seeds | Independent patterns, still rhythmically locked to the clock |
| X.6 | Save the patch, quit Rack, reopen | Knob positions and pattern restore. Full patch-save tests are in §13 |
| X.7 | Leave running 10 minutes | No drift, no degradation, no memory growth |
| X.8 | Bypass the module (Ctrl/Cmd-E), then un-bypass | Clean recovery |

---

## 8. Slide **[NOW]** — Task 7

Watch CV PITCH on the Scope. Roughly 1 step in 4 is flagged for slide.

| ID | Do | Expect |
|---|---|---|
| T7.1 | SLIDE fully CCW (0) | Pitch changes are **instantaneous** — a clean staircase, no ramps at all |
| T7.2 | SLIDE to about halfway | Some notes now **glide** into pitch; others still jump. Only flagged steps slide |
| T7.3 | SLIDE fully CW | Glides are longer, but every one still **arrives before the next note** — no smearing across steps |
| T7.4 | Watch a glide on the Scope | A straight ramp that settles exactly on the target and stays flat. No overshoot, no wobble at the end |
| T7.5 | Halve the clock rate, SLIDE unchanged | Glides get **proportionally longer** — the feel survives a tempo change |
| T7.6 | Return SLIDE to 0 mid-glide | Later notes jump again immediately |
| T7.7 | Listen with SLIDE up | Should read as portamento/303-style glide, not as being out of tune |

---

## 9. Style zones **[NOW]** — Task 8

The GROUP knob is now three zones. **This is the section where your ear is the
only instrument that counts** — the harness proved the statistics, not the
music.

| GROUP | Zone | Display | Character |
|---|---|---|---|
| 1–10 | Bassline | `BASS` | Notes on the beat, root/fifth heavy, short punchy gates |
| 11–22 | Random | `RAND` | The unshaped generator — anything goes |
| 23–32 | Arpeggio | `ARP` | Stepwise runs of four, even mid-length gates |

| ID | Do | Expect |
|---|---|---|
| T8.1 | GROUP 3, DENSITY 4 | Notes land **on the beat**. Should feel like a bassline, not a scatter |
| T8.2 | GROUP 3, sweep DENSITY 1→8 | The first notes to appear are the on-beat ones; off-beats fill in after |
| T8.3 | GROUP 16 (RAND), DENSITY 4 | Noticeably more scattered than GROUP 3 |
| T8.4 | GROUP 28 (ARP) | Runs of notes that **walk up or down** rather than leaping around |
| T8.5 | Step GROUP 3 → 16 → 28 at fixed SUBGROUP | The three zones are **distinguishable by ear** without looking at the knob |
| T8.6 | Compare gate character across zones | BASS shortest and punchiest, RAND most varied, ARP most even |
| T8.7 | Cross a zone boundary: GROUP 10 → 11, then 22 → 23 | Character changes at the boundary. The display's zone line changes to match |
| T8.8 | **Judgement call.** Do the zones earn their place? | Are 10/12/10 the right sizes? Is BASS bassy enough? Is ARP arpy enough? Opinions wanted |

---

## 10. Reseed **[NOW]** — Task 9

Patch a manual trigger (or an LFO) into RESEED.

| ID | Do | Expect |
|---|---|---|
| T9.1 | Send one trigger | The pattern **changes** to a new one |
| T9.2 | Watch the GROUP and SUBGROUP knobs as it fires | They **jump to match** the new seed. The display's `G## S##` line updates too |
| T9.3 | Note the knob values, change them away, then dial them back by hand | The reseeded pattern **returns exactly**. This is the whole point — a seed you can hear but not dial in would be a dead end |
| T9.4 | Hold the RESEED input high continuously | Only **one** reseed happens — it is edge-triggered, not level-triggered |
| T9.5 | Unpatch RESEED | Nothing ever reseeds on its own |
| T9.6 | Fire RESEED 20 times | Different seeds, spread across the range — not the same few |
| T9.7 | Reseed, then save and reload the patch | The reseeded pattern comes back (it lives on the knobs) |

---

## 11. CV inputs **[NOW]** — Task 10

**Every CV input is an offset on its knob, not an absolute value.** The knob
sets the base; the CV moves you from there.

| ID | Do | Expect |
|---|---|---|
| T10.1 | Nothing patched into any CV input | Knobs behave exactly as in earlier sections. Unpatched is always a no-op |
| T10.2 | Slow LFO (±5 V) → **cvROOT** | The key transposes up and down. **Every note is still perfectly in tune** at every point of the sweep — never a smear or a bent note |
| T10.3 | Turn the ROOT knob while cvROOT is patched | Knob and CV **add**. The knob still does something |
| T10.4 | Constant 1 V into cvROOT (V/oct) | Transposes exactly **one octave** — i.e. back to the same root |
| T10.5 | Slow LFO → **cvSCALE** | Steps through scale positions. Clean jumps between scales, no glitching, no silence |
| T10.6 | Large negative CV into cvSCALE | Reaches position 0 and **clamps** there — it stops at unquantized, never wraps around to Minor Pentatonic |
| T10.7 | Slow LFO → **cvSEED** | Sweeps through patterns continuously. At the ends it **wraps** rather than sticking |
| T10.8 | Watch the knobs with cvSEED patched | GROUP/SUBGROUP stay where you left them — CV SEED does not move them, it offsets from them |
| T10.9 | LFO → **cvSLIDE** | Glide amount modulates; at 0 V it matches the knob alone |
| T10.10 | Unplug each CV cable in turn while running | Each unplug returns cleanly to the knob value. No stuck offsets |

---

## 12. Display **[NOW in Rack, PENDING on hardware]** — Task 11

| ID | Do | Expect |
|---|---|---|
| T11.1 | Read the three lines at GROUP 1 / SUBGROUP 1 | `G01 S01`, `BASS`, `C MINOR` |
| T11.2 | Turn GROUP and SUBGROUP | The `G## S##` line tracks both, always two digits |
| T11.3 | Cross zone boundaries (GROUP 10→11, 22→23) | The zone line changes `BASS` → `RAND` → `ARP` |
| T11.4 | Turn ROOT and SCALE | The third line tracks both, e.g. `G# DORIAN` |
| T11.5 | SCALE to position 0 | The line reads `<root> RAW` — it does not pretend to be a scale |
| T11.6 | Patch a CV into cvROOT or cvSCALE and sweep | The display follows the **effective** value, CV included, not just the knob |
| T11.7 | Look at the module in the browser (not placed in a rack) | Renders sensibly with no module attached — no crash, no blank |

---

## 13. Patch save **[NOW]** — Task 12

| ID | Do | Expect |
|---|---|---|
| T12.1 | Set a distinctive seed and knobs, save, quit Rack, reopen | Everything returns: knobs, seed, pattern |
| T12.2 | Reseed to a new pattern, then save and reload | The reseeded pattern returns — it lives on the knobs |
| T12.3 | Patch RUN, stop playback, save and reload | It comes back **stopped**. Run state is saved |
| T12.4 | Reload and start the clock | Playback starts from a predictable place. Step position is deliberately not saved |
| T12.5 | Load a patch saved before this build, if you have one | Knobs load without error. Note anything that sounds different and report it |

---

## 14. MetaModule hardware **[PENDING hardware]**

```bash
git push                                    # CI cross-compiles for ARM
gh run download --repo X13F-Technologies/nzzl-metamodule --dir ~/Downloads/nzzl-build
```

Copy `NZZL.mmplugin` to the MetaModule's SD card, then:

| ID | Do | Expect |
|---|---|---|
| H.1 | Boot with the plugin installed | NZZL appears in the module list; no boot failure |
| H.2 | Load NZZL into a patch | Loads without error or hang |
| H.3 | Repeat §2–§13 on hardware | Same behaviour as in VCV Rack |
| H.4 | **Same seed on hardware and in Rack, same knobs** | **Byte-identical pattern** — same notes, same rhythm. This is the whole point of the deterministic design |
| H.5 | Check CPU load on the MetaModule | Headroom left for other modules |
| H.6 | Run 30 minutes | No crash, no audio glitching, no thermal issue |
| H.7 | Save and reload a hardware patch | Seed and knobs restore |
| H.8 | **Read the display on the physical screen** | Three lines render, update without flicker, and are legible. **Untested — the text content is verified, the rendering is not** |

---

## 15. Sign-off sheet

Copy this block, fill it in, and paste it back when reporting results.

```
NZZL system test — date: ____________  build/commit: ____________

Part 1  Smoke            S.1–S.5        [ ] pass  [ ] fail: ______________
Part 2  Clock/Run/Len    T2.1–T2.11     [ ] pass  [ ] fail: ______________
Part 3  Seed system      T3.1–T3.5      [ ] pass  [ ] fail: ______________
Part 4  Gate/Density     T4.1–T4.10     [ ] pass  [ ] fail: ______________
Part 5  Velocity         T6.1–T6.5      [ ] pass  [ ] fail: ______________
Part 6  Pitch/Scales     T5.1–T5.27     [ ] pass  [ ] fail: ______________
Part 7  Cross/Stress     X.1–X.8        [ ] pass  [ ] fail: ______________
Part 8  Slide            T7.1–T7.7      [ ] pass  [ ] fail: ______________
Part 9  Style zones      T8.1–T8.8      [ ] pass  [ ] fail: ______________
Part 10 Reseed           T9.1–T9.7      [ ] pass  [ ] fail: ______________
Part 11 CV inputs        T10.1–T10.10   [ ] pass  [ ] fail: ______________
Part 12 Display          T11.1–T11.7    [ ] pass  [ ] fail: ______________
Part 13 Patch save       T12.1–T12.5    [ ] pass  [ ] fail: ______________
Part 14 Hardware         H.1–H.8        [ ] n/a   [ ] pass  [ ] fail: _____

Musical judgement (not pass/fail — opinions wanted):
  Do the seed zones sound useful? ________________________________
  Is DENSITY's response musical?  ________________________________
  Is the scale list the right 12? ________________________________
  Does raw-as-position-0 beat a separate switch? _________________
  Do the three style zones earn their place? _____________________
  Is the slide range useful end to end? __________________________
  Is the panel layout workable to play? __________________________
  Anything that feels wrong to play? _____________________________
```

**Reporting a failure:** give the test ID, the knob settings, the seed
(GROUP/SUBGROUP), and what you heard or measured instead. Seed + knobs is
enough to reproduce anything this module does.

---

## 16. Maintaining this guide

- A new task ships → give it a numbered **[NOW]** part with real test IDs.
- The automated harness grows → move the covered rows *out* of here. Anything
  a test in `tests/` proves shouldn't burn human attention.
- The generator changes → regenerate §6.5's reference table; stale numbers are
  worse than none.
- Per-task Claude/user split lives in [DESIGN.md](DESIGN.md) § Per-task test
  plan; task status lives in § Implementation status.
