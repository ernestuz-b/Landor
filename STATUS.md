# Landor — Project Status

This file describes **what exists now**, not the final architecture.

Source baseline reviewed before this update:

```text
08072f8c05a53b34a564a0b9d627b6f1b1c52015
world: resolve runtime layer overlays
```

For intended architecture, read `dev-docs/DESIGN_STATE.md`,
`dev-docs/DESIGN_DECISIONS.md`, and, for mapping specifically,
`dev-docs/MAPPING_MODEL.md`.

The authored layer file contract is defined in:

```text
dev-docs/LAYER_SOURCE_FORMAT.md
```

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

That value is deliberate: the installed clang-format 20.1.2 does not accept
`Standard: c++23`, so the formatter keeps parsing as C++20 while CMake remains
authoritative for C++23 compilation.

### Tests

The test target currently includes:

```text
tests/test_world_headers.cpp
tests/test_area.cpp
tests/test_coord.cpp

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
tests/world/test_map_storage_roles.cpp
tests/world/test_runtime_layer_source.cpp
tests/world/test_map_runtime_catalogue.cpp
tests/world/test_map_runtime_overlay.cpp

tests/platform/storage/test_storage_filesystem.cpp
```

`Tile`, `Chunk`, `Cache`, the streaming layer source reader, the authored Patch/source geometry contract, the Placement coordinate transform, the live Placement lifecycle on Map, filesystem storage, and the checked Map point access (`Map::value<LayerT>()` and `Map::at()`) now have behavioural GoogleTests. The terminal layer fallback contract and the checked Map access result contract keep their compile-time assertions in `tests/world/test_layer_fallback.cpp`, `tests/world/test_map_result.cpp` and the header tripwire, and are now additionally exercised behaviourally through `value<LayerT>()` in `tests/world/test_map_value.cpp` and `at()` in `tests/world/test_map_at.cpp`. The two explicit Map storage roles are pinned by focused behavioural tests in `tests/world/test_map_storage_roles.cpp` (authored resolution uses only the authored root, a missing runtime source is not an error while no binding names the layer, and one Storage object may fill both roles) and by the constructor tripwire in `tests/test_world_headers.cpp`. The runtime layer source identity/geometry contract (one logical runtime overlay source per Map layer, source geometry pinned to the complete Map area, defensive narrow-coordinate comparisons) is pinned by `tests/world/test_runtime_layer_source.cpp`, and the Map runtime binding catalogue (empty and one-binding construction, the old constructor no longer being current, a bound runtime cell overriding authored state, and a bound-but-absent runtime source failing the access) is pinned by `tests/world/test_map_runtime_catalogue.cpp`. The implemented runtime overlay read resolution (D-36) is pinned by `tests/world/test_map_runtime_overlay.cpp`: per-cell precedence over authored state and the fallback, space fall-through, mixed cells coexisting in one Chunk, a fully runtime-resolved Chunk skipping broken authored sources, exact `LayerSourceError`/`RuntimeLayerSourceError` propagation, missing-binding versus bound-but-broken behaviour, and residency of resolved Chunks. The first mutable world-state path (`Map::set<LayerT>()` plus Cache dirty tracking and dirty-safe invalidation, D-37) is pinned by `tests/world/test_map_mutation.cpp`: fallback, runtime and authored backed mutations, the dirty resident value being authoritative for subsequent reads without rereading, same-value sets creating no dirty state, bounds checks before resolution work, exact source-error propagation without creating dirty state, and `CacheFull` when a mutation would need a new spatial slot; the dirty-behaviour contract itself is pinned by the extended `tests/world/test_cache.cpp` (per-plane dirty flags, same-value no-op, fill refusal over dirty planes, atomic dirty-safe area and full invalidation), and the dirty-safe placement lifecycle by the extended `tests/world/test_map_placement.cpp` (`DirtyState` creation and transform rejections, UnknownPatch/CapacityFull ordering, no-op transforms succeeding while dirty).

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
    layer_source.hpp
    layer_fallback.hpp
    authored_layer_source.hpp
    runtime_layer_source.hpp
    patch.hpp
    placement.hpp
    patchset.hpp
    place.hpp
    map.hpp
    map_result.hpp
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
- fixed-capacity layer-plane fills (refused when the target plane is already dirty);
- resident layer reads;
- resident layer mutation with per-plane dirty marking (`set<LayerT>()`);
- dirty queries (`dirty<LayerT>()`, `has_dirty()`);
- Tile packing on presentation;
- dirty-safe area invalidation; an intersecting dirty slot makes the whole operation fail atomically, returning `false` and changing nothing;
- dirty-safe full invalidation; any dirty resident plane refuses it.

Dirty state is per resident layer plane per spatial slot, not per cell: a changed value dirties the whole plane, a same-value set does not dirty a clean plane, and once dirty a plane stays dirty — there is no flush, clear or write-back API (D-37). The Cache still has no replacement or eviction policy: when every spatial slot is occupied, filling a new spatial area fails rather than inventing an eviction policy.

### `landor::geo::Map`

Map is the logical spatial surface and owns live Placements.

Its current header is aligned to the new Cache model:

- supported Layers remain part of the Map type;
- Cache remains layer-oriented;
- `Map::at()` is implemented: checked multi-layer world access returning `MapResult<tile_type>` — bounds check first, every supported layer resident in the Map template's declared layer order (fail-fast, exact first error), then Tile packing through `Cache::tile()`; the scalar overload `at(x, y)` forwards to `at(coord_type{x, y})`;
- `Map::value<LayerT>()` is implemented: layer-specific checked access returning `MapResult<LayerT::value_type>` — bounds check first, canonical chunk residency, chunk-plane resolution (runtime overlay when a binding names the layer, then authored Placements in descending `PlacementId` order, then the terminal fallback), and installation through `Cache::fill<LayerT>()`;
- the live Placement lifecycle is implemented: `place()` (both overloads) returns `MapPlacementResult = std::expected<PlacementId, MapPlacementError>`; unknown Patch is checked before capacity and dirty state; the whole-cache invalidation is performed before the `PlacementId` is assigned and the Placement constructed, so a refused invalidation fails with `MapPlacementError::DirtyState` without creating a Placement or consuming an id; otherwise successful creation assigns the next stable `PlacementId`, starting at 1 and increasing monotonically, never reused;
- `Map::set<LayerT>(position, value)` is implemented as the first mutable world-state path: bounds check first, then `ensure_resident<LayerT>()` installs a clean plane when the chunk is missing, and `Cache::set<LayerT>()` mutates the resident value and marks the plane dirty only when the value actually changes; the dirty resident value is authoritative for subsequent reads of that Chunk; failure carries the exact `MapError` (`OutOfBounds`, the exact lower-level source error, or `CacheFull`), and dirty state is never a `MapError`;
- public lookup is const-only (`placement(PlacementId)` returns a bounded pointer, `nullptr` for unknown ids); `set_position()`, `set_rotation()`, `set_reflection()`, and `set_orientation()` return `MapPlacementMutationResult = std::expected<void, MapPlacementMutationError>`, reject unknown ids with `UnknownPlacement`, and mutate only when the requested value actually differs from the current one; a no-op transform request needs no invalidation and succeeds even while dirty state exists;
- a placement change that alters the world composition currently invalidates the whole resident Cache before mutating the Placement, because transformed Patch coverage does not exist yet (D-33); the invalidation is dirty-safe (D-37) and a refusal surfaces as `MapPlacementMutationError::DirtyState` with the Placement left unchanged;
- Map borrows two explicit storage roles: `const landor::storage::Storage&` for the authored read source (immutable; Map never writes it) and `landor::storage::Storage&` for the runtime overlay read source (writable type so future materialisation/write-back can write it; read-only from Map's perspective until then); both are the build-selected `landor::storage::Storage` alias (currently `StorageFilesystem`), both are borrowed and outlive the Map, and the same Storage object may legitimately fill both roles;
- Map also borrows the runtime layer binding catalogue: `std::span<const RuntimeLayerBinding>` scoped to this Map instance, where a binding `{ layer, source }` identifies the logical runtime/materialised overlay source of `(Map::id(), layer)` in the runtime storage role (see `src/world/runtime_layer_source.hpp`, D-35); the constructor asserts that every binding names a layer supported by the Map type and that no LayerId appears twice in the span — a duplicate is invalid configuration, not a precedence case; zero bindings means this Map currently has no persistent runtime overlay source for those layers, not that the layer is unsupported;
- authored resolution (`open_layer_source()`/`read_cells()` inside `resolve_chunk<LayerT>()` for Placement sources) uses the authored storage only, and runtime overlay resolution (D-36) uses the runtime storage only: when a runtime binding names the layer, the bound source is opened and validated once per missing layer Chunk and its non-space cells resolve their cells ahead of authored state; when no binding names the layer the runtime storage is never inspected; no write path exists yet;
- Map carries an explicit typed terminal fallback dependency: a `FallbackT` template parameter constrained by `LayerFallbackProvider<FallbackT, CoordT, Layers...>` (see `src/world/layer_fallback.hpp`). The provider is queried per layer and per world coordinate and returns exactly `LayerT::value_type`; it represents procedural baseline generation or a fixed layer default behind one small operation. Map borrows one provider object as `const` and owns it not, and exposes no accessor for it. `value<LayerT>()` and `at()` query the provider per layer and per unresolved in-Map cell during chunk-plane resolution, behind both the runtime overlay (D-36) and the authored Placement chain (D-34).

The checked Map access result/error contract is now pinned in `src/world/map_result.hpp`: `Map::at()` returns `MapResult<tile_type>` and `Map::value<LayerT>()` returns `MapResult<LayerT::value_type>`, with `MapError = std::variant<MapErrorCode, LayerSourceError, AuthoredLayerSourceError, RuntimeLayerSourceError>`. Map-local failures are `OutOfBounds` (the coordinate is outside the Map, detected before Cache/Storage work) and `CacheFull` (the bounded Cache cannot accept another spatial slot and has no eviction policy yet); source failures keep their exact lower-level domains: the exact `LayerSourceError` for `.layer` parser/read failures in any source role, the exact `AuthoredLayerSourceError` for authored Patch/source geometry failures, and the exact `RuntimeLayerSourceError` for runtime Map/source geometry failures (D-36). `storage::Error` never surfaces directly; the layer source reader already maps Storage failures to `LayerSourceError::StorageFailed`.

The single-layer checked access path is now implemented: `value<LayerT>()` ensures canonical chunk residency by resolving a complete layer plane — first, when a runtime binding names the layer, opening the bound runtime source once and validating it once against the complete Map area and reading the unresolved in-Map cells from it (non-space resolves the cell, space leaves it unresolved); then opening each relevant authored source once per Placement, validating it once against the Patch, and reading the relevant cells through the streaming reader; and finally falling back for the remaining in-Map cells — and installs the plane through `Cache::fill<LayerT>()`. Authored precedence is pinned: a higher `PlacementId` has higher precedence, so a later successful placement overlays an earlier one (see `dev-docs/DESIGN_DECISIONS.md`, D-34). The checked multi-layer access `Map::at()` is now implemented on the same seam: the bounds check happens before any Cache population, Storage read, authored-source opening or fallback call; the non-template `ensure_resident()` then walks the supported layers in the Map template's declared order (not sorted by `LayerId`), resolves only the missing ones, and fails fast, returning the exact first `MapError` unchanged; on success `Cache::tile()` packs the owned `Tile` value.

### Runtime layer sources

The logical identity of a runtime/materialised `.layer` source is
`(MapId, LayerId)`: one Map has at most one logical runtime overlay source per
layer, and that source covers the Map's complete logical `Area`
(`src/world/runtime_layer_source.hpp`, D-35). Its geometry is pinned to the
Map's world area:

```text
P = map.area().min()
D = map.area().max() - map.area().min() + 1
```

so source-local `(0, 0)` is the Map's minimum world coordinate and
`runtime local = world - map.area().min()`. No Placement or Patch transform
participates: runtime state is world-oriented, two Placements sharing a Patch
remain independent in world state, and persistence identity is independent of
`CacheChunkSide`. The Cache remains temporary residency, not persistence
identity.

The contract pins the logical source only: the source may be much larger than
RAM and never has to be loaded, rewritten or materialised in whole, and
Storage may represent it however its backend requires. No filename,
SourceId allocation/registration or creation policy is defined yet.
`validate_runtime_layer_source()` checks an already-parsed
`LayerSourceLayout` against the Map area with the same defensive numeric style
as the authored validator: wide signed intermediates, no
`Area::width()`/`height()`, and no narrowing of the source `P` into the Map's
coordinate type first.

Map carries the catalogue as a borrowed `std::span<const RuntimeLayerBinding>`
constructor parameter with the supported/unique-layer precondition asserted in
the constructor. Checked reads now consult the catalogue through the private
`Map::runtime_binding<LayerT>()` seam (D-36): a non-space runtime cell is the
authoritative current world value for its cell and outranks every authored
Placement and the fallback; a space runtime cell (`0x20`) contributes nothing,
so the cell continues to authored Placements and then to the fallback. A
missing binding is normal absence (no runtime overlay for that layer);
a present binding whose source cannot be opened, parsed or validated is a
checked-access failure that propagates the exact `LayerSourceError` or
`RuntimeLayerSourceError` instead of falling through. For one missing layer
Chunk the bound runtime source is opened and validated exactly once, before
any per-cell reading, and when the runtime overlay resolves every logical
cell of the Chunk no authored source is opened and the fallback is never
queried. No write path exists yet: runtime sources remain read-only from Map's
perspective and no write-back is implemented. Mutable state exists only as
dirty resident Cache planes (D-37): `Map::set<LayerT>()` marks a resident
layer plane dirty, the dirty value is authoritative for subsequent reads of
that Chunk, and dirty-safe invalidation refuses to discard it.

## Authored-world contracts

### `landor::geo::Patch`

An immutable reusable authored region containing identity, authored geometry, and zero or more `LayerBinding { LayerId, storage::SourceId }` entries.

Patch-local `(0, 0)` is the authored transform/source origin. Dense v1 authored source coordinates begin at that same origin; `D` gives the local rectangle size and `P` agrees with `Patch::natural_position()`, the world coordinate of that origin at natural placement.

That agreement is now enforced, not only documented: `validate_authored_layer_source()` in `src/world/authored_layer_source.hpp` checks an already-parsed `LayerSourceLayout` against the Patch and rejects an empty or non-zero-origin local area (`InvalidPatchGeometry`), a `D`/local-extent disagreement (`DimensionMismatch`), and a `P`/`natural_position()` disagreement (`PositionMismatch`). Extent and position are compared in 64-bit intermediates, so narrow `Coord8`/`Coord16` Patches compare correctly against the `Coord32` source position without narrowing or wrap. The check deliberately lives at this compatibility seam, not in the `Patch` constructor: `Patch` remains unconstrained authored metadata.

Missing authored data for a layer is represented by absence of a binding, not by unsupported capability.

### Authored layer source files

`dev-docs/LAYER_SOURCE_FORMAT.md` now pins the initial on-disk source format.

Version 1.0 uses:

- filename convention `<PatchName>.<LayerName>.layer`;
- LF-only (`0x0A`) line endings;
- line-oriented metadata terminated by the first empty line (`0x0A 0x0A`);
- core `V`, `D`, and `P` metadata records for version, dimensions, and natural position;
- decimal or `0x`-prefixed hexadecimal integer metadata;
- dense row-major one-byte cells after the metadata block;
- exactly `width` cell bytes plus one LF per row, for exactly `height` rows;
- ASCII space (`0x20`) as “no authored contribution” rather than runtime zero;
- direct Patch-local addressing through `data_offset + y * (width + 1) + x`.

Metadata remains extensible; future layer-specific records are explicitly possible but not defined yet. Sparse coordinate-record data is also deferred rather than included in version 1.0.

A minimal streaming reader now implements this contract in `src/world/layer_source.hpp`: bounded incremental metadata parsing, direct cell addressing, and bounded single-row cell reads through the Storage contract. Non-empty reads also validate that requested cell bytes contain no structural LF and that the touched row ends with LF, without scanning untouched rows. Opening never loads or copies the complete source, and whole-file structural scanning is not performed. Cache/Map integration exists for reads; runtime write-back is still pending.

The generic reader stays Patch-unaware. The dense v1 Patch/source geometry contract is validated by the separate bridge header `src/world/authored_layer_source.hpp`, which takes an already-parsed layout, performs no Storage I/O, and carries no source-identity semantics (no LayerId/SourceId/backend/share concerns).

### `landor::geo::Placement`

One live occurrence of a Patch with its own identity, position, and Orientation.

`Placement::position()` is the Map coordinate of Patch-local `(0, 0)`. Reflection and rotation happen about that origin, followed by translation; transformed bounds are not renormalised around a new top-left.

Transform order remains:

```text
reflection -> rotation -> translation
```

That contract is now implemented pointwise as `local_to_world()` and `world_to_local()`, both returning `std::optional<coord_type>`. Reflection, rotation, and translation/subtraction run in a wide signed intermediate, so narrow coordinate types cannot overflow; a result that no longer fits in the coordinate type is reported as `nullopt` instead of wrapping. No `Patch` object is required, and Patch-local-area checking stays a separate `patch.local_area().contains(local)` step for Map. The anchor, the order, and the round trips over all 16 orientations are pinned by `tests/world/test_placement.cpp`.

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

- cache replacement policy;
- dirty write-back/flush policy (dirty state itself is implemented per D-37);
- write-back timing;
- procedural-state materialisation policy;
- per-Placement transformed coverage for finer Cache invalidation (current placement mutations invalidate the whole resident Cache);
- broader mutation APIs between simulations and live layer state (single-cell `Map::set<LayerT>()` is implemented);
- `Region` ownership/view/mutation semantics.

## Near-term implementation sequence

A sensible next sequence from the current tree is:

1. fuse Map mutation, Cache mutation, dirty tracking and dirty-safe invalidation into one coherent change (the checked single-layer `value<LayerT>()` slice, the checked multi-layer `Map::at()`, the runtime overlay read resolution, and the first mutable world-state path `Map::set<LayerT>()` with per-plane dirty tracking and dirty-safe invalidation are now implemented and tested, D-36 and D-37);
2. add replacement/write-back policy only after the fixed-capacity no-eviction path is working;
3. introduce `Region` only when a simulation needs an algorithmic working-area API;
4. separately finish mechanical policy alignment in CMake, enum spelling, and test layout.

Keep each step small and independently testable.
