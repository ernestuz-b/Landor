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
> last verified   : git HEAD 00cbb8b "Merge commit '285f228d...'"  (+ uncommitted hygiene pass, see §4)
> verified on     : 2026-09-16
> verified by     : every command in §5 was actually run, output quoted
> trust horizon   : if `git log -1 --oneline` no longer shows 00cbb8b, treat §5–§6 as STALE
>                   and re-run §5 (~1 min) before trusting numbers
> headline        : THE CMAKE BUILD IS GREEN (main.cpp only). Do not read that as "the design
>                   headers parse" — nothing compiles them, see §6.
> ```

---

## 0. Orientation in 60 seconds

```bash
cd /mnt/AI/AiProgs/experiments/Landor
git log -1 --oneline && git status --short        # compare against the stamp above

# (a) build health — currently trivially green, only main.cpp is compiled
cmake -S . -B /tmp/lb -DCMAKE_BUILD_TYPE=Debug >/dev/null && cmake --build /tmp/lb -j8 2>&1 | tail -1

# (b) REAL header health — this is the meaningful number now
for h in src/storage/*.hpp src/world/*.hpp; do \
  n=$(g++ -fsyntax-only -std=c++20 -Isrc -Iinclude -x c++ "$h" 2>&1 | grep -c 'error:'); \
  printf '%-28s %s\n' "$h" "$([ "$n" = 0 ] && echo clean || echo "$n errors")"; done

g++ -std=c++20 -Isrc tests/test_coord.cpp -o /tmp/tc && /tmp/tc   # tested code still green?
```

If those outputs match §5, everything below is current and you need nothing else.

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
CMakeLists.txt               ONE target: exe `Landor` = main.cpp + listed headers. Sources are
                             listed EXPLICITLY (no globbing — a new file must be added by hand).
                             No library target, no enable_testing(), no test targets.
src/storage/types.hpp        landor::storage :: SourceId, Offset, Size, Error, Result, STATUS_OK
src/world/                   ← header-only design contracts, NO .cpp files any more (§4)
  coord.hpp        ✅ clean      namespace Geo         COMPLETE, tested
  area.hpp         ✅ clean      namespace Geo         COMPLETE, tested
  layer.hpp        ✅ clean      landor::geo           Layer concept + LayerId/layer_count/any_layer
  orientation.hpp  ✅ clean      landor::geo           Rotation, Reflection, Orientation (2 bytes)
  tile.hpp         ✅ clean      landor::geo           TileView (read) + TileEdit (write); CellWord
  patch.hpp        ❌ 6 errors   landor::geo           Patch + LayerBinding{LayerId, storage::SourceId}
  placement.hpp    ❌ 7 errors   landor::geo           Placement = PatchId + position + transform ref
  patchset.hpp     ❌ 6 errors   landor::geo           compose patches/placements into one Area
  place.hpp        ❌ 7 errors   landor::world         Place = narrative identity over PlacementIds
  map.hpp          ❌ 15 errors  landor::geo           the hub: resolve/place/at; LAYER_BITS table
  chunk.hpp        ✅ clean      namespace Geo         placeholder skeleton, nothing uses it
  cache.hpp        ✅ clean      namespace Geo         placeholder skeleton, nothing uses it

  All ❌ above share ONE cause: the `Geo` → `landor::geo` bridge, see §6.
src/renderers/utf-8/         empty directory, nothing in it yet
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

**Hygiene pass of 2026-09-16** (uncommitted as of this writing; deletions are staged via `git rm`):

- `Rotation`/`Reflection`/`Orientation` moved from `orientation.cpp` **into `orientation.hpp`**; `.cpp` deleted.
- `Place` moved from `place.cpp` **into `place.hpp`** (keeps `namespace landor::world`, includes `placement.hpp`);
  `.cpp` deleted. Both moves verified by compiling a throwaway TU against them.
- Deleted every obsolete generated stub: `area/cache/chunk/layer/map/patch/patchset/placement/tile.cpp`
  (all were `X::X() {}` against header-only or template types). `src/world` now has **zero .cpp files**.
- Deleted `istorage.hpp` — obsolete virtual-interface experiment contradicting build-selected concrete storage.
- `CMakeLists.txt` reduced to main.cpp + headers, with the two placeholder headers grouped under a comment.
- `Map::place(...)` (both overloads) now return **`std::optional<PlacementId>`**: placement capacity is a
  compile-time bound (`std::array<std::optional<placement_type>, MaxPlacements>`) so exhaustion is a
  legitimate outcome and is visible in the type rather than hidden behind a reserved id.

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
| configure | `cmake -S . -B /tmp/lb -DCMAKE_BUILD_TYPE=Debug` | ✅ OK |
| full build | `cmake --build /tmp/lb -j8` | ✅ `Built target Landor` — **0 errors** (was 33 before §4) |
| `tools/checks/check-build.sh` | run it | ✅ exit 0 |
| binary runs | `./transient/pipeline3/builds/default/Landor` | ✅ `Memory system initialized, usable 65536 bytes.` |
| header syntax (§0 loop) | per-header `g++ -fsyntax-only` | ✅ 8 clean · ❌ 5 failing, 41 errors total, all cause #1 in §6 |
| tests, standalone | `g++ -std=c++20 -Isrc tests/test_coord.cpp -o /tmp/tc && /tmp/tc` | ✅ `All 121 tests passed.` |
| | `g++ -std=c++20 -Isrc tests/test_area.cpp -o /tmp/ta && /tmp/ta` | ✅ `All tests passed.` |
| CTest | `ctest --test-dir transient/pipeline3/builds/default` | ❌ `No tests were found!!!` — no `enable_testing()`, no test targets → `check-tests.sh` exits 2 |
| `tools/checks/check-static.sh` | — | ⚠️ untested since the pass; `clang-tidy` **is** installed at `/usr/bin/clang-tidy` |

**A green build no longer means much.** The only compiled TU is `main.cpp`, which includes nothing from
`src/world`. The real health signal is the per-header syntax loop in §0 (b), and CMake does **not** run it.
Until something compiles the headers, edits to them can be silently broken.

Tooling note: the check scripts default `BUILD_DIR` to `transient/pipeline3/builds/{default,static,sanitize}`,
which is *not* the Qt Creator tree at `transient/build/Desktop_Qt_6_11_2_Debug` (Qt 6.11.2 kit; clangd from it).
Override with `BUILD_DIR=... tools/checks/check-build.sh` to share one tree.

---

## 6. Breakage ledger

**Fixed by the §4 hygiene pass** (all were the bulk of the old 33 build errors): definitions stranded in
`.cpp` (`Rotation`/`Reflection`/`Orientation`, `Place`), the `X::X() {}` generated scaffolds, `istorage.hpp`,
and stale CMake entries for deleted files.

**Cause #1 — the only remaining blocker.** Half-finished namespace migration `Geo` → `landor::geo`:
`coord.hpp` (`namespace Geo`, line 11; `using Coord32 = Coord<int32_t>` at line 333) and `area.hpp` still
declare under `Geo`, while `patch/placement/patchset/place/map.hpp` live in `landor::geo` and name
`Coord32`, `Area`, `area_type` unqualified. Every one of the 41 header errors bottoms out here.
→ *fix:* one decision — migrate `coord.hpp`/`area.hpp` to `landor::geo`, or add a bridge such as
`namespace landor::geo { using Geo::Coord32; using Geo::Coord8; using Geo::Coord16; }` plus an `Area` alias.
Should be a design decision, not a search-and-replace.

**Structural gap worth closing now:** nothing compiles the world headers, so regressions are invisible.
Cheapest guards, either is enough: (a) a `tools/checks/check-headers.sh` running the §0(b) loop and failing on
any error; (b) a `tests/test_headers.cpp` TU that includes every header, wired into CTest.

Still-open design detail inside `map.hpp`: it forward-declares **`landor::geo::Storage`** and holds
`Storage& m_storage` (same for `Generator&`). Per the current architecture the concrete storage type is
chosen by the platform header and lives in `landor::storage` (`using Storage = StorageFilesystem;` etc.),
so `Map` should consume `storage::Storage&` rather than invent its own `geo::Storage`. Left untouched in
the hygiene pass on purpose — it depends on the platform-selected storage header, which does not exist yet.

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

- Next up per plan: the **platform-selected storage header** (`landor::storage::Storage` = filesystem / SD /
  ROM), then repoint `Map` at it (§6 last paragraph).
- Resolve cause #1, the `Geo` → `landor::geo` bridge — the single thing standing between the design headers
  and compiling.
- Guard the headers so a green build means something again (§6 structural gap) and wire tests into CMake
  (`enable_testing()` + one target per test) so `check-tests.sh` stops exiting 2.
- Where do coordinate z-components live? `Geo::Coord<T>` is **2D (x, y)** but `tile.hpp`/`map.hpp` commentary
  relies on `z == -1` meaning the ground/base plane. Unresolved tension, affects `TileView::base()`.
- Consistency sweep when convenient: `Map::set_position/set_rotation/set_reflection/set_orientation` return
  bare `bool` without `[[nodiscard]]`, now inconsistent with `place()` returning `std::optional`; decide the
  house style for "may fail" (a shared Landor result type would settle both).
- Same "storage owns the identity, geo owns the relationship" audit for other `geo` types (`Placement`,
  `PatchSet`) — check for locally redefined ids.
- `chunk.hpp`/`cache.hpp` remain placeholder skeletons (`namespace Geo`) waiting on their interface drafts.

---

## 10. How to keep this file honest

- Update the §5 numbers and the stamp line at the top whenever you touch build wiring or finish a header.
- A change this size is worth a commit; if you don't commit, at least bump `verified on`.
- Keep it under ~200 lines. It earns its place by being *skimmed*; long-form reasoning belongs in dev-docs.
- Never paste DESIGN_STATE.md content in here — link the `D-n`/`Q-n` instead.
- Note for future sessions: the owner often works **concurrently** in Qt Creator / another agent session.
  Re-check `git status` and file mtimes before editing, and diff against this file's stamp first.
