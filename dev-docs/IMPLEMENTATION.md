# Landor — Current Implementation Model

This document explains how the current source contracts fit together.

It is intentionally narrower than `DESIGN_STATE.md`: it describes the implementation shape
we are actively building, not every future gameplay system.

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

They are deliberately usable with narrow embedded integer widths as well as host-friendly
widths.

Important implementation rules:

- numeric narrowing is explicit;
- signed overflow is never assumed to wrap;
- operations that promote must use a sufficiently wide intermediate;
- geometry functions should be `constexpr` when the standard-library/toolchain path permits
  it;
- boundary/extreme-width tests are important because narrow types expose bugs hidden by
  `int32_t`.

The current geometry test migration has already exposed narrow-type rotation construction
issues, which is exactly why all supported widths need instantiation in tests.

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

The type carries meaning; storage stays elsewhere.

The set of layers supported by a `Map` is part of the map type:

```cpp
Map<..., Terrain, Elevation, Fire>
```

The eventual capacity template shape may be consolidated into a structural policy as
implementation proceeds.

## 4. Synthetic Tile

`Tile<Layers...>` stores one `LayerValue<LayerT>` per supported layer.

Construction requires all layer values, which prevents returning a partially assembled
tile.

`Tile` is deliberately a value:

- safe across cache movement;
- safe across storage I/O;
- safe across managed-heap compaction;
- independent of where each layer value originated.

A renderer or rule system that only needs the state at one coordinate can consume a tile
without learning the persistence model.

## 5. Patch catalogue

`Patch<CoordT>` is immutable authored metadata.

The patch object borrows:

- its name (`std::string_view`);
- its layer-binding table (`std::span<const LayerBinding>`).

Those referenced objects must therefore outlive the patch.

`LayerBinding` contains:

```cpp
LayerId layer;
storage::SourceId source;
```

No storage path or device information belongs in the patch.

Layer lookup is intentionally linear because the number of bound layers is expected to be
small. Do not add an index/hash structure without measurement.

Absence is returned as `nullptr`.

## 6. Placement

`Placement<CoordT>` is a small live value:

```text
PlacementId
PatchId
position
Orientation
```

It does not point to its patch and does not own authored data.

This keeps placements safe to store/move independently and prevents accidental lifetime
coupling to a patch object.

The owning `Map` resolves `PatchId` through its catalogue.

Although `Placement` exposes mutators on the value type, callers holding map-owned
placements should receive const access; map-level mutation APIs are responsible for cache
invalidation.

## 7. Place

`landor::world::Place` bridges physical map occurrences and story systems.

It borrows a span of `PlacementId`s.

It intentionally does not:

- query geometry;
- own patches;
- own placements;
- know storage.

Future schedules/ownership/dialogue code should be able to carry `PlaceId` without
depending on `landor::geo` internals beyond the association maintained at a higher level.

## 8. PatchSet

`PatchSetRole` contains:

```text
role id
candidate PatchIds
min_count
max_count
```

`PatchSet` borrows a span of roles.

The deterministic composer is a separate component still to be implemented.

Do not make candidate array order silently become gameplay priority. Selection policy must
be explicit.

## 9. Map

The current `Map` template is the architectural hub.

It currently parameterizes:

- maximum placements;
- synthetic tile cache size;
- coordinate type;
- supported layers.

The capacity arguments should eventually become a named structural policy once the
implementation slice makes that refactor useful.

### Owned state

Map owns:

- its live placement slots;
- placement count/id progression;
- disposable synthetic tile-cache entries.

### Borrowed state

Map borrows:

- authored patch catalogue;
- storage object;
- generator/fallback source.

The storage/generator seam is not fully aligned yet. In particular, `Map` should use the
build-selected `landor::storage::Storage` rather than a stale geography-local forward
declaration.

### Resolution

The intended per-layer path is conceptually:

```text
working/materialized state
    -> top authored placement supplying the requested layer
    -> procedural/default fallback
```

Do not implement speculative cache/storage machinery before the smallest correct resolution
slice exists and is tested.

### Placement capacity exhaustion

The current header returns `std::optional<PlacementId>` from `place()`.

Do not mechanically churn this API. When implementing placement, choose the domain result
that best expresses the actual outcomes. With C++23 available, `std::expected` is preferred
if a meaningful reason needs to accompany failure.

## 10. Storage contract

`StorageBackend<T>` requires:

```cpp
Result read(SourceId, Offset, std::span<std::byte>) const;
Result write(SourceId, Offset, std::span<const std::byte>);
Result size(SourceId, Size&) const;
```

The contract is logical and all-or-fail.

The concrete implementation may do anything necessary underneath, including operations
that are not physically in-place.

Higher layers must not rely on:

- filesystem path structure;
- filesystem buffering;
- SD-sector size;
- NAND erase-block policy;
- flash page size.

## 11. Filesystem storage

`StorageFilesystem` currently stores:

- root path as `std::string_view`;
- a span of relative source paths.

`SourceId` is a dense index into that source table.

This intentionally avoids a map/hash lookup at runtime.

The concrete implementation is incomplete; only its contract exists.

The strings backing the root/source views must outlive the storage object.

## 12. Platform alias

The platform file exports:

```cpp
using Storage = StorageFilesystem;
```

This is the desired pattern.

As additional targets arrive, CMake should make a different implementation/header visible
to the common code rather than adding runtime selection.

## 13. Managed heap

The managed heap is available for objects whose dynamic lifetime truly requires it.

Landor-side rule:

```text
fixed/value storage if bounded
        |
        v
managed heap only when genuinely dynamic
```

Do not move Map's compile-time placement slots, tile cache or similar bounded structural
state into the managed heap just because the heap exists.

When a managed object is used, persistent code keeps handles/ids rather than resolved raw
addresses. Raw access is scoped according to the managed-heap component's own contract.

## 14. Configuration and generated files

The intended build tree is:

```text
transient/build/
```

Generated configuration may live under:

```text
transient/build/generated/landor/config.hpp
```

A compile-time value that changes type layout should still feed a template/policy at the
composition root rather than being fetched from a deep global config object.

## 15. Tests

The latest source has fully migrated geometry tests to GoogleTest.

Final layout rule is:

```text
tests/<source-subdirectory>/test_<source-name>.cpp
```

CMake explicitly lists test translation units.

Keep the all-headers compile tripwire or equivalent contract check because it catches
cross-header namespace/include drift that behavioural tests may never instantiate.

GoogleMock may be used for interactions once storage/generator seams gain behaviour worth
mocking.

## 16. Logging implementation direction

No logger is implemented yet.

When introduced, build the smallest useful slice:

1. level/category enum;
2. monotonic timestamp;
3. `std::source_location`;
4. output record/sink seam;
5. compile-out macros;
6. RAII function trace;
7. host serialized sink;
8. embedded fixed SPSC transport only when the embedded port needs it.

Do not pre-build a general logging framework.

## 17. Known implementation alignment tasks

Before expanding architecture substantially, align these mechanical contracts:

- C++23 in CMake;
- exceptions/RTTI disabled for Landor code;
- GoogleMock no longer forcibly disabled;
- warning target/options upgraded to the project policy;
- `.clang-format` control braces moved to next line;
- scoped enum values migrated to PascalCase as touched;
- geometry tests moved under `tests/world/`;
- `Map` storage type corrected to the build-selected `landor::storage::Storage`.

Each should be a small independent change with tests.
