# Landor — Current Implementation Model

This document explains how the current source contracts fit together.

It is intentionally narrower than `DESIGN_STATE.md`: it describes the implementation shape we are actively building, not every future gameplay system.

For the detailed mapping model, read `MAPPING_MODEL.md` as well.

## 1. Current layers of the codebase

```text
world/environment contracts
    src/world/

logical storage contract
    src/storage/

selected platform implementations
    src/platform/

renderer implementations
    src/renderers/

dynamic object allocator (imported component)
    include/managed_heap/

tests
    tests/
```

Directories group responsibility. They do not mirror C++ namespaces.

## 2. Geometry

`Coord<T>` and `Area<CoordT>` are foundational value types.

They are deliberately usable with narrow embedded integer widths as well as host-friendly widths.

Important implementation rules:

- numeric narrowing is explicit;
- signed overflow is never assumed to wrap;
- operations that promote must use a sufficiently wide intermediate;
- geometry functions should be `constexpr` when the standard-library/toolchain path permits it;
- boundary/extreme-width tests are important because narrow types expose bugs hidden by `int32_t`.

## 3. Layers

`src/world/layer.hpp` defines the `Layer` concept.

A concrete layer is a type:

```cpp
struct Terrain
{
    static constexpr LayerId id = ...;
    using value_type = ...;
};
```

The type carries meaning; storage, cache, persistence, and simulation behaviour stay elsewhere.

The set of layers supported by a `Map` is part of the map type.

## 4. Tile

`src/world/tile.hpp` defines:

```cpp
Tile<CoordT, Layers...>
```

A Tile is a small owned value for one coordinate.

It contains:

- its coordinate;
- one copied property value per supported layer.

The current private representation assumes byte-valued layers and stores them in a dense `std::array`. That is an implementation detail, not a public contract. If a future layer requires a wider value, Tile's private packing can change while `tile[layer]` remains the public access form.

Important invariants:

- Tile owns its values;
- Tile contains no pointer/reference/handle back into Map, Cache, Patch data, Storage, or the managed heap;
- copying a Tile is ordinary value copying;
- changing a Tile copy does not mutate the Map;
- `tile[Fire]` reads a property already present in that Tile and performs no Map/Cache/Storage operation.

## 5. Chunk

`src/world/chunk.hpp` defines the spatial I/O/cache unit.

A Chunk represents:

```text
one LayerId
one aligned square origin
one side length
```

It deliberately does not carry a `storage::SourceId`. Which backing source supplies a layer is resolved elsewhere by Map/source composition.

Chunks are aligned to a grid defined by their own side length. `Chunk::containing()` handles negative coordinates using floor-like alignment rather than truncation toward zero.

Chunk is not the generic game-side name for an arbitrary work rectangle. `Area` remains geometry; `Region` is the intended future algorithmic working-area concept.

## 6. Cache

`src/world/cache.hpp` now contains the first real Cache implementation.

The central invariant is:

> Cache remains layer-oriented internally.

It does not store an array of complete Tiles.

The type is parameterized by:

```text
Capacity
CacheChunkSide
CoordT
Layers...
```

### Spatial slots

`Capacity` counts canonical spatial slots.

Each slot stores:

```text
occupied flag
canonical origin
per-layer residency flags
one fixed layer plane per supported Layer
```

For a canonical side `N`, each layer plane contains `N * N` values in row-major order.

A slot may therefore have some layers resident and others missing for the same spatial area.

### Implemented Cache behaviour

The current implementation provides:

- `chunk_for<LayerT>(position)`;
- `contains<LayerT>(position)`;
- `contains(position)` for complete-Tile residency;
- `value<LayerT>(position)` for resident layer reads;
- `tile(position)` for presentation-time Tile packing;
- `fill<LayerT>(chunk, values)`;
- `invalidate(area)`;
- `invalidate_all()`.

`fill()` can populate a missing layer in an already-existing spatial slot without consuming another slot.

If a new spatial area is requested when all slots are occupied, `fill()` returns `false`. The implementation deliberately does not invent an eviction policy.

### Tile packing

`Cache::tile(position)` gathers one value from each resident layer plane and constructs a fresh `Tile<CoordT, Layers...>`.

The returned Tile is not cache storage. It remains valid independently of future cache invalidation or replacement.

### Deferred Cache policy

The current Cache has no:

- replacement policy;
- dirty-state model;
- write-back policy;
- procedural materialisation policy.

These are future slices and must not be inferred from the existing no-eviction implementation.

## 7. Patch catalogue

`Patch<CoordT>` is immutable authored metadata.

The patch object borrows:

- its name (`std::string_view`);
- its layer-binding table (`std::span<const LayerBinding>`).

Those referenced objects must outlive the Patch.

`LayerBinding` contains:

```cpp
LayerId layer;
storage::SourceId source;
```

No storage path or device information belongs in the Patch.

Layer lookup is intentionally linear because the number of bound layers is expected to be small. Do not add an index/hash structure without measurement.

Absence is returned as `nullptr`.

## 8. Placement

`Placement<CoordT>` is a small live value:

```text
PlacementId
PatchId
position
Orientation
```

It does not point to its Patch and does not own authored data.

This keeps placements safe to store/move independently and prevents accidental lifetime coupling to a Patch object.

The owning Map resolves `PatchId` through its catalogue.

Although `Placement` exposes mutators on the value type, callers holding map-owned placements should receive const access; map-level mutation APIs are responsible for Cache invalidation.

## 9. Place

`landor::world::Place` bridges physical map occurrences and story systems.

It borrows a span of `PlacementId`s.

It intentionally does not:

- query geometry;
- own Patches;
- own Placements;
- know Storage.

## 10. PatchSet

`PatchSetRole` contains:

```text
role id
candidate PatchIds
min_count
max_count
```

`PatchSet` borrows a span of roles.

The deterministic composer is a separate component still to be implemented.

Do not make candidate array order silently become gameplay priority. Selection policy must be explicit.

## 11. Map

`src/world/map.hpp` is the architectural hub.

The current template parameterizes:

- maximum Placements;
- Cache spatial-slot capacity;
- canonical Cache chunk side;
- coordinate type;
- supported Layers.

A named structural policy may still become useful later if the positional capacity arguments become cumbersome, but the current template parameters are authoritative source reality.

### Owned state

Map owns:

- its live Placement slots;
- Placement count/id progression;
- one mutable, fixed-capacity layer-oriented Cache.

The Cache is mutable because logically-const world reads may populate residency without changing logical world state.

### Borrowed state

Map borrows:

- authored Patch catalogue;
- Storage object;
- Generator/fallback source.

The Storage seam is still not fully aligned: `map.hpp` currently forward-declares `Storage` in `landor::geo`, while the intended build-selected common type is `landor::storage::Storage`.

Correct that explicitly before substantial Map implementation depends on the stale declaration.

### Access

Public Map access is intended to be checked.

`Map::at(position)` should:

```text
validate position
    -> ensure every required layer is resident
    -> ask Cache to pack a Tile
```

`Map::value<LayerT>(position)` should similarly validate the coordinate and ensure only the requested layer is resident.

There is deliberately no public unchecked `Map::operator[]` path.

### Residency population

The intended next implementation seam is:

```text
Map request
    -> Cache residency check
    -> determine missing Layer Chunk(s)
    -> resolve source/value contribution through Map rules
    -> Storage / Generator / working state as appropriate
    -> Cache::fill<LayerT>()
```

Do not invent a source-file byte layout if the repository does not already define how a spatial layer position maps to a byte range.

### Per-layer resolution

The intended conceptual ordering remains:

```text
materialized/working override
    -> highest-priority authored Placement that supplies the requested layer
    -> procedural/default source
```

The precise precedence rule is not yet implemented and must be pinned by tests when `Map::resolve()` becomes real.

### Placement capacity exhaustion

The current header returns `std::optional<PlacementId>` from `place()`.

Do not mechanically churn this API. When implementing placement, choose the domain result that best expresses the actual outcomes. With C++23 available, `std::expected` is preferred if a meaningful reason needs to accompany failure.

## 12. Storage contract

`StorageBackend<T>` requires:

```cpp
Result read(SourceId, Offset, std::span<std::byte>) const;
Result write(SourceId, Offset, std::span<const std::byte>);
Result size(SourceId, Size&) const;
```

The contract is logical and all-or-fail.

Higher layers must not rely on:

- filesystem path structure;
- filesystem buffering;
- SD-sector size;
- NAND erase-block policy;
- flash page size.

## 13. Filesystem storage

`StorageFilesystem` stores:

- root path as `std::string_view`;
- a span of relative source paths.

`SourceId` is a dense index into that source table.

The strings backing the root/source views must outlive the Storage object.

The current `.cpp` implements `read()`, `write()`, and `size()`.

The current behaviour includes:

- unknown-source rejection;
- subtraction-style range checks that avoid overflow;
- all-or-fail reads;
- writes that replace bytes without growing/truncating/creating the source;
- filesystem failures reported through the existing Storage error enum.

The implementation intentionally does not infer specific causes such as read-only media or no-space when `std::fstream` does not provide reliable evidence.

## 14. Platform alias

The filesystem platform header exports:

```cpp
using Storage = StorageFilesystem;
```

This is the desired pattern.

As additional targets arrive, CMake should select a different implementation/header rather than add runtime backend selection.

## 15. Managed heap

The managed heap is available for objects whose dynamic lifetime truly requires it.

Landor-side rule:

```text
fixed/value storage if bounded
        |
        v
managed heap only when genuinely dynamic
```

Do not move Map's compile-time Placement slots, Cache slots, or similar bounded structural state into the managed heap merely because the heap exists.

When a managed object is used, persistent code keeps handles/ids rather than resolved raw addresses. Raw access is scoped according to the managed-heap component's own contract.

## 16. Configuration and generated files

The intended build tree is:

```text
transient/build/
```

Generated configuration may live under:

```text
transient/build/generated/landor/config.hpp
```

A compile-time value that changes type layout should still feed a template/policy at the composition root rather than being fetched from a deep global config object.

## 17. Tests

The test target currently contains both older root-level geometry tests and newer mirrored tests.

Mirrored mapping tests now exist for:

```text
tests/world/test_tile.cpp
tests/world/test_chunk.cpp
tests/world/test_cache.cpp
```

Filesystem Storage tests live under:

```text
tests/platform/storage/test_storage_filesystem.cpp
```

The all-headers compile tripwire remains useful because it catches cross-header namespace/include drift that behavioural tests may never instantiate.

Final layout rule remains:

```text
tests/<source-subdirectory>/test_<source-name>.cpp
```

so the older root-level `test_coord.cpp` and `test_area.cpp` can be moved separately.

## 18. Logging implementation direction

No logger is implemented yet.

When introduced, build the smallest useful slice rather than pre-building a framework.

## 19. Known implementation alignment tasks

Current mechanical/source-policy gaps include:

- Map still uses a stale geography-local `Storage` declaration instead of the build-selected `landor::storage::Storage` type;
- `BUILD_GMOCK` is still forced off in CMake despite project policy allowing GoogleMock;
- no-exceptions/no-RTTI and the full warning policy are not yet fully enforced by CMake;
- `.clang-format` still declares C++20 while the project builds as C++23;
- scoped storage `Error` values are still snake_case rather than PascalCase;
- older geometry tests still live directly under `tests/`.

Treat these as separate reviewable changes.

## 20. Next implementation slice

The architecture is now sufficiently defined to begin Map integration.

The recommended order is:

1. correct the Map → Storage type seam;
2. implement checked `value<LayerT>()` / residency for the smallest source path that is actually defined;
3. implement checked `at()` by ensuring required layers then calling `Cache::tile()`;
4. pin overlap/precedence semantics with tests as source resolution becomes concrete;
5. add replacement/write-back policy only after the no-eviction path is proven;
6. introduce `Region` only when a simulation needs it.

If the source-to-byte mapping required for Chunk loading is not defined, stop and report that missing contract rather than inventing a storage format.
