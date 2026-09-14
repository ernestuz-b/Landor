# Landor

> A tiny game for big ideas.

Landor is a top-down exploration game written in **C++20 with no dependencies**.
You live in a little valley, wander anywhere you like, hunt for hidden shards,
open what is locked, and reach the beacon. Complete the missions and grow,
It starts as an ASCII game and later grows tiled graphics, all while running on 
hardware as small as a Raspberry Pi Pico.

| | |
| --- | --- |
| **Language** | C++20, standard library only — no third-party dependencies |
| **View** | UTF-8 text tiles now (ASCII fallback), tiled graphics later (same grid) |
| **Targets** | Linux PC, Raspberry Pi Zero, Raspberry Pi Pico, STM32F429 Discovery |
| **Current version** | 0.1 (starter valley; not all game dynamics yet) |
| **Status** | Design phase — interface and contracts first, then implementation |

## Documentation

| Document | Audience | What is in it |
| --- | --- | --- |
| [`GAMEPLAY.md`](GAMEPLAY.md) | players, including children | how to play, controls, pictures, tips — no technicalities |
| [`IMPLEMENTATION.md`](IMPLEMENTATION.md) | developers | design decisions, data model, terrain editing, rendering, configuration |
| this file | everyone | what the project is, how to get and run it, where things live |

## Features (0.1 and direction)

* Free exploration of a single hand-authored valley with fog of war and remembered
  terrain; `Explored N%` progress in the HUD.
* Day/night cycle that shrinks vision, driven by a deterministic step counter.
* Objectives: collect shards, pry a chest open for a key, unlock the gate, reach the
  beacon to win.
* Editable terrain — dig, breach, pave and remove roads — as journaled transactions.
* Engine-style object model: Actor → Pawn → Character, NPCs whose choices follow D&D
  alignment through behaviour trees, animals as actors, crops as tile state.
* Deterministic rules: no RNG, no wall-clock, scriptable headless play, replay-based
  saves.
* Bounded memory: every dynamic object lives in one fixed-size managed-heap region —
  no system allocator, no unbounded growth
  ([`IMPLEMENTATION.md`](IMPLEMENTATION.md) §2.4).
* Graphics-ready from the start: every tile carries a stable id next to its glyph, so
  a sprite renderer can index an atlas without touching a single rule.

## Getting the code

```bash
git clone <repository-url> landor
cd landor
```

## Building and running

All check scripts run from the project root:

```bash
tools/checks/check-build.sh              # configure + build (Debug)
tools/checks/check-tests.sh              # run the test suite
tools/checks/check-warnings.sh           # build with warnings treated as errors
tools/checks/check-sanitizers.sh         # ASan + UBSan build and tests
tools/checks/check-static.sh             # full clang-tidy + cppcheck analysis
tools/checks/check-static.sh --file F    # analyse a single file
tools/checks/check-tidy-changed.sh       # clang-tidy for changed files
```

Build and test manually:

```bash
cmake -S . -B transient/pipeline3/builds/debug \
      -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build transient/pipeline3/builds/debug -j
cd transient/pipeline3/builds/debug && ctest --output-on-failure
```

Run the game from the project root (world files load by relative path), or pass
`--world /abs/path.txt`:

```bash
transient/pipeline3/builds/debug/landor                      # play
transient/pipeline3/builds/debug/landor --demo               # watch a winning route
transient/pipeline3/builds/debug/landor --headless --script route.txt
transient/pipeline3/builds/debug/landor --save save.txt      # autosaves on quit
transient/pipeline3/builds/debug/landor --load save.txt
transient/pipeline3/builds/debug/landor --view 60x30
```

Keys: `WASD` or arrow keys to move, `E` to interact, `Q`/`Esc` to quit. Full player
guide: [`GAMEPLAY.md`](GAMEPLAY.md).

> **Note** — the repository is currently at the design stage: only the documents and
> the [`managed-heap`](managed-heap/) component exist so far. The commands above
> describe the intended interface; they become valid as the corresponding
> milestones land. See [Status](#status-and-roadmap).

## Repository layout

```
README.md            this file
GAMEPLAY.md          player guide (non-technical)
IMPLEMENTATION.md    technical design and decisions
dev-docs/            phases, binding, rules of the repo workflow
src/world            cells, chunks, tile descriptors, terrain operations
src/game             rules: step(), inventory, vision, day/night, win state
src/render           frame builder, auto-tiling/influence, terminal & headless backends
src/actor            Actor / Pawn / Character, controllers
src/npc              alignment, behaviour trees, dialog
src/host             argv, interactive terminal, script replay, save/load
managed-heap/        single-header managed object store, frozen component
                     (SPEC.md, USER_GUIDE.md)
assets/worlds/       plain-text maps, one glyph per tile
assets/tilesets/     graphics atlases (later versions)
tools/               map generator, check scripts
transient/           builds and pipeline artefacts (never at the project root)
```

## Development

Build and analysis artifacts stay under `transient/pipeline3/builds/<variant>`, and
`cmake --build` / `ctest` run *from* the build directory. Preferred entry points are
the check scripts in `tools/checks/` (`check-build.sh`, `check-tests.sh`,
`check-warnings.sh`, `check-sanitizers.sh`, `check-static.sh`, `check-tidy-changed.sh`).
Repository-wide agent and workflow instructions live in the parent
[`../AGENTS.md`](../AGENTS.md).

Ground rules for contributors:

* Rules live in `world`/`game`; rendering only reads state through `Frame`.
* Terrain changes only through the single terrain-operation API.
* No magic numbers outside the generated config header and rule tables.
* Determinism includes container iteration order — see
  [`IMPLEMENTATION.md`](IMPLEMENTATION.md) §7.
* Plain standard CMake only: any IDE, Qt Creator included, must be able to open the
  project as-is. No IDE-owned files in the repo, no generator-specific logic.

## Status and roadmap

| Version | Content |
| --- | --- |
| **0.1** *(current)* | Starter valley: shards, chest and key, gated beacon, day/night, fog of war, HUD, ASCII rendering, headless demo/script modes, save/load |
| 0.x | Terrain editing in play, first NPC with alignment-driven behaviour tree, elevation rules |
| 1.0 | ASCII-only release: full valley objectives, NPC population, behaviour trees, dialog |
| > 1.0 | Tiled graphics renderer, ecology (crops and animals), further targets |

Known open design questions are listed at the end of
[`IMPLEMENTATION.md`](IMPLEMENTATION.md); answering them is part of the current work.

## License

Not yet declared.
