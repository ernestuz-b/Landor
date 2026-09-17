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

The exact runtime naming convention and the exact mapping from live world state to `storage::SourceId` are deliberately not pinned yet. They should be defined when the runtime-source manager/write-back path is implemented.

The authored filename convention:

```text
<PatchName>.<LayerName>.layer
```

must not be reused blindly as a runtime identity scheme, because Patch name alone is insufficient to distinguish multiple Placements.

## Read resolution

For a layer value that is not already resident as current state, stored resolution conceptually proceeds from most mutable to least mutable:

```text
runtime/materialized source
    -> authored Placement contribution
    -> procedural/default contribution
```

The exact precedence among multiple authored Placements is a separate Map-resolution rule and is not defined here.

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

The current `Map` source contract borrows one selected `Storage` object. That is not yet sufficient to express independently located authored and runtime persistence. The implementation must gain an explicit seam for the two roles rather than assuming one filesystem root.

The exact API shape is deferred until the runtime reader/writer slice is implemented.

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
- whether runtime sources are per Placement, per world area, per save slot, or another concrete partitioning;
- runtime file creation API;
- dirty-state granularity;
- write-back scheduling;
- crash consistency or atomic replacement policy;
- cache eviction policy;
- exact authored Placement precedence;
- procedural/default fallback API.

Those are implementation decisions to make with concrete Map persistence requirements. The contract already fixed is simpler: **authored files are immutable; runtime files are writable overlays/materialized state; both use the same `.layer` format; runtime storage may be physically separate from authored storage; and normal gameplay accesses both incrementally through the Cache rather than loading whole files into memory.**
