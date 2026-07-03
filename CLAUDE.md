# NZZL MetaModule Plugin — Claude Code Instructions

Deterministic generative bassline/sequencer module for 4ms MetaModule hardware,
developed and tested locally as a VCV Rack 2 plugin.

**Before making any changes, read [docs/DESIGN.md](docs/DESIGN.md)** — it records
the architecture rules, determinism invariants, and design decisions already made.
Violating them (especially the RNG stream isolation) silently breaks users' saved
patches.

## Build & test loop

```bash
# 1. Native tests — run after ANY change to src/*.hh (pure logic)
./tests/run_tests.sh

# 2. VCV Rack build + install (user tests by ear/scope in Rack)
cmake --build build-vcv && cmake --install build-vcv
# then the user restarts VCV Rack 2

# 3. MetaModule hardware build — push to main, CI builds NZZL.mmplugin
git push   # artifact downloadable via: gh run download
```

## Hard rules

- **All playback logic must be deterministic from the seed.** No `rand()`,
  no `random::` calls in `process()`. The only non-deterministic moment is
  the RESEED trigger.
- **Pure logic goes in headers under `src/` with no `rack.hpp` include**
  (`rng.hh`, `pattern.hh`, future `scales.hh`) so the native test harness
  can compile it. `nzzl.cc` is a thin Rack adapter only.
- **Never change how an existing attribute consumes its RNG stream** — each
  attribute has its own salted stream precisely so patches survive upgrades.
  New attributes get new salts.
- MetaModule SDK limits: no iostream/fstream/stringstream, no exceptions,
  C++20, ARM GCC 12.2/12.3 exactly (CI handles this).
- Commit and push at each working checkpoint. If push returns 403 for
  MiGenteClothing, run `gh auth switch -u X13F-Technologies`.

## Reporting testing responsibility (required every task)

When you finish implementing any task or change, your summary to the user
MUST end with two clearly labeled sections:

```
✅ AUTOMATED (already verified — no action needed)
- <what the native harness verified, with test names>

🖐 MANUAL TEST NEEDED (user, in VCV Rack / on hardware)
- <numbered, concrete steps: what to patch, what to turn, what to expect>
```

Rules:
- Never mark something automated unless a test in `tests/` actually covers
  it and passed in this session. Realtime logic in `nzzl.cc` (triggers,
  timers, voltages) is manual unless it has been extracted into a pure
  header with tests.
- If nothing needs manual testing, say so explicitly rather than omitting
  the section.
- The per-task Claude/user test split is pre-planned in
  [docs/DESIGN.md](docs/DESIGN.md) § "Per-task test plan" — follow it.
- Do not mark a task ✅ in the DESIGN.md status table until the user
  confirms the manual portion.

## Project status

Track progress against the implementation order in
[docs/DESIGN.md](docs/DESIGN.md) § "Implementation status". Update that
section when a task is completed and user-confirmed.
