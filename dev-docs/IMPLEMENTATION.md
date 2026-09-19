# Landor — Current Implementation Model

This document explains how the current source contracts fit together.

It is intentionally narrower than `DESIGN_STATE.md`: it describes the implementation shape we are actively building, not every future gameplay system.

For the detailed mapping model, read `MAPPING_MODEL.md` as well. The authored layer file contract is defined separately in `LAYER_SOURCE_FORMAT.md`.

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
per-layer dirty flags
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
- `set<LayerT>(position, value)` for resident layer mutation;
- `dirty<LayerT>(position)` for per-plane dirty queries;
- `has_dirty()` for any-dirty-state queries;
- `dirty_chunks<LayerT>(output)` for fixed-capacity enumeration of the layer's dirty planes;
- `mark_clean<LayerT>(chunk)` to clear one plane's dirty bit (the write-back hook of D-38);
- `tile(position)` for presentation-time Tile packing;
- `fill<LayerT>(chunk, values)`;
- `invalidate(area)`, `[[nodiscard]] bool`, dirty-safe;
- `invalidate_all()`, `[[nodiscard]] bool`, dirty-safe.

`fill()` can populate a missing layer in an already-existing spatial slot without consuming another slot.

If a new spatial area is requested when all slots are occupied, `fill()` returns `false`. The implementation deliberately does not invent an eviction policy.

`fill()` is also dirty-safe: it refuses to overwrite a dirty target plane and returns `false` without writing, so a resolved value can never silently replace an unflushed mutation (D-37).

`set<LayerT>()` mutates an already-resident value and marks that resident layer plane dirty when — and only when — the value actually changes; a same-value set is a no-op on a clean plane. Dirty flags are per resident layer plane per spatial slot, not per cell: `fill()` installs a clean plane, and once a plane is dirty it stays dirty. The only clearing path is `mark_clean<LayerT>()`, called only by Map's explicit `flush<LayerT>()` (D-38) after a plane has been fully persisted: no fill, invalidation or placement operation clears dirt, so dirty state otherwise persists until the Cache is destroyed.

Both invalidation forms are atomic and dirty-safe: if any slot that would be discarded contains a dirty resident plane, the whole operation is refused and the Cache remains unchanged, returning `false`. A dirty slot outside the invalidated area does not block `invalidate(area)`, because it is not discarded.

### Tile packing

`Cache::tile(position)` gathers one value from each resident layer plane and constructs a fresh `Tile<CoordT, Layers...>`.

The returned Tile is not cache storage. It remains valid independently of future cache invalidation or replacement.

### Deferred Cache policy

The current Cache has a dirty-state model (D-37): one dirty bit per resident layer plane per spatial slot, marked by a changed `set<LayerT>()`, sticky, and protected by dirty-safe `fill()`/`invalidate()`/`invalidate_all()` refusal. Its only write-back exposure is the persistence hooks (`dirty_chunks<LayerT>()`, `mark_clean<LayerT>()`), which Map's explicit `flush<LayerT>()` (D-38) drives; the Cache itself performs no write-back scheduling.

It still has no:

- replacement policy;
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

Patch-local `(0, 0)` is the authored transform/source origin. For dense v1 authored sources, the source rectangle begins at that origin; its `D` dimensions match the authored local extent and its `P` position matches `Patch::natural_position()`.

### Authored layer source format

`dev-docs/LAYER_SOURCE_FORMAT.md` defines the current version 1.0 authored source layout.

One source corresponds to one Patch layer and conventionally uses:

```text
<PatchName>.<LayerName>.layer
```

The file begins with LF-terminated metadata records. `V`, `D`, and `P` currently describe the format version, dimensions, and authored natural position. The first empty line (`0x0A 0x0A`) ends metadata.

The remaining bytes are a dense one-byte-per-cell row-major grid. Each row contains exactly the declared width in cell bytes followed by one LF. ASCII space (`0x20`) means “this Patch has no authored contribution for this layer at this coordinate”; it is not synonymous with runtime zero.

Once `data_offset` is known, Patch-local cell `(x, y)` has physical source offset:

```text
data_offset + y * (width + 1) + x
```

The format is intentionally extensible through future metadata records, including possible layer-specific records, but version 1.0 does not define them. Sparse coordinate-record data is deferred.

A minimal streaming parser/source-reader now implements this contract in `src/world/layer_source.hpp`: it parses the bounded metadata region incrementally (no whole-file read or copy), exposes the parsed layout with direct cell addressing, and reads bounded row fragments through the Storage contract. Each non-empty cell read also checks that the requested bytes contain no structural LF and performs a one-byte check of the touched row's LF terminator; untouched rows are not scanned.

The generic reader deliberately stays unaware of Patch semantics. The dense v1 Patch/source geometry contract is enforced by a separate bridge header, `src/world/authored_layer_source.hpp`:

```cpp
template<typename CoordT>
[[nodiscard]] constexpr
std::expected<void, AuthoredLayerSourceError>
validate_authored_layer_source(
    const Patch<CoordT>& patch,
    const LayerSourceLayout& source) noexcept;
```

It takes an already-parsed `LayerSourceLayout` (no Storage I/O) and checks that the Patch local area is non-empty and anchored at `(0, 0)`, that the source `D` dimensions equal the Patch local extent exactly, and that the source `P` position equals `Patch::natural_position()`. An empty or non-zero-origin local area is rejected at this seam rather than translated or normalized, and `Patch` itself remains unconstrained authored metadata; the check is not enforced in the `Patch` constructor.

Both comparisons run in 64-bit signed intermediates. The extent is computed from the min/max corners instead of `Area::width()`/`height()`, because those accessors wrap for narrow scalar types when the true extent exceeds the scalar maximum (a 128-cell `Coord8` area reports `width() == -128` — a separate Area accessor limitation the validator avoids). The source `P` (`Coord32`) is never narrowed into the Patch's coordinate type before comparing, so an out-of-range `P` (for example 128 against a `Patch<Coord8>`) reports `PositionMismatch` instead of wrapping to -128. The helper validates geometry only: LayerId, SourceId, backend and source sharing are outside its contract.

## 8. Placement

`Placement<CoordT>` is a small live value:

```text
PlacementId
PatchId
position
Orientation
```

It does not point to its Patch and does not own authored data.

`position` is specifically the Map coordinate of Patch-local `(0, 0)`. Reflection and rotation operate about that local origin, then translation adds `position`; the transformed bounds are not renormalised to a new top-left.

The point transform is implemented on the value: `local_to_world()` and `world_to_local()` both return `std::optional<coord_type>`. Reflection, rotation, and translation/subtraction are performed in a wide signed intermediate, so narrow coordinate types cannot overflow, and a result outside the scalar range is reported as `nullopt` rather than wrapped or clamped. Transforming a coordinate does not require a `Patch`; local-area membership remains a separate `patch.local_area().contains(local)` check for Map.

This keeps placements safe to store/move independently and prevents accidental lifetime coupling to a Patch object.

The owning Map resolves `PatchId` through its catalogue.

Although `Placement` exposes mutators on the value type, callers holding map-owned placements receive const access through the Map's public lookup; the map-level mutation APIs are responsible for Cache invalidation.

The live lifecycle is now implemented in Map: `place()` returns `MapPlacementResult`, public `placement(PlacementId)` is const-only, and `set_position()`, `set_rotation()`, `set_reflection()`, and `set_orientation()` return `bool`, reject unknown ids, and mutate only when the requested value actually differs. See section 11 for the result contract and D-33 in `DESIGN_DECISIONS.md` for the current whole-cache invalidation policy.

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
- terminal fallback provider type, constrained by `LayerFallbackProvider`;
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
- the authored storage role (`const landor::storage::Storage&`);
- the runtime storage role (`landor::storage::Storage&`);
- the runtime layer binding catalogue (`std::span<const RuntimeLayerBinding>`);
- terminal fallback provider.

Map carries two explicit storage roles because authored assets and runtime save-state must not be assumed to share one root or device (see `LAYER_STORAGE_MODEL.md`). Both references currently use the build-selected `landor::storage::Storage` alias exposed by the selected platform header (currently `StorageFilesystem`); `map.hpp` includes that header directly rather than carrying a geography-local `Storage` type. That shared backend type is current source reality, not a permanent requirement: if a concrete target needs heterogeneous authored/runtime backends, that is a platform-composition concern for a later slice. The authored role is borrowed as const, so Map can read authored sources but never write them; the runtime role is a mutable reference because the explicit write-back (D-38) writes through it, and future materialisation may extend that role. The roles are semantic references, not a physical-separation requirement: the same Storage object may legitimately fill both roles. Both storage objects outlive the Map.

Map also borrows the runtime layer binding catalogue, a `std::span<const RuntimeLayerBinding>` from `src/world/runtime_layer_source.hpp` scoped to this Map instance: a binding `{ layer, source }` identifies the logical runtime/materialised overlay source of `(Map::id(), layer)` in the runtime storage role (D-35). The constructor asserts the catalogue precondition — every binding names a layer supported by the Map type, and no `LayerId` appears twice in the span; a duplicate is invalid configuration, not a precedence case. Zero bindings means this Map currently has no persistent runtime overlay source for those layers, which is not the same as an unsupported layer. Checked reads consult the catalogue (D-36): when a binding names `LayerT`, `Map::runtime_binding<LayerT>()` returns it and the bound runtime source is opened and validated once per missing layer Chunk, with non-space cells authoritative over authored state; when no binding names the layer the runtime storage role is never inspected. The only current write path is the explicit write-back: `flush<LayerT>()` consults the catalogue to find the bound runtime source before persisting a dirty plane (D-38). The backing records outlive the Map.

The terminal fallback dependency is a `FallbackT` template parameter constrained by `LayerFallbackProvider<FallbackT, CoordT, Layers...>` from `src/world/layer_fallback.hpp`. Map stores a `const fallback_type&` and owns nothing, and exposes no public accessor for it. The provider answers one small operation:

```cpp
provider.template value<LayerT>(world_position)
    -> exactly LayerT::value_type
```

It covers procedural baseline generation or a fixed layer default behind that operation, always answers (absence is not an outcome), and must return the exact layer value type. The old vague `Generator&` placeholder is not part of the contract.

### Access

Public Map access is checked.

`Map::at(position)` is implemented:

```text
validate position
    -> ensure every supported layer is resident (declared layer order, fail-fast)
    -> ask Cache to pack a Tile
```

It validates the coordinate first (OutOfBounds before any cache, source or fallback work); the non-template `ensure_resident()` then walks the supported layers in the Map template's declared order, resolving only the missing ones through the chunk-plane seam below and failing fast on the first failure with that exact `MapError`; on success `Cache::tile()` packs the complete owned `Tile`. The scalar overload `at(x, y)` forwards to `at(coord_type{x, y})`.

`Map::value<LayerT>(position)` is implemented: it validates the coordinate first (OutOfBounds before any cache, source or fallback work), ensures only the requested layer is resident through the chunk-plane seam below, and returns the resident layer value from the Cache.

`Map::set<LayerT>(position, value)` is the first mutable world-state path: it validates the coordinate first, ensures only the requested layer is resident through the same chunk-plane seam (installing a clean plane when the Chunk is missing), then mutates the resident value through `Cache::set<LayerT>()`, which marks the layer plane dirty only when the value actually changes. No Storage write happens, dirty state is not a `MapError`, and the dirty resident value is authoritative for subsequent reads of that Chunk — `value<LayerT>()` and `at()` return it without rereading runtime, authored or fallback state. A missing runtime binding only means there is no persistent runtime source yet; a dirty plane that genuinely differs from its backing state cannot be persisted without a binding, and `flush<LayerT>()` fails it with `MapPersistenceErrorCode::MissingRuntimeBinding` (D-38).

`Map::flush<LayerT>()` is the explicit dirty-state write-back (D-38): it collects the dirty `LayerT` Chunks through `Cache::dirty_chunks<LayerT>()` into fixed-capacity stack storage, re-resolves each plane's fresh backing answer through the normal resolution chain (never through the dirty resident Cache), compares it with the live plane over the logical in-Map cells only, marks the plane clean without any write when nothing differs, and otherwise persists exactly the differing cells through the bound runtime source with `write_cells()` (preflighting encodability first), marking the plane clean only after the whole plane succeeds. `Map::flush_all()` walks the declared template layer order and fails fast. The result is the dedicated `MapPersistenceResult` domain of `src/world/map_persistence.hpp`; no `MapError` value is introduced.

There is deliberately no public unchecked `Map::operator[]` path.

The checked access contract is now expressed as an expected-based result in `src/world/map_result.hpp`:

```cpp
MapError = std::variant<MapErrorCode, LayerSourceError, AuthoredLayerSourceError, RuntimeLayerSourceError>
MapResult<T> = std::expected<T, MapError>
```

- `Map::at(position)` and `Map::at(x, y)` return `MapResult<tile_type>`;
- `Map::value<LayerT>(position)` returns `MapResult<LayerT::value_type>`;
- the private seam is the chunk-plane resolver `resolve_chunk<LayerT>(chunk, values)`, which returns `MapResult<void>` and resolves one complete canonical layer plane without heap allocation; both the templated `ensure_resident<LayerT>()` and the non-template `ensure_resident()` are implemented, and both return `MapResult<void>`; the non-template form walks the declared layer pack in the Map template's declared order, resolves only the layers not yet resident, fails fast, and returns the exact first `MapError` unchanged.

`MapErrorCode` carries the Map-local outcomes: `OutOfBounds` (the coordinate does not belong to the Map; checked before Cache/Storage work) and `CacheFull` (the bounded Cache cannot accept another spatial slot; no eviction policy exists yet). The lower-level source errors are preserved rather than flattened: the exact `LayerSourceError` (parser/read failures for any source role), the exact `AuthoredLayerSourceError` (authored Patch/source geometry failures), and the exact `RuntimeLayerSourceError` (runtime Map/source geometry failures, D-36). `storage::Error` never appears in `MapError`; the layer source reader already maps Storage failures to `LayerSourceError::StorageFailed`.

A `Placement::world_to_local()` that returns `nullopt` while testing whether a Placement contributes at one world coordinate is not a Map error: that Placement simply does not contribute there, so no transform failure belongs in `MapError`.

Unsupported layer types remain compile-time errors. Both the single-layer `value<LayerT>()` path and the multi-layer `at()` path are implemented and tested.

### Residency population

The implemented single-layer seam is:

```text
Map request (value<LayerT>, set<LayerT>)
    -> contains(position) check (OutOfBounds before any other work)
    -> Cache residency check (a resident hit touches no source and no fallback)
    -> determine the missing canonical layer Chunk containing position
    -> resolve the complete layer plane through Map rules
    -> Cache::fill<LayerT>()
```

Resolution operates at chunk granularity rather than point granularity: a cache miss requires a complete canonical `CacheChunkSide × CacheChunkSide` layer plane, so resolving individual points would reopen the same `.layer` metadata over and over. `resolve_chunk<LayerT>()` fills the plane row-major (x fastest) into the caller's storage with no heap allocation:

- each plane cell is first classified as a logical in-Map position or as cache padding; world coordinates are derived from `chunk.origin()` plus the local x/y in a wide signed intermediate, so cells near the coordinate limits are padding rather than wrapped coordinates; cache padding never calls the fallback, never consults authored or runtime sources, and stays value-initialized, and public checked access cannot expose it;
- when a runtime binding names the layer, the bound runtime source is then opened exactly once through the runtime storage role and validated exactly once against the complete Map geometry (`validate_runtime_layer_source()`); the unresolved in-Map cells are read one at a time through `read_cells()`, with local coordinates computed as `world - map.area().min()` in wide `std::int64_t` before the explicit `std::uint32_t` conversion, and each non-space runtime cell resolves its cell (space leaves the cell unresolved); a missing binding skips this pass entirely without inspecting the runtime storage;
- the live Placements are collected into a fixed pointer array and sorted by descending `PlacementId` (stable identity precedence, not array slot order);
- each considered Placement is resolved against its Patch, checked for a `LayerT` binding, and inspected for coverage of the still-unresolved in-Map cells before its source is opened; the source is then opened once, validated once against the Patch, and read cell by cell through the streaming reader — always through the authored storage role; a Placement whose relevant cells are all already resolved is never opened;
- a Placement contributes to a cell only when its Patch binds `LayerT`, the world coordinate inverse-transforms into the Patch's local area, and the authored cell carries a contribution (not ASCII space `0x20`);
- every still-unresolved in-Map cell is then answered by the terminal fallback provider.

The dense authored source-to-byte mapping is defined by `LAYER_SOURCE_FORMAT.md` and is implemented by the streaming reader in `src/world/layer_source.hpp`, and the world/Placement-to-Patch-local point transform is implemented and tested in `Placement`. The terminal fallback contract at the end of that path is pinned in `src/world/layer_fallback.hpp` and is a real borrowed Map dependency; `value<LayerT>()` reaches the provider through that seam, not through a reintroduced generator placeholder.

### Per-layer resolution

The implemented resolution order for checked access is:

```text
resident Cache
    -> runtime/materialized overlay
    -> authored Placements, highest PlacementId first
    -> terminal fallback provider (procedural baseline or layer default)
```

The runtime overlay step is implemented (D-36): a non-space runtime cell is the authoritative current world value for its cell and outranks every authored Placement and the fallback; a space runtime cell (`0x20`) contributes nothing, so the cell continues to authored Placements and then to the fallback. A missing binding means no runtime overlay exists for that layer (normal absence, authored/fallback behaviour unchanged); a present binding whose source cannot be opened, parsed or validated is a checked-access failure — the exact `LayerSourceError` or `RuntimeLayerSourceError` propagates instead of falling through. When the runtime overlay resolves every logical cell of the requested Chunk, no authored source is opened and the fallback is never queried; otherwise authored resolution sees only the still-unresolved cells through the existing resolved-mask seam. There is only one runtime source per Map layer (D-35), so there is no runtime-vs-runtime precedence.

Authored precedence is pinned to stable Placement identity: a higher `PlacementId` has higher precedence, so a later successful placement overlays an earlier one (see `DESIGN_DECISIONS.md`, D-34). The rule follows the monotonic id, not the array-slot order a Placement happens to occupy, and resolution is per-layer: a Placement declines a cell — letting resolution continue downward — when its Patch has no binding for the layer, the world coordinate is outside its transformed Patch, or the authored cell is ASCII space `0x20`. The runtime source identity and geometry are pinned by D-35: one logical runtime overlay source per Map layer, identified by `(MapId, LayerId)`, covering the complete Map area with `P` = the Map area minimum and `D` = the Map area extent, world-oriented and independent of `CacheChunkSide`; `validate_runtime_layer_source()` in `src/world/runtime_layer_source.hpp` checks an already-parsed layout against the Map area with wide intermediates, no `Area::width()`/`height()`, and no narrowing of the source position.

### Placement creation and mutation

`place()` (both overloads) returns `MapPlacementResult = std::expected<PlacementId, MapPlacementError>`, where `MapPlacementError` is a scoped enum with `UnknownPatch`, `CapacityFull`, and `DirtyState`:

- `UnknownPatch`: the id does not resolve in the Patch catalogue; checked first, so a full Map or a dirty Cache never hides an invalid Patch id;
- `CapacityFull`: every fixed `MaxPlacements` slot is already occupied; checked before invalidation, so a capacity failure is reported without attempting any invalidation;
- `DirtyState`: the whole-cache invalidation would discard dirty resident state, so it was refused atomically (D-37).

The `PlacementId` is assigned only after the invalidation has succeeded and the Placement is constructed only after the id assignment, so a `DirtyState` failure creates no Placement and consumes no id. Successful creation otherwise assigns the next stable `PlacementId`, starting at 1 and increasing monotonically; ids are never reused. The natural overload places at the Patch's natural position with the identity orientation; the explicit overload uses the requested position and orientation.

Public lookup is const-only: `placement(PlacementId)` performs a bounded search and returns `const placement_type*`, `nullptr` for unknown ids. `set_position()`, `set_rotation()`, `set_reflection()`, and `set_orientation()` return `MapPlacementMutationResult = std::expected<void, MapPlacementMutationError>`: `UnknownPlacement` for unknown ids; the stored Placement is modified only when the requested value differs from the current one, and a no-op transform request needs no invalidation and succeeds even while dirty state exists. Any real change first invalidates resident Cache data — currently the whole Cache, because transformed Patch coverage does not exist yet (D-33) — and only after that dirty-safe invalidation succeeds is the Placement modified; a refused invalidation fails with `MapPlacementMutationError::DirtyState` and leaves the Placement unchanged.

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
tests/world/test_layer_source.cpp
tests/world/test_layer_fallback.cpp
tests/world/test_authored_layer_source.cpp
tests/world/test_placement.cpp
tests/world/test_map_placement.cpp
tests/world/test_map_mutation.cpp
tests/world/test_map_result.cpp
tests/world/test_map_value.cpp
tests/world/test_map_at.cpp
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

- `BUILD_GMOCK` is still forced off in CMake despite project policy allowing GoogleMock;
- no-exceptions/no-RTTI and the full warning policy are not yet fully enforced by CMake;
- scoped storage `Error` values are still snake_case rather than PascalCase;
- older geometry tests still live directly under `tests/`.

Treat these as separate reviewable changes.

## 20. Next implementation slice

The authored source file layout and Placement transform anchor are pinned, the source format now has a streaming parser/addressing helper (`src/world/layer_source.hpp`) with behavioural tests (`tests/world/test_layer_source.cpp`), the Placement/world ↔ Patch/source-local transform itself is implemented and tested (`tests/world/test_placement.cpp`), and the dense v1 Patch/source geometry contract is validated by `src/world/authored_layer_source.hpp` with behavioural tests (`tests/world/test_authored_layer_source.cpp`). The live Placement lifecycle is implemented in Map (`place()` with `MapPlacementResult`, const lookup, and `set_*` mutation; see section 11) and is covered by `tests/world/test_map_placement.cpp`.

Checked single-layer access `Map::value<LayerT>()` is now implemented and tested (`tests/world/test_map_value.cpp`): checked coordinate, chunk-plane resolution of authored Placements in descending `PlacementId` order (D-34), terminal fallback for the remaining in-Map cells, `Cache::fill<LayerT>()`, and exact propagation of lower-level source errors. Checked multi-layer access `Map::at()` is now also implemented and tested (`tests/world/test_map_at.cpp`): checked coordinate before any other work, multi-layer residency through the same chunk-plane seam in the Map template's declared layer order (not sorted by `LayerId`), per-layer precedence and space fall-through, fail-fast propagation of the exact first `MapError`, `CacheFull` propagation, invalidation on winning-Placement changes, and owned by-value `Tile` semantics. The terminal fallback contract is pinned in `src/world/layer_fallback.hpp` and is an explicit borrowed Map dependency; both `value<LayerT>()` and `at()` now query it as the last step of chunk-plane resolution. The runtime layer source identity/geometry contract is validated by `src/world/runtime_layer_source.hpp` and pinned by `tests/world/test_runtime_layer_source.cpp`; the Map runtime binding catalogue (empty/one-binding construction, the old constructor no longer being current, and the bound-source read contract) is pinned by `tests/world/test_map_runtime_catalogue.cpp` and the header tripwire. Runtime overlay read resolution (D-36) is implemented and pinned by `tests/world/test_map_runtime_overlay.cpp`: per-cell precedence over authored state and the fallback, space fall-through, mixed cells in one Chunk, a fully runtime-resolved Chunk skipping broken authored sources, exact `LayerSourceError`/`RuntimeLayerSourceError` propagation, missing-binding versus bound-but-broken behaviour, and residency of resolved Chunks.

The recommended order is:

1. fuse Map mutation, Cache mutation, dirty tracking and dirty-safe invalidation into one coherent change (the checked single-layer `value<LayerT>()` slice, the checked multi-layer `Map::at()`, the runtime overlay read resolution, and the first mutable world-state path `Map::set<LayerT>()` with per-plane Cache dirty tracking and dirty-safe invalidation are now implemented and tested, D-36 and D-37);
2. add the replacement policy only after the no-eviction path is proven (explicit per-layer write-back already exists as `Map::flush<LayerT>()`/`flush_all()`, D-38; replacement under dirty pressure and flush scheduling remain open);
3. introduce `Region` only when a simulation needs it.

Do not create placeholder objects merely to stand in for missing contracts.
