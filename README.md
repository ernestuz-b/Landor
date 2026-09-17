# Landor

> A small game built to carry a surprisingly large world.

Landor is an open-source exploration, simulation and learning game written in modern C++.

The game is being designed from the world model outward. Terrain, water, fire, weather, plants, animals, people, ownership, schedules, settlements and other systems are intended to interact through simulation rather than through a collection of scripted special cases. The story and game mechanics provide a reason to explore those systems.

The first renderer is deliberately simple: UTF-8/text. Later renderers may be graphical, including isometric or small 3D presentations, without replacing the underlying world model.

Landor is also a C++ engineering project. It deliberately targets both Linux-class machines and small embedded systems such as Raspberry Pi Pico and STM32 devices. The architecture therefore favours explicit ownership, bounded memory, compile-time structure, determinism and platform seams that remain visible in the code.

## Current state

Landor is moving from architecture/contracts into the first mapping and storage implementation slices.

The repository currently contains:

- the managed heap component used when genuinely dynamic allocation is unavoidable;
- geometry primitives and GoogleTests;
- the layer-oriented world model;
- compact positional `Tile` values;
- aligned one-layer `Chunk` geometry;
- a fixed-capacity, layer-oriented resident `Cache` core with tests;
- reusable `Patch`, live `Placement`, story-facing `Place` and `PatchSet` concepts;
- a `Map` contract aligned to the layer-oriented Cache model;
- the platform-independent Storage contract;
- a filesystem-backed Storage implementation with tested `read`, `write`, and `size` operations;
- GoogleTest-based contract and behavioural tests.

The game itself is not yet a playable release. Old text about shards, a chest, a key and a beacon describes an early prototype fixture, not the current definition of Landor.

## Language and targets

Landor is a **C++23** project.

The supported portable subset is the subset exercised by the supported toolchains and CI. A C++23 facility is not assumed to be portable merely because one desktop compiler implements it.

Primary target classes are:

- Linux desktop/workstation;
- Raspberry Pi Zero-class Linux systems;
- Raspberry Pi Pico/RP2040-class microcontrollers;
- STM32-class microcontrollers.

Ports may differ in ambition, rendering and world scale. They should share formats, architecture and engineering contracts where practical; they are not required to be bit-for-bit identical games.

Landor runtime code has no required third-party runtime framework. Development and tests may use external tools such as GoogleTest/GoogleMock.

## World model in one page

### Layers, not stored tiles

World state is layer-oriented. Terrain, elevation, water, fire, moisture, wind and future properties are independent layer kinds.

A `Layer` is a type-level description of one spatial property. It defines a stable layer id and a value type; it does not own storage, caching or simulation policy.

A `Tile<CoordT, Layers...>` is a small value assembled when somebody asks what exists at one coordinate. It owns the coordinate and copies of the layer values. There is no authoritative array of complete `Tile` objects.

This distinction matters. A Patch that contains no authored Fire layer does **not** mean fire cannot exist there. Fire may come from another contribution, a default, procedural generation or live simulation state.

### Cache and Chunk

Resident world state also remains layer-oriented.

A `Chunk` is an aligned square portion of one layer used by the Cache/I/O machinery.

The Cache owns a bounded number of canonical spatial slots. Within each slot, layer planes are independently resident. It does **not** store the world as an array of complete Tiles.

When `Map::at(position)` is eventually fully implemented, Map ensures the required layers are resident and the Cache packs one value from each layer at that coordinate into a fresh Tile.

The current Cache deliberately has no eviction/write-back policy. When all spatial slots are occupied, a fill for a new spatial area fails rather than silently inventing replacement semantics.

### Patch, Placement, PatchSet and Place

An authored `Patch` is immutable reusable spatial content. It has geometry and zero or more layer bindings to logical Storage sources.

A `Placement` is one live occurrence of a Patch in a Map. It gives the Patch a position and orientation. Several Placements may refer to the same authored Patch.

A `PatchSet` is an immutable recipe used to compose groups of Patches, such as a home made from a house, well, barn, fields and a unique feature.

A `Place` is a story-facing identity. Schedules, ownership, dialogue and missions should refer to a Place rather than to the authored Patch used to implement it.

### Map

`Map` is the logical spatial surface.

It owns live Placements, resolves each layer independently, owns/integrates the layer-oriented Cache, and returns compact Tile values to callers.

Authored data, procedural/default values and materialized simulation state can therefore coexist at one coordinate without forcing them into one stored Tile structure.

Public Map access is intended to be checked. Cache and Chunk remain internal mapping/I/O concepts rather than game-side APIs.

For the detailed mapping model, read [`dev-docs/MAPPING_MODEL.md`](dev-docs/MAPPING_MODEL.md).

## Storage

World code addresses Storage through logical `SourceId`, byte offset and size values. It must not infer paths, sectors, pages, erase blocks or file handles from a source id.

The concrete Storage implementation is selected at **build time**. Common code is intended to use the selected `landor::storage::Storage` alias.

The filesystem backend currently implements and tests:

- complete bounded reads;
- bounded in-place byte replacement writes;
- logical source size queries.

The intended platform-selection rule is simple:

- implementation known when the target is built -> CMake selects the implementation;
- variation represented by C++ types -> templates/concepts;
- implementation genuinely unknown until runtime -> runtime polymorphism.

Do not introduce virtual dispatch merely to abstract something the build already knows.

## Memory

Prefer fixed-capacity and value storage whenever a useful upper bound exists.

If a capacity affects object layout or type identity, it is a compile-time parameter. When a type has several related capacities, prefer a named structural policy once that is clearer than a list of integer template arguments.

The managed heap under `include/managed_heap/` comes from another project and is used when dynamic allocation is genuinely unavoidable. Treat it as an imported component: use its public contract and do not modify it casually as part of unrelated Landor work.

Allocating standard containers are not the default in core/runtime code. They may be used only when their allocation semantics are explicitly suitable for the target and allocator. The relocatable managed heap is not automatically a valid STL allocator.

## Build and tests

The normal build directory is:

```text
transient/build
```

Typical host build:

```bash
cmake -S . -B transient/build
cmake --build transient/build -j
ctest --test-dir transient/build --output-on-failure
```

Tests use GoogleTest. GoogleMock is allowed by project policy where interaction testing is clearer than a concrete fake, although CMake still needs to stop forcing its build off.

Tests should mirror the source tree:

```text
src/world/cache.hpp
tests/world/test_cache.cpp
```

The newer mapping/platform tests already follow that pattern; a few older geometry tests still live at the test root. See `STATUS.md`.

## Documentation

Read documentation in this order when modifying the repository:

1. [`STATUS.md`](STATUS.md) — what exists now and what is currently inconsistent;
2. the relevant source headers and tests;
3. [`dev-docs/DESIGN_STATE.md`](dev-docs/DESIGN_STATE.md) — current architecture;
4. [`dev-docs/DESIGN_DECISIONS.md`](dev-docs/DESIGN_DECISIONS.md) — important settled choices and why;
5. [`dev-docs/MAPPING_MODEL.md`](dev-docs/MAPPING_MODEL.md) — detailed Map/Tile/Region/Cache/Chunk model;
6. [`dev-docs/IMPLEMENTATION.md`](dev-docs/IMPLEMENTATION.md) — how the current contracts fit together;
7. [`dev-docs/coding_rules.md`](dev-docs/coding_rules.md) — engineering rules;
8. [`dev-docs/coding_style.md`](dev-docs/coding_style.md) — formatting, naming and code-reading conventions;
9. [`AGENTS.md`](AGENTS.md) — operational rules for automated coding agents.

Headers are also part of the architecture documentation. Public types should explain ownership, lifetime, invariants, extension seams and non-obvious rationale.

If source and documentation disagree about what is currently implemented, source and tests win. Do not silently preserve stale prose: report the mismatch and update the relevant documentation when the architectural contract changes.

## License

The repository contains the **GNU General Public License, version 2**. See [`LICENSE`](LICENSE).
