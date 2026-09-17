# Mapping, Tile, Region, Cache and Chunk Model

## Overview

The mapping system deliberately presents a very simple interface to game and simulation code:

```cpp
Tile tile = map.at(23, 14);

if (tile[Fire] == 0)
{
    ...
}
```

The caller deals with `Map`, `Tile`, and, for larger operations, `Region`.

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

---

## Tile

A `Tile` is the compact value representing one map position.

It contains the properties present in that build of Landor.

For example, a build might currently contain five properties:

```text
Ground
Height
Vegetation
Water
Fire
```

A future build may contain eight, ten, or some other compile-time-defined number.

The set of properties is determined at build time. A `Tile` does not reserve runtime space for hypothetical properties that are not part of that build.

If each property occupies one byte, a five-property `Tile` may therefore occupy approximately five bytes; an eight-property build approximately eight bytes, and so on.

`Tile` is intentionally a **cheap value type**.

It should normally be safe and inexpensive to:

```cpp
Tile a = map.at(x, y);
Tile b = a;
```

Copying a `Tile` copies its values. It does not retain a pointer, reference, or handle into the cache.

This is an important part of the API rather than an accidental implementation detail.

A tile should preferably remain:

* compact;
* trivially copyable where practical;
* free of ownership;
* free of heap allocation;
* free of back-pointers into `Map` or `Cache`.

### Symbolic property access

Properties are accessed symbolically:

```cpp
tile[Fire]
tile[Water]
tile[Height]
```

For example:

```cpp
Tile tile = map.at(23, 14);

if (tile[Fire] == 0)
{
    fire_end(tile);
}
```

This symbolic syntax should resolve at compile time. It is not a runtime lookup into some external layer object.

Conceptually:

```cpp
tile[Fire]
```

means:

```text
select the Fire property already present in this Tile
```

not:

```text
go and resolve the Fire layer from storage
```

---

## Properties and simulations

A property describes **state that exists in the world**.

It does not specify whether that state is:

* authored;
* static;
* procedural;
* currently simulated;
* or intended to become simulated later.

The simulations determine how properties evolve.

For example, `Water` may initially be mostly authored world data. Later it can become an actively simulated property.

The representation does not need to change when that happens.

A future water simulation may read:

```text
Height
Ground
Water
```

and update `Water` according to flow.

This allows world behaviour such as:

* rivers flowing downhill;
* wells interacting with local water;
* irrigation;
* flooding;
* players digging channels;
* players diverting rivers;
* terrain modification changing future water flow.

Thus an authored river is best understood as an **initial condition**, not necessarily permanent truth.

The same principle applies to the other simulation properties.

The build's tile properties form the fundamental state upon which simulations operate.

---

## Area

`Area` is the geometric concept.

It describes a rectangular extent and belongs to the geometry machinery.

It has no cache, storage, simulation, or residency semantics.

---

## Region

A `Region` is a world-level working area requested by a game system or simulation.

For example, the fire simulation might operate on a `16 x 8` region.

Conceptually:

```cpp
Region region = map.region(...);
```

The precise Region API can be decided when its implementation is needed, but its role is clear:

> A Region is an area of live tile-oriented world state used by an algorithm.

Regions are not required to match cache or storage boundaries.

A simulation may request:

```text
16 x 8
```

even if the cache works internally with:

```text
32 x 32
```

chunks.

The simulation should not need to know or care.

---

## Layers

Persistent or source data is organised by property/layer before it enters the cache.

For example:

```text
Ground layer
Height layer
Vegetation layer
Water layer
Fire layer
```

These are useful for storage because each property can be stored, generated, compressed, or fetched independently.

However:

> **Layer organisation stops at the cache boundary.**

Above that boundary, the world is tile-oriented.

The cache takes layer-oriented data and materialises ordinary `Tile` values.

So the transition is:

```text
layer-oriented stored data
        |
        v
      Cache
        |
        v
tile-oriented resident data
```

A symbolic property such as `Fire` may be used on both sides as a compile-time descriptor, but that does not mean live game code is performing layer resolution.

---

## Chunk

`Chunk` is reserved for the cache and I/O system.

It should not become Landor's generic name for an arbitrary rectangular piece of the world.

The reason for the abstraction is specifically storage and cache organisation.

A Chunk represents an aligned square portion of **one stored layer** used for cache/fetch operations.

For example:

```text
Fire Chunk
Water Chunk
Height Chunk
```

for the same spatial cache area are three separate chunks.

If a cache fill needs six stored layers, it may therefore require six chunk reads.

### Cache chunk size

The cache has a configured canonical chunk size.

For example:

```text
32 x 32
```

This is analogous to the line size of a conventional cache, except it is two-dimensional.

If an algorithm asks `Map` for data covering an `8 x 8` area, that does **not** imply an `8 x 8` storage fetch.

The cache determines the containing canonical chunk:

```text
32 x 32 cache chunk
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

If the canonical chunk is resident, no I/O is needed.

If it is absent, the cache fetches the whole `32 x 32` chunk for the required layer.

This deliberately trades some over-fetching for:

* fewer fragmented reads;
* predictable cache organisation;
* spatial locality;
* simple residency tests.

### Chunk alignment

Chunks should be aligned to the grid corresponding to their size.

If chunk dimensions are powers of two, this gives especially simple containment.

For example, aligned `8 x 8` chunks fit cleanly inside aligned `32 x 32` cache chunks.

This avoids arbitrary partial-overlap splitting.

For cache purposes, residency can therefore remain binary at canonical chunk granularity:

```text
Fire chunk (4, 7): resident
Fire chunk (5, 7): missing
Fire chunk (6, 7): resident
```

rather than:

```text
half of Fire chunk (5, 7) is resident
```

Smaller chunk sizes may still exist within the I/O machinery when useful, but `Chunk` remains an I/O/cache concept, and the cache itself should have one clearly defined canonical residency/fetch size unless a real need for hierarchical residency appears later.

---

## Cache

The cache is an implementation detail beneath `Map`.

Normal game and simulation code should never need to inspect it.

The caller writes:

```cpp
Tile tile = map.at(23, 14);
```

not:

```cpp
Tile tile = cache.at(23, 14);
```

`Map::at()` hides whether the requested tile was already resident or required storage I/O.

Conceptually:

```text
Map::at(x, y)
      |
      v
find containing cache area
      |
      +---- resident ----> return Tile value
      |
      `---- missing
              |
              v
        determine required
        layer Chunks
              |
              v
           Storage
              |
              v
        populate cached Tiles
              |
              v
        return Tile value
```

If five properties must be loaded, one cache miss for the corresponding spatial area may conceptually involve:

```text
Ground      Chunk
Height      Chunk
Vegetation  Chunk
Water       Chunk
Fire        Chunk
```

Each chunk is layer-oriented on the storage side.

Once loaded, those values populate the corresponding fields/properties of the resident tile array.

From that point upward, the layer distinction no longer matters operationally.

---

## Cache representation

The resident cache should be tile-oriented.

Conceptually:

```text
+----------+----------+----------+
| Tile     | Tile     | Tile     |
| Ground   | Ground   | Ground   |
| Height   | Height   | Height   |
| Water    | Water    | Water    |
| Fire     | Fire     | Fire     |
+----------+----------+----------+
```

This reflects the way simulations consume the data: they commonly need several properties belonging to the same spatial position.

A tile in the cache is therefore very close to the `Tile` value returned by `Map::at()`.

This is intentional.

---

## Public versus internal concepts

The architecture should maintain this separation:

```text
PUBLIC / GAME-SIDE CONCEPTS

Map
Tile
Region
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

* simulations should not know about chunks;
* simulations should not know cache-line dimensions;
* simulations should not access the cache directly;
* `Tile` should not expose cache residency;
* `Region` dimensions should not be constrained by storage layout;
* `Storage` should not need to understand game simulation semantics.

---

## Core design principles

### Tile is a value

`Tile` is deliberately small and cheap enough to return and copy by value.

```cpp
Tile tile = map.at(x, y);
```

is the normal interface.

### Region is algorithmic

A `Region` describes the live-world extent on which an algorithm wants to operate.

Its shape is chosen by that algorithm.

### Chunk is I/O-specific

A `Chunk` exists because of storage/cache organisation.

It should not be reused as the generic rectangular work unit of simulations.

### Cache is hidden

The cache exists to make residency and I/O efficient.

It must not leak into the normal world-facing API.

### Layers end at cache ingress

Layer-oriented organisation is valuable for storage.

Resident game state is tile-oriented.

### Properties precede simulations

A property may exist before an active simulation for it exists.

Adding or extending simulations should not require redesigning `Tile`, `Map`, or the storage boundary.

### Build determines the property set

The number and identity of tile properties are compile-time/build-time choices.

There is no need for runtime support for layers that cannot exist in that build.

### Authored state is an initial condition

Properties loaded from storage describe the starting/current world state.

Simulation may subsequently evolve them.

A river, fire, vegetation distribution, or other authored feature need not remain unchanged merely because it originated in stored map data.
