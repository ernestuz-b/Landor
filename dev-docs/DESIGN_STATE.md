# Landor — Design State

This file describes the current intended architecture.

It replaces older architectural descriptions that treated the world as a stored tile grid,
compressed terrain map or single hand-authored valley.

For implementation reality, consult `../STATUS.md` and the source tree. If source and this
document disagree, report the mismatch: source tells you what exists, this file tells you
where the design is going.

## 1. Project identity

Landor is an exploration and simulation game with an educational purpose.

The simulation is not background decoration. It is intended to be the substrate of the
game: weather, fire, water, plants, animals, people, settlements, ownership, schedules and
economy can become gameplay because they share one world model.

The tone is "fantastic realism": unusual things may be introduced by game mechanics, but
ordinary world systems still apply. A magical fire may appear underwater; that does not
require the map representation to forbid it. The fire/water simulation decides what
happens next.

The first renderer is UTF-8/text. Later renderers may be graphical. Rendering must not
become the owner of world semantics.

## 2. Target philosophy

Landor targets Linux-class systems and small microcontrollers.

The same project should be able to scale down to Raspberry Pi Pico/RP2040 and STM32-class
hardware without turning the desktop version into embedded-style spaghetti.

Ports may differ in:

- rendering;
- map/world scale;
- simulation density;
- asset count;
- memory capacities;
- some game content.

They should share formats and architectural ideas where practical. Behavioural identity
across every target is not required.

Landor avoids unnecessary embedded OSS/framework dependencies. Straightforward
infrastructure that can be implemented compactly and audited locally should remain local.

## 3. C++ and portability

Landor is C++23.

The portable contract is not "every C++23 library facility exists on every compiler".
A facility becomes part of portable Landor once the supported embedded toolchains provide
the subset Landor uses.

Modern C++ is encouraged where it makes contracts clearer:

- `constexpr`;
- `consteval`;
- concepts;
- structural non-type template parameters;
- `std::span`;
- `std::expected`;
- RAII;
- scoped enums;
- `std::source_location`;
- compile-time validation.

There are no exceptions in Landor code.

There is no RTTI-based design: no `dynamic_cast`, no `typeid`.

`noexcept` is used semantically where a function promises not to throw; it is not
mechanically added to every declaration.

## 4. World directory and namespaces

Filesystem layout and namespaces are separate decisions.

`src/world/` is the physical home of world/environment machinery. Types in that directory
may belong to `landor::geo`, `landor::world` or another appropriate namespace.

Do not reorganize directories merely to mirror namespaces.

## 5. Layer-oriented world

### 5.1 Layer

A `Layer` is a type-level spatial property.

A concrete layer provides:

```cpp
static constexpr LayerId id;
using value_type = ...;
```

Examples may include terrain, elevation, fire, water, moisture and wind.

A layer type contains no:

- storage;
- cache;
- persistence policy;
- simulation algorithm.

Those belong to the systems that provide or mutate values.

### 5.2 Missing authored data is not unsupported capability

This is a central invariant.

A patch with no Fire binding does not mean Fire is impossible at that coordinate. It means
only that the patch contributes no authored Fire value there.

The map may obtain the Fire value from:

- materialized working state;
- another authored contribution;
- procedural generation;
- a layer default.

This keeps map representation permissive while simulation enforces physical/game rules.

### 5.3 Tile is synthetic

A `Tile<Layers...>` is a short-lived owned value assembled for one coordinate.

It contains copies of the values for all layers supported by that map/build.

A tile contains no:

- reference into patch data;
- pointer into cache storage;
- managed-heap address;
- storage handle.

Changing a returned tile does not mutate the map.

There is no authoritative array of complete tiles.

A cache of assembled tiles is allowed only as disposable derived state.

## 6. Authored content and live geography

### 6.1 Patch

A `Patch` is immutable reusable authored content.

It contains:

- `PatchId`;
- authored name;
- natural position;
- rectangular local extent;
- zero or more `LayerBinding` entries.

A `LayerBinding` connects one authored layer to one logical `storage::SourceId`.

Patch does not know whether the source is a host file, SD-card file, flash, ROM or raw
NAND.

Patch owns no live placement state.

### 6.2 Placement

A `Placement` is one live occurrence of a `Patch`.

It contains:

- `PlacementId`;
- `PatchId`;
- map position;
- `Orientation`.

Transform order is:

```text
reflection -> rotation -> translation
```

Several placements may reuse one patch.

Mutation of a placement goes through the owning `Map` so derived spatial caches can be
invalidated correctly.

### 6.3 PatchSet

A `PatchSet` is an immutable composition recipe.

A recipe contains named/id'd roles. Each role provides candidate patches and a minimum and
maximum count.

Example home roles:

```text
house
well
barn
field
unique feature
```

`PatchSet` does not implement the composition algorithm. Composition policy is separate.

Given the same deterministic inputs, composition should reproduce the same untouched
baseline.

### 6.4 Place

A `Place` is narrative identity, not geometry.

Schedules, ownership, dialogue, missions and actors can refer to a stable `PlaceId` such as
"the player's home" or "GoodMagePalace" without depending on which patches implement it.

A place may refer to one or more `PlacementId`s.

A place owns no map data and performs no spatial queries.

## 7. Map

`Map` is the logical spatial surface of one region of the world.

A map is not an authored file and is not a rectangular stored `Tile` array.

The map:

- owns live placements;
- refers to immutable authored patch descriptors;
- resolves each supported layer independently;
- combines authored, procedural/default and materialized working state;
- returns synthetic tile values;
- owns/integrates any disposable spatial cache needed to accelerate those answers.

Overlap resolution is per-layer. A placement that supplies no value for one layer does not
hide a lower-priority contribution to that layer.

The exact precedence rule belongs in `Map`, not in `Patch`.

The intended conceptual resolution order is:

```text
materialized/working override
    -> highest-priority authored placement that supplies this layer
    -> procedural/default source
```

The precise rule must be pinned by tests when implemented.

## 8. Simulation and mutation

Simulation works on world/layer state, not renderer objects.

The map may temporarily represent combinations that simulation will rapidly resolve. This
is intentional.

World mutations should have a small number of explicit front doors so cache invalidation,
persistence and deterministic replay are not bypassed.

The exact mutation API is still open and should be introduced by concrete simulation
requirements rather than by speculative framework design.

## 9. Storage

### 9.1 Logical contract

World code addresses storage using:

```text
SourceId
Offset
Size
```

The platform-independent storage contract currently provides:

```text
read
write
size
```

Reads and writes are whole-range operations: success means the requested logical range was
fully transferred.

Physical representation stays below the boundary.

Common world code does not know:

- filenames;
- paths;
- file descriptors;
- sectors;
- NAND pages;
- erase blocks;
- mount points.

### 9.2 Build-selected concrete implementation

Storage is known at build time.

Concrete implementations have descriptive names:

```text
StorageFilesystem
StorageSpiNand
...
```

The selected implementation exposes:

```cpp
using Storage = StorageFilesystem;
```

Common code uses `landor::storage::Storage`.

Do not add a virtual storage base class merely to hide a type CMake already knows.

### 9.3 C APIs and vendor HALs

Platform code may use C APIs and vendor HALs directly where appropriate.

Wrap them when Landor needs a semantic boundary, not merely to make a C handle look more
C++-like.

## 10. Memory and allocation

### 10.1 Fixed when bounded

Prefer:

- values;
- stack objects;
- `std::array`;
- spans/views;
- fixed-capacity structures.

If a capacity is fixed at compile time, it should normally be a template parameter.

When a type has several related capacities, prefer a named structural policy:

```cpp
Map<MapCapacity{
    .placements = 64,
    .tile_cache = 16
}>
```

rather than:

```cpp
Map<64, 16>
```

once the positional arguments stop being self-explanatory.

### 10.2 Dynamic allocation

Dynamic allocation is a fallback, not the default.

Use the managed heap only when the lifetime/quantity is genuinely dynamic and a fixed
capacity/value representation is not appropriate.

The managed heap comes from another project. It is currently vendored under
`include/managed_heap/` but should be treated as an imported component.

Do not modify it casually. A defect or required feature should be handled explicitly and
separately. Whether it eventually becomes a submodule/subrepo remains open.

### 10.3 Standard containers

Allocating STL containers are not forbidden because they are STL; they are constrained
because allocation and pointer-stability matter.

An allocating standard container may be used when:

- its cost is appropriate for the target;
- its allocator semantics are explicitly satisfied;
- the allocation is not hidden from a critical deterministic/real-time path.

Do not assume the relocatable managed heap is an ordinary STL allocator. Standard
containers generally retain pointers into their allocated blocks.

## 11. Configuration

A contained application-level configuration object is acceptable.

Global configuration must not become a service locator used from deep subsystems.

Interpret configuration near the composition root and pass lower-level objects the exact
value/policy they require.

Use CMake according to the nature of the choice:

### Build switch

Use a compile definition for a genuine build switch.

### Typed generated values

Use `configure_file()` when C++ should consume generated typed constants/configuration.

Generated headers belong under the build tree, for example:

```text
transient/build/generated/landor/config.hpp
```

and should be included as:

```cpp
#include "landor/config.hpp"
```

after CMake adds the generated include directory.

### Whole implementation

If the implementation is known at build time, CMake selects the source/header. Do not
generate a runtime selector for it.

## 12. Error handling and invariants

Exceptions are not used.

A failure is represented only when the caller has a meaningful decision to make.

Use the simplest domain representation:

- `nullptr` for a natural lookup absence;
- bool for a genuinely binary, obvious outcome;
- `std::expected<T, E>` for value-or-recoverable-error;
- named result/status structures when that is clearer.

Do not use `std::optional` as a generic substitute for error modelling.

Do not use `std::expected::value()` as normal Landor control flow because its failure path
is exception-oriented. Check the result explicitly.

Broken internal state is a programming fault, not a recoverable error result.

Use assertions in diagnostic builds.

In production, an unrecoverable condition may simply terminate unless the platform has
meaningful stronger semantics such as:

- entering a safe state;
- retaining crash diagnostics;
- controlled reset.

Do not create a fatal abstraction merely to rename `abort()`.

## 13. Logging and diagnostics

Logging is cross-cutting instrumentation and may be globally reachable.

It must not influence game semantics.

The intended logger preserves the useful design of the older production logger while using
modern C++ facilities.

### Compile-time grades

Verbose logging can be compiled away completely.

The outer logging API may deliberately be a macro so a disabled log statement does not
evaluate its arguments.

### Levels and categories

Severity and diagnostic category are separate concepts.

Example severity:

```text
Trace
Debug
Info
Warning
Error
```

Example categories:

```text
General
Function
Storage
World
Hardware
Simulation
```

### Function tracing

RAII function tracing is available at a very verbose grade.

A scope helper logs entry in its constructor and exit in its destructor. Modern
implementation can use `std::source_location` to capture function/file/line automatically.

Function traces are especially valuable when an agent or developer is reconstructing a
control-flow failure.

### Real-time timing

A diagnostic trace must preserve timing when it may be used to replay hardware or
real-time failures.

Use a monotonic timestamp captured at the event, before slow sink I/O.

A printed record may also show the delta since the previous record, but delta is derived
from event timestamps rather than being the only timing information.

### Serialization

Output records must be serialized so concurrent producers do not create unreadable output.

Host builds may use a mutex or dedicated logging thread.

Embedded builds may use a fixed-capacity single-producer/single-consumer ring with an
interrupt- or scheduler-driven UART consumer.

The ring's ownership and publication rules must be valid under the C++ memory model.
Modern SPSC code should use appropriate atomic acquire/release publication rather than
relying only on native-word hardware atomicity.

Formatting should not require a large embedded framework. Rich host formatting may use
standard facilities when suitable; constrained sinks may use a smaller implementation.

## 14. Dependencies

Landor runtime/core code should remain independent of large third-party frameworks.

Development dependencies are different: GoogleTest, GoogleMock, compilers and static
analysis tools are normal development infrastructure.

On embedded targets, prefer local small infrastructure over importing a framework for a
small, auditable problem.

## 15. Documentation as architecture

Headers are expected to teach the next developer how the class fits into the system.

Public/architectural headers should explain:

- responsibility;
- ownership;
- lifetime;
- invariants;
- extension seams;
- non-obvious performance/allocation consequences;
- why a tempting alternative was rejected when that knowledge prevents future damage.

Do not turn headers into chronological diaries.

Important settled alternatives and their rationale belong in
`DESIGN_DECISIONS.md`.

## 16. Open questions

The following remain intentionally open:

- final mutation API between simulations and materialized layer state;
- exact layer precedence/overlap semantics once `Map::resolve()` is implemented;
- long-term repository relationship of `include/managed_heap/` (vendored vs submodule/subrepo);
- exact logging record representation and formatter on embedded targets;
- concrete platform storage implementations beyond the filesystem backend;
- networking/multiplayer synchronization details.

Do not invent answers to these merely to complete an unrelated task.
