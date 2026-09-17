# Mapping, Tile, Region, Cache and Chunk Model

## Overview

The mapping system deliberately presents a simple interface to game and simulation code:

```cpp
Tile tile = map.at(23, 14);

if (tile[Fire] == 0)
{
    ...
}
```

The caller deals with `Map` and `Tile`, and, for larger operations, with the intended `Region` concept.

It does **not** deal with the cache, storage layout, chunks, or layer-oriented I/O.

Those are implementation details beneath `Map`.

The conceptual boundary is:

```text
Game / simulations
        |
        | Map::at(), Region
        v
      Tile
        |
-------- Map implementation boundary --------
        |
      Cache
        |
      Chunk
        |
     Storage
```

## Tile

A `Tile` is the compact value representing one map position.

It contains:

- its map coordinate;
- the properties enabled in that build.

A tile therefore has identity through its position:

```cpp
Tile tile = map.at(23, 14);

Coord position = tile.position();
```

A copied tile keeps that identity:

```cpp
Tile a = map.at(23, 14);
Tile b = a;
```

Both values describe the same map position and initially contain the same property values.

There is no hidden reference back into `Map` or the cache.

Modifying a tile modifies only that local copy; the Map and the cache remain unchanged.

### Tile is a value type

`Tile` is intentionally cheap to copy.

Its property payload may be only a few bytes. If a build contains five byte-sized properties, the property portion occupies approximately five bytes. A future build with eight or ten properties grows accordingly.

The tile also carries its coordinate, but it should still remain a small ordinary value.

It should preferably be:

- compact;
- trivially copyable where practical;
- free of ownership;
- free of heap allocation;
- free of back-pointers into `Map` or `Cache`.

Returning `Tile` by value is therefore part of the intended API:

```cpp
[[nodiscard]] Tile Map::at(Coord position) const;
```

not an optimisation problem to be avoided.

## Tile property access

Properties are accessed symbolically:

```cpp
tile[Fire]
tile[Water]
tile[Height]
```

Conceptually:

```cpp
tile[Fire]
```

means:

```cpp
tile.properties()[Fire]
```

`operator[]` is simply convenient syntax for accessing a property already contained in the tile.

It does **not** cause a map lookup, cache lookup, or storage operation.

For example:

```cpp
Tile tile = map.at(23, 14);

if (tile[Fire] == 0)
{
    fire_end(tile);
}
```

The symbolic selector should resolve at compile time.

A build that does not contain the `Fire` property should not provide meaningful runtime handling for it; such use should be rejected at compile time.

## Build-defined properties

The property set is selected at build time.

For example, one build might contain:

```text
Ground
Height
Vegetation
Water
Fire
```

A future build may contain eight, ten, or another number of properties.

There is no need for every executable to reserve storage for all properties Landor may ever support.

Conceptually:

```cpp
using Tile = BasicTile<
    Ground,
    Height,
    Vegetation,
    Water,
    Fire
>;
```

The exact implementation may differ, but the principle is:

> A Tile contains exactly the world properties supported by that build.

## Properties and simulations

A property describes **state that exists in the world**.

It does not say whether that state is:

- authored;
- static;
- procedural;
- currently simulated;
- or intended to become simulated later.

Simulations determine how properties evolve.

For example, `Water` may initially be mostly authored map state.

Later, a water simulation may read:

```text
Height
Ground
Water
```

and update `Water` according to flow.

This allows behaviour such as:

- rivers flowing downhill;
- wells interacting with local water;
- irrigation;
- flooding;
- players digging channels;
- players diverting rivers;
- terrain modification changing future water flow.

An authored river is therefore an **initial condition**, not necessarily permanent truth.

The same principle applies to fire, vegetation, and future simulation properties.

The properties contained in a Tile are the basic state on which simulations operate.

## Area

`Area` is the geometric concept.

It describes a rectangular extent.

It has no cache, storage, simulation, or residency semantics.

## Region

`Region` is an intended game/simulation working-area concept, not a currently implemented API.

There is no `Region` type or `Map::region()` in the source yet, and its final form is deliberately left open. The concept remains part of the intended architecture.

Once implemented, a `Region` would describe a world-level working area requested by a game system or simulation.

For example, the fire simulation might operate on a `16 x 8` region.

Its role is:

> A Region is an area of world state used by an algorithm.

Regions would be allowed to have whatever dimensions are useful to that algorithm.

A fire simulation may request:

```text
16 x 8
```

while the cache internally works in:

```text
32 x 32
```

units.

The simulation should not know or care.

## Layers

World data is organised by property/layer.

For example:

```text
Ground layer
Height layer
Vegetation layer
Water layer
Fire layer
```

This organisation starts in the stored and procedural sources, where each property may be:

- stored independently;
- generated independently;
- compressed differently;
- fetched independently.

And it continues through the cache:

> **Layer organisation does not end at cache ingress.**

The resident representation is layer-oriented: the Cache preserves layer organisation internally, and Tile is assembled only at the presentation boundary.

The flow is:

```text
stored/authored/procedural layer sources
        |
        v
 layer-oriented Chunks
        |
        v
      Cache
 layer-oriented resident planes
        |
        | pack values for one coordinate
        v
      Tile
        |
        v
     caller
```

A compile-time property descriptor such as `Fire` may participate on both sides, but this does not mean that `tile[Fire]` performs layer resolution.

## Chunk

`Chunk` is reserved for the cache and I/O system.

It is not Landor's generic term for a rectangular portion of the world.

A Chunk exists because of storage and cache organisation.

A Chunk represents an aligned square portion of **one layer**, used for cache residency and I/O.

Which backing source supplies a layer is a separate resolution concern; a Chunk does not bind its layer to one particular source.

For the same spatial area, these are separate chunks:

```text
Ground Chunk
Height Chunk
Water Chunk
Fire Chunk
```

If a cache fill needs five properties, it may therefore require five layer Chunks for the same spatial area.

## Cache chunk size

The cache has a canonical chunk size, fixed for the Cache type.

For example:

```text
32 x 32
```

This is similar to a conventional cache-line size, except spatial and two-dimensional.

If an algorithm needs only an `8 x 8` Region, the cache may still need the containing `32 x 32` cache area.

```text
32 x 32 cache area
+--------------------------------+
|                                |
|       requested 8 x 8          |
|       +--------+               |
|       |        |               |
|       |        |               |
|       +--------+               |
|                                |
+--------------------------------+
```

If the required layer planes are resident there, no I/O is necessary.

If a layer plane is missing, the cache obtains the corresponding canonical Chunk for that layer and installs the values in the slot's layer plane.

This deliberately trades modest over-fetching for:

- fewer fragmented storage operations;
- good spatial locality;
- predictable cache layout;
- simple residency decisions.

## Chunk alignment

Chunks should be aligned to a grid corresponding to their size.

Power-of-two dimensions are especially convenient.

For example, aligned `8 x 8` subareas naturally fit within aligned `32 x 32` cache areas.

This avoids arbitrary partial cache residency.

The cache can reason in simple terms:

```text
Fire chunk (4, 7): resident
Fire chunk (5, 7): missing
Fire chunk (6, 7): resident
```

rather than:

```text
47% of Fire chunk (5, 7) is resident
```

Smaller Chunks may exist inside the I/O machinery when useful, but canonical cache residency should remain simple unless a real requirement later justifies hierarchical cache entries.

## Cache

The cache is private implementation machinery beneath `Map`.

Normal game and simulation code should not inspect it.

The caller writes:

```cpp
Tile tile = map.at(23, 14);
```

not:

```cpp
Tile tile = cache.at(23, 14);
```

`Map::at()` hides whether the requested tile:

- was already fully resident;
- required one or more missing layer Chunks to be resolved and filled;
- was assembled from resident layer values.

Conceptually:

```text
Map::at(x, y)
      |
      v
check map coordinates
      |
      v
find containing cache area
      |
      v
ensure required layer planes are resident
      |
      +---- layer already resident
      |
      `---- layer missing
              |
              v
        resolve/fetch layer Chunk
              |
              v
        fill corresponding layer plane
      |
      v
pack one Tile from resident layer values
      |
      v
caller
```

### Capacity and replacement

Cache capacity counts **spatial slots**, not individual layer chunks. One slot covers one canonical cache area and holds one layer plane per supported layer, each plane independently resident.

The current implementation has:

- no replacement policy;
- no eviction policy;
- no dirty/write-back policy.

When every spatial slot is occupied, filling a new spatial area fails rather than evicting existing state. Filling a missing layer in an already-present slot can still succeed, because it reuses the slot.

## Cache representation

The resident cache is layer-oriented.

It does not reorganise the resident world into an array of Tiles. Instead, each cache spatial slot holds one resident plane per supported layer:

```text
Spatial slot: origin (32, 64)

Ground plane:  resident
Height plane:  resident
Water plane:   missing
Fire plane:    resident
```

Each layer plane is independently resident. A missing layer for an already-present spatial slot can be filled without allocating another spatial slot.

The cache does not store authoritative Tile objects. When a complete Tile is requested, `Cache::tile(position)` constructs a fresh value by selecting one value from each resident layer plane at that coordinate:

```text
resident Ground plane
resident Height plane
resident Water plane
resident Fire plane
        |
        | select one value from each plane
        | for coordinate (x, y)
        v
      Tile
```

This matches how simulations normally consume data: several properties belonging to the same position are used together.

The returned Tile owns its values. Mutating the returned Tile does not mutate the cache.

## Checked Map access

All public Map access should be checked.

For example:

```cpp
Tile tile = map.at(23, 14);
```

validates that `(23, 14)` belongs to the Map before performing the underlying operation.

There is little value in exposing a separate unchecked public access path.

Unlike an ordinary in-memory array lookup, a Map access may involve:

- coordinate translation;
- cache lookup;
- cache miss handling;
- Chunk selection;
- several layer reads;
- storage I/O;
- tile assembly.

The cost of a bounds check is negligible beside those operations.

Even on small embedded targets, Landor does not require extreme frame rates. A target delivering approximately `8 FPS` can already be entirely acceptable.

The design should therefore favour:

- correctness;
- explicit contracts;
- predictable behaviour;
- easy debugging;

over saving a handful of comparisons in Map access.

This does **not** mean deliberately inefficient code. It means not creating unsafe APIs to optimise operations whose cost is insignificant compared with the work beneath them.

## `at()` versus `operator[]`

Landor should follow the useful semantic expectation associated with `at()`:

```cpp
map.at(position)
```

means checked access.

There is currently no compelling reason to provide:

```cpp
map[position]
```

as an unchecked alternative.

Inside a `Tile`, however:

```cpp
tile[Fire]
```

has different semantics.

It is merely convenient symbolic access to a property already contained in the value:

```cpp
tile.properties()[Fire]
```

No Map or cache operation occurs.

So the two forms do not conflict:

```cpp
Tile tile = map.at(position); // checked world access
auto fire = tile[Fire];       // cheap property access
```

## Public versus internal concepts

The architecture should maintain this separation:

```text
PUBLIC / GAME-SIDE CONCEPTS

Map
Tile
Region (intended, not yet implemented)
Area

--------------------------------

INTERNAL MAPPING / I/O CONCEPTS

Cache
Chunk
stored layers
Storage

--------------------------------

PLATFORM STORAGE

filesystem
flash
SD card
ROM
other target-specific implementations
```

In particular:

- simulations should not know about Chunks;
- simulations should not know cache-line dimensions;
- simulations should not access the cache directly;
- `Tile` should not expose cache residency;
- `Region` dimensions should not be constrained by storage layout;
- `Storage` should not need to understand simulation semantics.

## Core design principles

### Tile is a small identified value

A `Tile` contains:

- its coordinate;
- its build-defined properties.

It is deliberately cheap to return and copy by value.

```cpp
Tile tile = map.at(x, y);
```

is the ordinary interface.

### Tile property access is symbolic

```cpp
tile[Fire]
```

means property access within the Tile itself.

It does not query Map, Cache, or Storage.

### Map access is checked

All ordinary world access validates coordinates.

The cost is insignificant compared with the possible cache and storage work underneath.

### Region is algorithmic

The intended `Region` describes the world extent on which an algorithm wants to operate.

Its dimensions are chosen for the algorithm, not for storage.

### Chunk is I/O-specific

A `Chunk` exists because of cache/storage organisation.

It should not be reused as the generic rectangular work unit of simulations.

### Cache is hidden

The cache exists solely to make residency and I/O efficient.

It must not leak into normal game code.

### Layers remain fundamental through cache residency

Layer-oriented organisation belongs to the stored, authored and procedural sources, and it does not end at cache ingress.

Resident state is layer-oriented: the cache holds one plane per layer, and Tile is assembled only at the Map/Cache presentation boundary.

### Properties precede simulations

A property may exist long before a full simulation for it exists.

The later addition of water flow, fire propagation, vegetation growth, or other systems should not require redesigning the basic Tile/Map/storage boundary.

### Build determines the property set

The number and identity of Tile properties are compile-time/build-time decisions.

No runtime accommodation is required for properties that cannot exist in that executable.

### Authored state is an initial condition

Stored properties describe the starting/current state of the world.

Simulations may subsequently evolve them.

A river, fire, vegetation distribution, or other authored feature need not remain unchanged merely because it originated in stored map data.
