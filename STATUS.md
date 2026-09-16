# Landor — Project Status

> **Purpose of this file.** A working cache of project state so a new session can orient
> here instead of re-exploring the repository from scratch. Read §0, then jump straight to
> the section your task needs. Full exploration becomes optional, not mandatory.
>
> **Status = operational state. Design = [`dev-docs/DESIGN_STATE.md`](dev-docs/DESIGN_STATE.md).**
> This file never overrides a design document. Where they disagree, DESIGN_STATE.md wins
> and this file is the buggy one — report it, don't follow it.
>
> ```
> last verified   : git HEAD 32a3692 "Drafting the diffrent components, now working on the maps side." (+ dirty tree)
> verified on     : 2026-09-16
> verified by     : every command in §5 was actually run, output quoted
> trust horizon   : if `git log -1 --oneline` no longer shows 32a3692, treat §5–§6 as STALE
>                   and re-run §5 (~1 min) before trusting numbers
> ```

---

## 0. Orientation in 60 seconds

```bash
cd /mnt/AI/AiProgs/experiments/Landor
git log -1 --oneline && git status --short        # compare against the stamp above
grep -rn '^namespace' src/*/*.hpp                 # who has migrated to landor:: yet
cmake -S . -B /tmp/lb -DCMAKE_BUILD_TYPE=Debug >/dev/null && \
  cmake --build /tmp/lb -j8 2>&1 | grep -c 'error:'   # build health number
g++ -std=c++20 -Isrc tests/test_coord.cpp -o /tmp/tc && /tmp/tc   # tested code still green?
```

If those four outputs match §5, everything below is current and you need nothing else.

**Ignore these directories, always** — they are not source:
`.kilo/worktrees/` (full duplicate copies of the repo), `transient/` (Qt Creator build output),
`.qtcreator/`, `build/`. A naive `grep -r` or `find` will double-count everything in the repo.

---

## 1. What this is

A top-down ASCII exploration game in **C++20, standard library only — no third-party deps**,
engine split so it can run down to a Pi Pico. Find shards, get a key, unlock the gate, reach
the beacon. `docs/GAMEPLAY.md` is the player-facing description; `dev-docs/DESIGN_STATE.md`
is the authoritative technical decision record (numbered `## 1.`–`## 13.`, decisions as `D-n`,
open questions as `Q-n`).

Reading order for design context: `DESIGN_STATE.md` (authoritative, owns §1 "Targets") →
`IMPLEMENTATION.md` → `coding_rules.md` + `coding_style.md`. `README.md` is the overview but
has known drift, see §8.

---

## 2. Layout and per-file state

```
main.cpp                     stub: boots managed heap, prints capacity. BUILDS & RUNS.
CMakeLists.txt               single static lib `landor` + exe; sources listed EXPLICITLY
                             (no globbing — new files must be added by hand). No tests here.
src/storage/types.hpp        landor::storage :: SourceId, Offset, Size, Error, Result, STATUS_OK
src/world/                   ← all the current activity
  coord.hpp        13KB   namespace Geo         COMPLETE, tested
  area.hpp         8.3KB  namespace Geo         COMPLETE, tested
  layer.hpp        1.4KB  landor::geo           Layer concept + LayerId/layer_count/any_layer
  tile.hpp         3.6KB  landor::geo           TileView (read) + TileEdit (write); CellWord
  patch.hpp        4.6KB  landor::geo           Patch + LayerBinding  (just reworked, §4)
  placement.hpp    5.1KB  landor::geo           Placement = PatchId + position + transform ref
  patchset.hpp     4.3KB  landor::geo           compose patches/placements into one Area
  map.hpp          11KB   landor::geo           the hub: resolve/place/at; LAYER_BITS table
  orientation.hpp  64B    (global)              STUB — real Rotation/Reflection live in .cpp
  place.hpp        52B    (global)              STUB — real Place body lives in place.cpp
  chunk.hpp        68B    namespace Geo         STUB
  cache.hpp        68B    namespace Geo         STUB
  istorage.hpp     346B   namespace Geo         STUB
  *.cpp                    mostly empty scaffolds (`Patch::Patch() {}`) that cannot compile
src/renderers/utf-8/         empty directory, nothing in it yet
include/managed_heap/        FROZEN single-header object store (SPEC.md, USER_GUIDE.md, examples)
tests/                       test_coord.cpp, test_area.cpp — hand-rolled mains, no framework
tools/checks/                check-build|tests|warnings|sanitizers|static|tidy-changed.sh
dev-docs/                    DESIGN_STATE.md · IMPLEMENTATION.md · coding_rules.md · coding_style.md
docs/GAMEPLAY.md             player guide
```

---

## 3. The shape of the design (enough to write code)

- **Cells** are `CellWord = std::uint16_t`, packed `[type_id:8 | height:3 | overlay:3 | stage:2]`;
  bit-fields forbidden, shifts only. In-memory layout == on-disk ABI. Map width fixed at 256 so a row is
  exactly one 512-byte sector → `offset = y << 9`, no division. No compression (write amplification).
- **Layer-oriented.** `Map::at(pos)` *synthesizes* a Tile per query; there is no authoritative Tile array.
  A Patch lacking a layer ≠ that property cannot exist there — `Map` may fill it from fallback,
  procedural generation or live state. Keep absence distinct from "impossible".
- **Authored vs procedural.** Files describe reusable immutable `Patch`es; `Placement`s are live occurrences
  (position + reflection→rotation); `PatchSet` composes them; `Place` is *narrative* identity over PlacementIds.
- **One mutation front door**, plan-then-apply, allocate before patching. Determinism covers container
  iteration order; xorshift RNG only in logic phases.

Don't re-derive the rest here — grep DESIGN_STATE.md for the `D-n` you need.

---

## 4. Current work in progress

**Header-first interface drafting for the map/world layer.** The headers are design contracts written
to be read and argued over; they declare without defining and are *not expected to compile yet*.
Treat a header line that fails to parse as intent, not as a bug report.

Dirty tree at last verification: `CMakeLists.txt`, `src/world/{map,patch,patchset}.hpp` modified;
`src/world/tile.{hpp,cpp}`, `src/storage/types.hpp` newly added.

Decided most recently (2026-09-16, during the `patch.hpp` cleanup):

- **Storage vocabulary lives outside `geo`.** `src/storage/types.hpp` owns `SourceId/Offset/Size/Error/Result`;
  `geo` must not redefine identity types. Consequence: `LayerBinding{ LayerId layer; storage::SourceId source; }`
  — LayerId belongs to geography, SourceId belongs to storage.
- **No invented invalid ids.** The `no_layer_source` sentinel is gone. Lookup returns
  `const LayerBinding*` (`nullptr` = absent) via `Patch::binding(layer)` / `Patch::binding<LayerT>()`;
  `provides()` is just `binding(...) != nullptr`. Call pattern:
  `if (const auto* b = patch.binding<Terrain>()) storage.read(b->source, ...);`
  Same reasoning should apply to any future lookup — prefer "may be absent" over reserved values.

---

## 5. Build / test reality (measured, not assumed)

| check | command | result |
|---|---|---|
| configure | `cmake -S . -B /tmp/lb -DCMAKE_BUILD_TYPE=Debug` | OK |
| full build | `cmake --build /tmp/lb -j8` | ❌ **33 `error:` lines** — see §6 |
| tests, standalone | `g++ -std=c++20 -Isrc tests/test_coord.cpp -o /tmp/tc && /tmp/tc` | ✅ `All 121 tests passed.` |
| | `g++ -std=c++20 -Isrc tests/test_area.cpp -o /tmp/ta && /tmp/ta` | ✅ `All tests passed.` |
| CTest | `ctest --test-dir transient/build` | ❌ **not wired**: no `enable_testing()`, no `add_test()`, no test target → `check-tests.sh` exits 2 |
| `tools/checks/check-build.sh` | run it | ❌ exit 2 (same 33 errors) |
| `tools/checks/check-static.sh` | run it | ❌ exit 2 — `clang-tidy` **is** installed, but the script configures+builds first and dies on the build |

Tooling note: the check scripts default `BUILD_DIR` to `transient/pipeline3/builds/{default,static,sanitize}`,
which is *not* the Qt Creator tree at `transient/build/Desktop_Qt_6_11_2_Debug`. Override with
`BUILD_DIR=... tools/checks/check-build.sh` if you want them to share one tree.
| main | `g++ -std=c++20 -Iinclude main.cpp && ./a.out` | ✅ prints `Memory system initialized, usable 65536 bytes.` |

**Because the library target does not build, `main.cpp`'s compile status is only provable standalone.**
The fastest honest health signal for `coord`/`area` work is the two `g++` one-liners above, not cmake.

Build dir used by Qt Creator is `transient/build/Desktop_Qt_6_11_2_Debug` (Qt 6.11.2 kit; clangd from it).

---

## 6. Known breakage ledger (the whole 33 errors reduce to 4 causes)

1. **Half-finished namespace migration `Geo` → `landor::geo`.** `coord.hpp`/`area.hpp` still declare in
   `namespace Geo`, while `patch/map/placement/…hpp` are in `landor::geo` and reference unqualified
   `Coord32`, `Area`, `area_type`. → *fix:* add a using-bridge or finish migrating `coord.hpp`/`area.hpp`.
   Largest cluster (map.hpp, patch.hpp, placement.hpp).
2. **`Rotation`/`Reflection` are defined in `src/world/orientation.cpp`, not in the header**
   (`orientation.hpp` declares an empty `class Orientation`). Any header using them fails. → *fix:* move to
   `orientation.hpp`; also remove the stray `#pragma once` from `.cpp` files.
3. **`Place`'s entire class body sits in `place.cpp`** with a 52-byte `place.hpp`. Same fix.
4. **Empty `.cpp` scaffolds define things that no longer exist as non-templates** — `area.cpp`, `chunk.cpp`,
   `map.cpp`, `patch.cpp`, `patchset.cpp`, `placement.cpp` all begin `X::X() {}` against now-template types.
   → *fix:* delete the scaffold definition or make it an explicit instantiation.

Related, cosmetic-but-real: `patch.hpp` declares `template<typename CoordT = Coord32>` yet its constructor
parameter is hard-coded `Coord32 natural_position` rather than `coord_type`; `map.hpp`'s `TileView` is
documented with `At/Lookup/Length` but used as `tiles[...]`; stray `#pragma once` in `orientation.cpp`.

---

## 7. Conventions card (details in dev-docs/coding_rules.md + coding_style.md)

- Namespaces: `landor`, `landor::geo`, `landor::storage` (+ legacy `Geo`, being removed). `landor::*` only.
- Headers carry the design reasoning as comments — write the *why*, keep it, don't strip comments to "clean up".
- `[[nodiscard]]` on lookup/status functions; declaration order is meaningful documentation.
- Members `m_`-prefixed; aligned declarations in blocks (`LayerId           layer;`).
- No exceptions on hot paths — `storage::Result` carries `Error` + `bytes_transferred`.
- CMake lists sources explicitly: **new file ⇒ edit CMakeLists.txt**, or it silently isn't built.
- Standard library only. Nothing new gets vendored without a decision record entry.

---

## 8. Documentation drift (already discovered — don't re-discover)

`README.md` says / implies, reality differs:

| README claim | reality |
|---|---|
| `managed-heap/` at repo root | `include/managed_heap/` |
| refers to `../AGENTS.md` | does not exist (no AGENTS.md/CLAUDE.md anywhere) |
| tests use GoogleTest | no framework; hand-rolled `if (...) return N;` mains |
| `src/game/`, `src/render/`, `src/actor/`, `src/npc/`, `src/host/` | absent; only `src/world`, `src/storage`, `src/renderers/utf-8` (empty) |
| `assets/`, `tools/gen_starter_map.py` | absent |

Design docs also lag code in places (e.g. wording still describing `Patch::source()` lookups after §4).
When code and doc disagree, note it rather than silently reconciling.

---

## 9. Open threads / plausible next tasks

- Get to a green build: causes #1–#4 in §6, in that order of payoff.
- Decide `Geo` vs `landor::geo` for `coord.hpp`/`area.hpp` (the migration target must be one spelling).
- Where do coordinate z-components live? `Geo::Coord<T>` is **2D (x, y)** but `tile.hpp`/`map.hpp` commentary
  relies on `z == -1` meaning the ground/base plane. Unresolved tension, affects `TileView::base()`.
- Wire tests into CMake (`enable_testing()` + a target per test) so `check-tests.sh` stops exiting 2.
- Same "storage owns the identity, geo owns the relationship" cleanup may apply to other `geo` types
  (`Placement`, `PatchSet`) — check for locally redefined ids.
- `chunk`/`cache`/`istorage` are stubs waiting on their interface drafts.

---

## 10. How to keep this file honest

- Update the §5 numbers and the stamp line at the top whenever you touch build wiring or finish a header.
- A change this size is worth a commit; if you don't commit, at least bump `verified on`.
- Keep it under ~200 lines. It earns its place by being *skimmed*; long-form reasoning belongs in dev-docs.
- Never paste DESIGN_STATE.md content in here — link the `D-n`/`Q-n` instead.
- Note for future sessions: the owner often works **concurrently** in Qt Creator / another agent session.
  Re-check `git status` and file mtimes before editing, and diff against this file's stamp first.
