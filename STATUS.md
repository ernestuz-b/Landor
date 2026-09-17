# Landor — Project Status

This file describes **what exists now**, not the final architecture.

Source baseline reviewed before this documentation sync:

```text
cb753025158c6928f525d849f4b378199b6a4c4e
docs: sync mapping model with layer cache
```

For intended architecture, read `dev-docs/DESIGN_STATE.md`,
`dev-docs/DESIGN_DECISIONS.md`, and, for mapping specifically,
`dev-docs/MAPPING_MODEL.md`.

## Current repository state

### Build

The current `CMakeLists.txt` builds:

- `Landor`;
- `landor_tests` when `LANDOR_BUILD_TESTS=ON`.

The project is configured as C++23.

GoogleTest v1.18.0 is fetched through CMake `FetchContent`.

Known build-policy gaps remain:

- `BUILD_GMOCK` is still forced `OFF` even though GoogleMock is allowed by project policy;
- Landor-wide no-exceptions/no-RTTI enforcement is not yet wired in CMake;
- the intended aggressive warning set and warnings-as-errors policy is not yet fully encoded in CMake.

### Formatting

`.clang-format` now has the intended next-line brace style for declarations and control flow.

It still declares:

```text
Standard: c++20
```

and should be updated to C++23 for consistency with the build and coding rules.

### Tests

The test target currently includes:

```text
tests/test_world_headers.cpp
tests/test_area.cpp
tests/test_coord.cpp

tests/world/test_tile.cpp
tests/world/test_chunk.cpp
tests/world/test_cache.cpp

tests/platform/storage/test_storage_filesystem.cpp
```

`Tile`, `Chunk`, `Cache`, and filesystem storage now have behavioural GoogleTests.

The final test-layout rule is still to mirror `src/` under `tests/`. The newer mapping and platform-storage tests already do that; `test_area.cpp`, `test_coord.cpp`, and the all-headers tripwire still live at the test root and can be moved separately.

### Current source contracts

The substantive contracts currently under `src/` are:

```text
src/storage/
    types.hpp
    storage_contract.hpp

src/platform/storage/
    storage_filesystem.hpp
    storage_filesystem.cpp

src/world/
    coord.hpp
    area.hpp
    layer.hpp
    orientation.hpp
    tile.hpp
    chunk.hpp
    cache.hpp
    patch.hpp
    placement.hpp
    patchset.hpp
    place.hpp
    map.hpp
```

`src/renderers/utf-8/` exists but is not yet implemented.

The managed heap remains under:

```text
include/managed_heap/
```

and is treated as an imported component.

## Mapping architecture represented in source

The current mapping model is described in detail by `dev-docs/MAPPING_MODEL.md` and is now reflected by the source headers.

### `landor::geo::Layer`

A Layer is a type-level description of one spatial property. It has a stable `LayerId` and a `value_type`. It owns no storage, cache, persistence, or simulation policy.

### `landor::geo::Tile`

`Tile<CoordT, Layers...>` is a compact value representing one map coordinate.

It contains:

- its coordinate;
- one copied value for every layer supported by that build.

The current private representation packs byte-valued layers into a dense array. That representation is deliberately hidden so a future wider layer can change Tile internals without changing the public `tile[layer]` API.

A Tile has no reference or pointer back into Map, Cache, Storage, Patch data, or the managed heap. Modifying a returned Tile modifies only that copy.

### `landor::geo::Chunk`

Chunk is the aligned square spatial unit used by Cache/I/O for **one layer**.

It is not the generic game-side name for an arbitrary rectangular working area.

Chunk carries layer identity plus aligned spatial geometry. It deliberately does not bind itself to a `storage::SourceId`; source resolution remains a separate Map concern.

### `landor::geo::Cache`

Cache is now implemented as a bounded, fixed-capacity, **layer-oriented** resident store.

Capacity counts spatial slots. Each slot represents one canonical `CacheChunkSide × CacheChunkSide` area and contains independently resident planes for the supported layers.

The current Cache implements:

- canonical chunk calculation;
- per-layer residency checks;
- complete-Tile residency checks;
- fixed-capacity layer-plane fills;
- resident layer reads;
- Tile packing on presentation;
- area invalidation;
- full invalidation.

It deliberately has no replacement, eviction, dirty-state, or write-back policy yet. When every spatial slot is occupied, filling a new spatial area fails rather than inventing an eviction policy.

### `landor::geo::Map`

Map is the logical spatial surface and owns live Placements.

Its current header is aligned to the new Cache model:

- supported Layers remain part of the Map type;
- Cache remains layer-oriented;
- `Map::at()` is checked world access and returns `Tile<CoordT, Layers...>` by value;
- `Map::value<LayerT>()` remains layer-specific checked access;
- missing resident layers are intended to be populated through aligned Chunks;
- placement mutation invalidates affected cached layer data.

The actual Map resolution/population methods are still pending implementation. In particular, the repository has not yet completed the path from Patch/Placement/source resolution through Storage/Generator into `Cache::fill()`.

## Authored-world contracts

### `landor::geo::Patch`

An immutable reusable authored region containing identity, authored geometry, and zero or more `LayerBinding { LayerId, storage::SourceId }` entries.

Missing authored data for a layer is represented by absence of a binding, not by unsupported capability.

### `landor::geo::Placement`

One live occurrence of a Patch with its own identity, position, and Orientation.

Transform order remains:

```text
reflection -> rotation -> translation
```

### `landor::geo::PatchSet`

An immutable composition recipe. Selection/composition policy remains separate.

### `landor::world::Place`

A story-facing identity over one or more `PlacementId`s. It is intentionally separate from authored Patch identity and geometry.

## Storage architecture represented in source

`landor::storage::StorageBackend` remains the platform-independent logical contract:

```text
read(SourceId, Offset, span<byte>)
write(SourceId, Offset, span<const byte>)
size(SourceId, Size&)
```

Operations are whole-range success/failure. Physical paths, file handles, sectors, pages, erase blocks, and similar platform details stay below the boundary.

`StorageFilesystem` is no longer contract-only. The current `.cpp` implements:

- `read()`;
- `write()`;
- `size()`.

Reads and writes enforce logical range bounds and do not expose partial success.

The filesystem header exports:

```cpp
using Storage = StorageFilesystem;
```

as the build-selected concrete alias.

## Known source/document alignment tasks

These are known gaps, not invitations to redesign the architecture.

### Map storage seam

`map.hpp` still forward-declares `Storage` in `landor::geo`, while the intended common type is the build-selected `landor::storage::Storage`.

This should be corrected as a small, explicit seam change before substantial Map implementation depends on it.

### Storage error spelling

`src/storage/types.hpp` still uses snake_case scoped enum values such as:

```text
Error::invalid_source
Error::out_of_range
Error::read_failed
```

Project style requires PascalCase scoped enum values. Migrate them as a focused source/test change rather than silently diverging documentation from code.

### GoogleMock

CMake still forces `BUILD_GMOCK OFF`. Project policy allows GoogleMock when interaction testing is appropriate.

### Warning / exception / RTTI enforcement

The policy is documented, but CMake does not yet fully enforce it for Landor targets.

### Test layout

The newer tests mirror the source tree, while the older geometry tests still live directly under `tests/`.

## Mapping work that is intentionally still open

The following are not implemented and should not be invented as collateral work:

- final Map layer precedence once `Map::resolve()` is implemented;
- source/layer/spatial-coordinate to byte-offset mapping where not already specified by source format;
- cache replacement policy;
- dirty-state representation;
- write-back timing;
- procedural-state materialisation policy;
- final mutation API between simulations and live layer state;
- `Region` ownership/view/mutation semantics.

## Near-term implementation sequence

A sensible next sequence from the current tree is:

1. correct the Map → build-selected Storage type seam;
2. implement and test the smallest Map → Cache residency/population slice without inventing a storage format;
3. pin per-layer overlap/precedence behaviour with tests as `Map::resolve()` becomes real;
4. add replacement/write-back policy only after the fixed-capacity no-eviction path is working;
5. introduce `Region` only when a simulation needs an algorithmic working-area API;
6. separately finish mechanical policy alignment in CMake, `.clang-format`, enum spelling, and test layout.

Keep each step small and independently testable.
