# Landor

> A small game built to carry a surprisingly large world.

Landor is an open-source exploration, simulation and learning game written in modern C++.

The game is being designed from the world model outward. Terrain, water, fire, weather,
plants, animals, people, ownership, schedules, settlements and other systems are intended
to interact through simulation rather than through a collection of scripted special cases.
The story and game mechanics provide a reason to explore those systems.

The first renderer is deliberately simple: UTF-8/text. Later renderers may be graphical,
including isometric or small 3D presentations, without replacing the underlying world
model.

Landor is also a C++ engineering project. It deliberately targets both Linux-class
machines and small embedded systems such as Raspberry Pi Pico and STM32 devices. The
architecture therefore favours explicit ownership, bounded memory, compile-time structure,
determinism and platform seams that remain visible in the code.

## Current state

Landor is in an architecture-and-contract phase.

The repository currently contains:

- the managed heap component used when genuinely dynamic allocation is unavoidable;
- geometry primitives and tests;
- the layer-oriented world model contracts;
- reusable `Patch`, live `Placement`, story-facing `Place` and `PatchSet` concepts;
- the `Map` contract that synthesizes complete tile values from independent layers;
- the first platform-independent storage contract and a filesystem-backed implementation contract;
- GoogleTest-based contract and behavioural tests.

The game itself is not yet a playable release. Old README text describing shards, a chest,
a key and a beacon was an early fixture, not the definition of Landor.

## Language and targets

Landor is a **C++23** project.

The supported portable subset is the subset exercised by the supported toolchains and CI.
A C++23 facility is not assumed to be portable merely because one desktop compiler
implements it.

Primary target classes are:

- Linux desktop/workstation;
- Raspberry Pi Zero-class Linux systems;
- Raspberry Pi Pico/RP2040-class microcontrollers;
- STM32-class microcontrollers.

Ports may differ in ambition, rendering and world scale. They should share formats,
architecture and engineering contracts where practical; they are not required to be
bit-for-bit identical games.

Landor runtime code has no required third-party runtime framework. Development and tests
may use external tools such as GoogleTest/GoogleMock.

## World model in one page

### Layers, not stored tiles

World state is layer-oriented. Terrain, elevation, water, fire, moisture, wind and future
properties are independent layer kinds.

A `Layer` is a type-level description of one spatial property. It defines a stable layer
id and a value type; it does not own storage or simulation policy.

A `Tile` is a short-lived value assembled when somebody asks what exists at one
coordinate. It owns copies of the resolved layer values. There is no authoritative array
of complete `Tile` objects.

This distinction matters. A patch that contains no authored Fire layer does **not** mean
fire cannot exist there. Fire may come from a default, procedural generation or live
simulation state.

### Patch, Placement, PatchSet and Place

An authored `Patch` is immutable reusable spatial content. It has geometry and zero or
more layer bindings to logical storage sources.

A `Placement` is one live occurrence of a patch in a map. It gives the patch a position
and orientation. Several placements may refer to the same authored patch.

A `PatchSet` is an immutable recipe used to compose groups of patches, such as a home
made from a house, well, barn, fields and a unique feature.

A `Place` is a story-facing identity. Schedules, ownership, dialogue and missions should
refer to a place rather than to the authored patch used to draw it.

### Map

`Map` is the logical spatial surface.

It owns live placements, resolves each layer independently, and synthesizes `Tile` values.
Authored data, procedural/default values and materialized simulation state can therefore
coexist at one coordinate without forcing them into one stored structure.

A synthetic tile cache is permitted as a disposable optimization. It is never
authoritative state.

## Storage

World code addresses storage through logical `SourceId`, byte offset and size values. It
must not infer paths, sectors, pages, erase blocks or file handles from a source id.

The concrete storage implementation is selected at **build time**. Common code uses the
selected `landor::storage::Storage` alias.

The intended rule is simple:

- implementation known when the target is built -> CMake selects the implementation;
- variation represented by C++ types -> templates/concepts;
- implementation genuinely unknown until runtime -> runtime polymorphism.

Do not introduce virtual dispatch merely to abstract something the build already knows.

## Memory

Prefer fixed-capacity and value storage whenever a useful upper bound exists.

If a capacity affects object layout or type identity, it is a compile-time parameter.
When a type has several related capacities, prefer a named structural policy over an
unreadable list of integer template arguments.

The managed heap under `include/managed_heap/` comes from another project and is used when
dynamic allocation is genuinely unavoidable. Treat it as an imported component: use its
public contract and do not modify it casually as part of unrelated Landor work.

Allocating standard containers are not the default in core/runtime code. They may be used
only when their allocation semantics are explicitly suitable for the target and allocator.
The relocatable managed heap is not automatically a valid STL allocator.

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

Tests use GoogleTest. GoogleMock is also allowed where interaction testing is clearer than
a concrete fake.

Tests should mirror the source tree:

```text
src/world/coord.hpp
tests/world/test_coord.cpp
```

The current tree predates that final rule in a few places; see `STATUS.md`.

## Documentation

Read documentation in this order when modifying the repository:

1. [`STATUS.md`](STATUS.md) — what exists now and what is currently inconsistent;
2. [`dev-docs/DESIGN_STATE.md`](dev-docs/DESIGN_STATE.md) — current architecture;
3. [`dev-docs/DESIGN_DECISIONS.md`](dev-docs/DESIGN_DECISIONS.md) — important settled choices and why;
4. [`dev-docs/IMPLEMENTATION.md`](dev-docs/IMPLEMENTATION.md) — how the current contracts fit together;
5. [`dev-docs/coding_rules.md`](dev-docs/coding_rules.md) — engineering rules;
6. [`dev-docs/coding_style.md`](dev-docs/coding_style.md) — formatting, naming and code-reading conventions;
7. [`AGENTS.md`](AGENTS.md) — operational rules for automated coding agents.

Headers are also part of the architecture documentation. Public types should explain
ownership, lifetime, invariants, extension seams and non-obvious rationale.

If source and documentation disagree about what is currently implemented, source and tests
win. Do not silently preserve stale prose: report the mismatch and update the relevant
documentation when the architectural contract changes.

## License

The repository contains the **GNU General Public License, version 2**. See [`LICENSE`](LICENSE).
