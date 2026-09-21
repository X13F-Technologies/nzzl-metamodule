# NZZL MetaModule Plugin — Claude Code Instructions

Deterministic generative bassline/sequencer module for 4ms MetaModule hardware,
developed and tested locally as a VCV Rack 2 plugin.

**Project backlog:** NZZL is item 1 in the X13F software backlog —
`~/Documents/x13f-backlog/X13F_Backlog.md`
(https://github.com/X13F-Technologies/x13f-backlog). Update that item's status
there when this project's status changes.

**What the module currently is:** [docs/SPEC_AS_BUILT.md](docs/SPEC_AS_BUILT.md)
— the specification as built, in the same order as the original requirements,
with a full table of where and why the two differ. Update it when behaviour
changes. [docs/HANDOFF_SPEC.md](docs/HANDOFF_SPEC.md) is the original
requirements and is **frozen** — never edit it to match the build.

**Full manual acceptance procedure:** [docs/SYSTEM_TEST_GUIDE.md](docs/SYSTEM_TEST_GUIDE.md)
— the numbered test IDs (T5.9, T4.4, …) to cite when asking the user to test
something, and the acceptance criteria for tasks not yet built.

**Before making any changes, read [docs/DESIGN.md](docs/DESIGN.md)** — it records
the architecture rules, determinism invariants, and design decisions already made.
Violating them (especially the RNG stream isolation) silently breaks users' saved
patches.

## Build & test loop

```bash
# 1. Native tests — run after ANY change to src/*.hh (pure logic).
# Six suites; CI runs them too, so a red harness never reaches main.
./tests/run_tests.sh

# 2. VCV Rack build + install (user tests by ear/scope in Rack)
cmake --build build-vcv && cmake --install build-vcv
# then the user restarts VCV Rack 2

# 3. MetaModule hardware build — CI builds NZZL.mmplugin on EVERY branch,
# so push and check it before merging, not after.
git push   # artifact downloadable via: gh run download
```

## Hard rules

- **All playback logic must be deterministic from the seed.** No `rand()`,
  no `random::` calls in `process()`. The only non-deterministic moment is
  the RESEED trigger.
- **Pure logic goes in headers under `src/` with no `rack.hpp` include**
  (`rng.hh`, `pattern.hh`, `scales.hh`, `engine.hh`, `cv.hh`, `display.hh`)
  so the native test harness can compile it. `nzzl.cc` is a thin Rack adapter
  only — if you are about to put a decision in it, put it in a header instead
  and write the test.
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
  it and passed in this session. Realtime logic now lives in `engine.hh` and
  IS harness-covered; what genuinely remains manual is whether jacks and
  knobs are wired to the right engine fields, anything visual, how it
  sounds, and all hardware behaviour.
- If nothing needs manual testing, say so explicitly rather than omitting
  the section.
- The per-task Claude/user test split is pre-planned in
  [docs/DESIGN.md](docs/DESIGN.md) § "Per-task test plan" — follow it.
- Write manual steps as references to test IDs in
  [docs/SYSTEM_TEST_GUIDE.md](docs/SYSTEM_TEST_GUIDE.md) where they already
  exist, and add new ones to that guide rather than inventing ad-hoc steps.
- After changing that guide, run `python3 tools/build_test_bench.py` to
  rebuild `docs/test-bench.html` from it, then republish the artifact at
  https://claude.ai/artifact/LLLB9bmgk1dhRhW4MCKJoP so the click-through
  version does not drift from the guide.
- Do not mark a task ✅ in the DESIGN.md status table until the user
  confirms the manual portion.

## Project status

Track progress against the implementation order in
[docs/DESIGN.md](docs/DESIGN.md) § "Implementation status". Update that
section when a task is completed and user-confirmed.
