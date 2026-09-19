# Landor — Layer Storage Lifecycle

This document defines how Landor uses layer files over the lifetime of a running world.

The byte format itself is defined by `LAYER_SOURCE_FORMAT.md`. The important rule here is that **authored layer data and runtime layer data use the same `.layer` format, but they have different ownership and persistence roles**.

## Two source roles

Landor distinguishes two kinds of stored layer source.

### Authored sources

Authored sources are immutable inputs describing the authored state of a Patch layer.

They may live on read-only storage such as:

- a read-only filesystem;
- ROM or flash presented as immutable assets;
- packaged game data;
- another Storage backend that is not writable at runtime.

An authored source:

- uses the versioned `.layer` format from `LAYER_SOURCE_FORMAT.md`;
- is referenced by authored Patch metadata;
- may be shared by many Placements of the same Patch;
- is never modified by simulation or gameplay;
- is never a write-back target.

Authored state is therefore an input/initial condition, not mutable save-state storage.

### Runtime sources

Runtime sources persist mutable world state produced while the game is running.

They use the **same `.layer` file format** as authored sources so the same metadata, dense-grid addressing, validation, and byte encoding can be reused.

A runtime source:

- is writable;
- may live on a completely different filesystem, device, root, or Storage backend from authored data;
- records materialized changes to the live world;
- is the only persistent layer-file target for runtime write-back;
- participates in reads ahead of authored data when runtime state exists.

The architecture must not assume that authored and runtime sources are colocated or even writable through the same physical storage implementation.

## Cache is the live working state

Layer files are backing stores. They are **not** intended to be loaded as complete in-memory world images.

The live simulation/game state is the resident layer data held by the Cache. Simulations and gameplay operate on that resident state.

The normal flow is:

```text
cache hit
    -> use resident layer state directly

cache miss
    -> resolve runtime/authored/procedural source
    -> read only the required backing ranges
    -> populate the required Cache layer plane/Chunk
    -> continue using resident state
```

A `.layer` source may be much larger than available RAM. Nothing in the Map architecture may require a whole authored or runtime layer to be resident at once.

Only small source metadata needed for direct addressing — such as dimensions, natural position, `data_offset`, and row stride — needs to remain available after a source header has been parsed.

For runtime mutation, the direction is reversed:

```text
simulation/game changes resident state
    -> resident state becomes dirty
    -> later write-back updates the corresponding runtime-source ranges
```

The Cache therefore mediates simulation state; the files provide persistence/backing storage.

## Runtime state belongs to the live world

A Patch is reusable authored data. Several Placements may instantiate the same Patch.

Therefore runtime mutation must **not** silently become mutation of the Patch definition or of the Patch's authored source.

For example, if two Placements both use `GoodMagePalace`, a fire or terrain change in one Placement must not automatically change the other Placement merely because both originated from the same authored `.layer` file.

Runtime persistence must consequently identify the live world occurrence or world area whose state it represents.

The runtime identity is now pinned (see `DESIGN_DECISIONS.md`, D-35): a runtime source belongs to `(MapId, LayerId)` — one logical runtime overlay source per Map layer — and its geometry is the complete Map area (`P` = Map area minimum, `D` = Map area extent), with source-local `(0, 0)` equal to the Map's minimum world coordinate. The runtime overlay belongs to Map world coordinates, not to the authored object that originally supplied the value: if Fire at a world coordinate changes at runtime, persistence goes through that Map's Fire runtime source, never through `GoodMagePalace.Fire` authored source, and a second placement of the same Patch is unaffected. No Placement or Patch transform participates.

What remains deliberately unpinned is the physical side: the exact runtime filename/naming convention, the exact `SourceId` allocation/registration for that identity, and file creation. They should be defined when the runtime-source manager/write-back path is implemented.

The authored filename convention:

```text
<PatchName>.<LayerName>.layer
```

must not be reused blindly as a runtime identity scheme, because Patch name alone is insufficient to identify a Map layer.

## Read resolution

For a layer value that is not already resident as current state, stored resolution proceeds from most mutable to least mutable. This order is now implemented in checked Map reads (D-36):

```text
runtime/materialized source
    -> authored Placement contribution
    -> procedural/default contribution
```

A non-space runtime cell is authoritative over all authored Placements and the procedural/default fallback; a space runtime cell means "no runtime contribution" and resolution continues downward. The exact precedence among multiple authored Placements is a separate Map-resolution rule (D-34) and is not defined here.

Runtime state has priority because it represents changes already made to the live world.

## Meaning of space in runtime files

The generic `.layer` format reserves ASCII space (`0x20`) to mean:

```text
this source makes no contribution at this coordinate
```

That rule applies to runtime files as well as authored files.

Therefore a space in a runtime source means "no runtime override here" and resolution may continue to authored or procedural/default state.

It does **not** mean "force the world value to empty/zero". If a Layer needs an explicit semantic value meaning empty, absent, burned, dry, zero, etc., that value must have its own non-space Layer encoding.

This distinction lets a physically dense runtime file act as a logical overlay without copying every authored value into it.

## Runtime file creation

A runtime source does not have to exist before the world is modified.

A future implementation may create one when a layer first needs persistent runtime state. Because version 1.0 is physically dense, an initially empty runtime overlay can be represented by:

- normal `V`, `D`, `P`, and future metadata;
- the normal blank-line metadata terminator;
- a dense grid filled with spaces except where runtime contributions exist.

The exact creation policy, allocation API, naming, and source registration mechanism are implementation work and are intentionally left open.

## Dirty state and write-back

Mutable resident layer data will eventually need dirty tracking.

The intended direction is:

```text
simulation/game mutation
    -> current resident layer state becomes dirty
    -> dirty state remains authoritative while resident
    -> write-back persists it to a runtime source
    -> authored source remains untouched
```

Write-back should operate on the required runtime-source ranges rather than requiring the complete layer to be assembled or rewritten in memory.

Eviction must not discard dirty state. Before a dirty layer region can be evicted, its required runtime state must be safely persisted or eviction must fail/defer.

The exact granularity of dirty tracking and write-back — cell, range, Chunk, layer plane, or another measured choice — is not pinned yet.

## Storage separation

The Storage abstraction remains responsible only for byte I/O. It must not acquire Patch, Placement, simulation, or layer-resolution semantics.

Map-side persistence must, however, be able to reach both roles:

```text
Authored Storage
    read-only is valid

Runtime Storage
    writable
```

These may happen to be the same backend or physical device on a host build, but no design may require that.

The `Map` source contract now borrows two explicit storage roles and the runtime layer binding catalogue:

```cpp
const storage::Storage&                authored storage
storage::Storage&                      runtime storage
std::span<const RuntimeLayerBinding>   runtime layers
```

Resolution is role-separated: authored reads (`open_layer_source()` and `read_cells()` for Placement sources inside `Map::resolve_chunk<LayerT>()`) receive the authored reference, and runtime overlay reads receive the runtime reference, so each role is only ever touched for its own sources. Map never writes to either role in the current slice: runtime sources are read-only from Map's perspective until write-back exists. Runtime overlay read resolution is implemented (D-36): for one missing layer Chunk the bound runtime source is opened and validated exactly once, then unresolved in-Map cells are read one at a time; when no binding names the layer the runtime storage is never inspected. Runtime `SourceId` allocation/registration and write-back do not exist yet; the `SourceId` a runtime source carries is supplied by the binding catalogue.

The binding catalogue drives read resolution: a binding `{ layer, source }` names the logical runtime source of `(Map::id(), layer)` in the runtime storage role, and the Map constructor asserts that every binding names a layer supported by the Map type and that no `LayerId` appears twice (D-35). Checked reads consult it through the private `Map::runtime_binding<LayerT>()` seam; no write path consults it yet.

Both references currently use the build-selected `storage::Storage` type. That is current source reality, not a permanent architectural prohibition against heterogeneous authored/runtime backing implementations on a concrete target.

The same physical `Storage` object may fill both roles. The requirement is that Map carries distinct semantic references for the two roles, not that the objects be different.

## Same format, different lifecycle

Using one file format for both roles is intentional:

```text
authored .layer       runtime .layer
      |                     |
      +---- same parser ----+
      +---- same layout ----+
      +---- same metadata --+
      +---- same addressing +
```

What differs is not the encoding but the lifecycle:

```text
authored: create/edit offline -> ship -> read only
runtime:  create/materialize while running -> mutate -> write back -> reload later
```

In both cases, normal gameplay access is incremental. A source header is parsed to establish direct addressing, then Cache fills and runtime write-back use bounded byte ranges. Whole-file loading is not part of the runtime model.

## Deliberately deferred

This document does not yet define:

- runtime source filenames;
- runtime `SourceId` allocation/registration;
- runtime file creation API;
- dirty-state granularity;
- write-back scheduling;
- crash consistency or atomic replacement policy;
- cache eviction policy;
- procedural/default fallback API.

Those are implementation decisions to make with concrete Map persistence requirements. The contract already fixed is simpler: **authored files are immutable; runtime files are writable overlays/materialized state; both use the same `.layer` format; runtime storage may be physically separate from authored storage; and normal gameplay accesses both incrementally through the Cache rather than loading whole files into memory.**
