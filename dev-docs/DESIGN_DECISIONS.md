# Landor — Design Decisions

This is a compact record of important **settled** architectural choices and why they were made.

It is not a diary and it does not record every idea considered.

When an implementation choice looks unusual, check here before replacing it with a more conventional pattern.

## D-01 — C++23, portable subset verified by target toolchains

**Decision:** Landor is a C++23 project.

**Why:** One purpose of the project is to use and relearn modern C++ rather than freezing the code at an older language version.

**Constraint:** "C++23" does not mean every library feature is assumed present on every embedded compiler. Facilities used by portable core code must be validated on the supported embedded toolchains.

## D-02 — No exceptions

**Decision:** Landor code does not use exceptions.

**Why:**

- control flow should remain visible at the call site;
- exception catch boundaries are easy to place at the wrong architectural level;
- small embedded targets benefit from avoiding exception runtime machinery;
- explicit result types are a better match for recoverable domain outcomes.

Recoverable outcomes are values/results. Broken invariants are programming faults.

External test frameworks may internally use exceptions; that does not make exceptions part of Landor's design.

## D-03 — No RTTI

**Decision:** Landor does not use RTTI for architecture or dispatch.

No `dynamic_cast` or `typeid`.

**Why:** Runtime type discovery is unnecessary when most variation is already known at build time or through the type system.

## D-04 — Choose polymorphism by when the choice is known

**Decision:**

- build-time choice -> CMake selects a concrete implementation;
- compile-time type variation -> templates/concepts;
- runtime-unknown choice -> virtual/runtime polymorphism.

**Why:** Virtual dispatch solves a real problem when the concrete type is unknown at runtime. Using it for a build-known device/storage backend adds machinery without adding information.

Storage is the first concrete example.

## D-05 — Build-selected concrete platform aliases

**Decision:** platform implementations use descriptive concrete names and the selected implementation exposes the common alias.

Example:

```cpp
using Storage = StorageFilesystem;
```

**Why:** Common code sees one clear type, while another coder can still see which concrete implementation exists in the selected platform file.

Do not build preprocessor selector mazes in common code.

## D-06 — Filesystem directories do not mirror namespaces

**Decision:** project directories group responsibilities; namespaces express semantic ownership.

`src/world/` may contain both `landor::geo` and `landor::world`.

**Why:** forcing directory and namespace hierarchies to mirror each other creates churn without improving the model.

## D-07 — Layer-oriented world and Cache; Tile is presentation

**Decision:** authoritative and resident map state remains layer-oriented through storage, Chunk, Cache, and Map resolution.

`Chunk` is the aligned spatial I/O/cache unit for one layer.

`Cache` stores bounded spatial slots whose layer planes are independently resident. It does **not** reorganise the resident world into an array of complete Tiles.

`Map::at()` returns a small owned `Tile<CoordT, Layers...>` value by packing one value from each required resident layer at the requested coordinate.

`tile[LayerTag]` selects a value already inside the Tile and performs no Map, Cache, or Storage lookup.

**Why:**

- unrelated layers have different sources and lifecycles;
- authored absence must not mean capability absence;
- simulations can materialise state independently;
- one spatial cache area may have some layers resident and others missing;
- keeping Cache layer-oriented avoids coupling persistence/residency to the caller-facing Tile representation;
- Tile can remain a compact, copyable presentation value with no back-pointer or proxy semantics.

A separate full-Tile residency cache is not part of the current design. If one is ever proposed as an additional optimisation, it requires its own measured justification and must remain derived state rather than authoritative world representation.

## D-08 — Patch is authored content; Placement is live geography

**Decision:** immutable authored data and live occurrence state are separate types.

**Why:** one authored house/barn/well/etc. should be reusable at several coordinates and orientations without copying authored layer data.

Patch-local `(0, 0)` is the transform anchor. `Placement::position()` is the Map coordinate of that origin. Transform order is:

```text
reflection -> rotation -> translation
```

Conceptually:

```text
world = position + rotate(reflect(local))
```

Orientation happens about `(0, 0)` and does not renormalise the resulting bounds around a new top-left. A rotated/reflected Patch may therefore extend to negative coordinates relative to `Placement::position()`.

For dense v1 authored sources, source-local `(0, 0)` is the same Patch-local origin; `D` gives the local rectangle size and `P` gives the natural world position of that origin.

## D-09 — Place is narrative identity

**Decision:** story systems use `PlaceId` rather than `PatchId`.

**Why:** "the player's home" or "GoodMagePalace" should remain the same narrative place even when its physical implementation uses several placements or changes geometry.

## D-10 — PatchSet is a recipe, not an owner or generator

**Decision:** `PatchSet` describes roles/candidates/counts. Composition policy is separate.

**Why:** authored content, composition recipe and deterministic generation algorithm have different responsibilities and should be independently replaceable.

## D-11 — Storage uses opaque logical sources

**Decision:** geography addresses storage through `SourceId`, `Offset` and `Size`.

**Why:** patches/maps should not know whether a source is a path, SD-card file, flash region or NAND object.

No physical storage geometry leaks into authored world descriptors.

## D-12 — Fixed capacity becomes type information

**Decision:** if a value is fixed at compile time and affects layout/capacity, make it a template/non-type template parameter.

When several related capacities exist, prefer a named structural policy object.

**Why:** the dependency stays visible, compiler checks relationships, and there is no hidden global capacity lookup in low-level code.

## D-13 — Dynamic allocation is exceptional

**Decision:** prefer bounded value/fixed storage. Use the managed heap only when genuine dynamic allocation is unavoidable.

**Why:** predictable memory is important on embedded targets and usually makes ownership clearer on hosts too.

The managed heap is imported from another project and is not to be casually edited.

## D-14 — Managed heap is not automatically an STL allocator

**Decision:** allocating standard containers require an allocator whose semantics actually match the container.

**Why:** the managed heap may relocate objects; standard containers normally retain addresses into their allocation. "It allocates memory" is not enough to make it a valid allocator.

## D-15 — Contained global configuration is allowed

**Decision:** application-level global configuration is acceptable when contained and preferably read-only after initialization.

Deep subsystems do not reach into it.

**Why:** passing the entire application configuration through every layer is busywork, but hidden deep configuration access becomes a service locator and destroys local reasoning.

Pass the specific value or policy a subsystem needs.

## D-16 — CMake selects the configuration mechanism

**Decision:**

- simple build switch -> compile definition;
- typed generated values -> `configure_file`;
- whole build-known implementation -> selected source/header.

**Why:** these are different kinds of configuration and should not be forced into one mechanism.

## D-17 — Recoverable failure is explicit; `std::expected` is preferred when rich

**Decision:** use domain-specific direct outcomes for simple cases and `std::expected<T,E>` when a caller needs either a value or a meaningful recoverable reason.

`std::optional` is not the default error mechanism.

**Why:** optional often obscures the reason something did not happen and spreads presence-check ceremony through code.

Simple natural absence may still be represented by `nullptr` or a bool.

## D-18 — Invariants are not recoverable errors

**Decision:** programming faults and corrupted internal invariants are asserted in diagnostic builds rather than returned as ordinary domain failures.

Production adds a special fatal path only when the platform has meaningful semantics such as safe-state entry, retained diagnostics or reset policy.

**Why:** pretending corrupted state is a recoverable business outcome makes code continue after its assumptions are already false.

## D-19 — Full-throttle warnings

**Decision:** Landor code should compile warning-free under aggressive compiler warnings, with warnings treated as errors.

**Why:** narrowing, signedness, shadowing, bad casts and portability assumptions are cheap to catch at compile time and expensive on embedded hardware.

Third-party dependencies do not inherit this policy.

## D-20 — `noexcept` is semantic

**Decision:** use `noexcept` when a function genuinely promises non-throwing behaviour; do not stamp it everywhere mechanically.

**Why:** declarations should document semantics and remain easy to graft/reuse in other projects.

## D-21 — Prefer direct includes

**Decision:** include the header that defines a contract rather than aggressively forward-declaring everything.

**Why:** explicit dependencies are easier to read and usually require less ceremony. Forward declarations are still valid when they materially reduce coupling.

## D-22 — Headers are architecture documentation

**Decision:** public/architectural headers explain responsibility, ownership, lifetime, invariants, rationale and extension seams.

**Why:** the next developer should be able to learn how a class belongs in the system from the declaration.

Historical rationale that would otherwise clutter headers belongs here.

## D-23 — GoogleTest and GoogleMock

**Decision:** tests use GoogleTest. GoogleMock is allowed when interactions are what the test needs to specify.

**Why:** concrete fakes are often clearer for state behaviour; mocks are useful for interaction contracts. Host development resources are not a meaningful constraint.

## D-24 — Test tree mirrors source tree

**Decision:**

```text
src/world/coord.hpp
tests/world/test_coord.cpp
```

**Why:** test ownership remains obvious as the tree grows.

CMake lists test sources explicitly rather than globbing.

## D-25 — Logging is globally reachable instrumentation

**Decision:** logging may be global/cross-cutting provided it cannot alter domain semantics.

**Why:** forcing a logger through every constructor adds noise without improving ownership of game state.

Configuration is different: deep code must not use a global configuration object to change behaviour.

## D-26 — Logging grades can disappear at compile time

**Decision:** the outer logging API may be macros so disabled grades eliminate the entire statement including argument evaluation.

**Why:** constrained builds should pay zero runtime/code-size cost for disabled verbose diagnostics.

## D-27 — Function tracing is RAII

**Decision:** very verbose builds may use an RAII scope tracer to record function entry and exit.

**Why:** scope lifetime automatically pairs entry/exit across early returns and gives developers/agents a powerful bug reconstruction trace.

Modern implementation should use stack lifetime and `std::source_location`.

## D-28 — Real-time logs preserve event timing

**Decision:** diagnostic records use a monotonic timestamp captured when the event is emitted, before slow output.

**Why:** hardware/real-time failures may need to be replayed with timing preserved. Delta-from-previous-record may be printed, but is derived from event timestamps.

## D-29 — Logging output is serialized through bounded transport

**Decision:** callers do not concurrently scribble directly to the physical output.

Host builds may serialize with a mutex or logging thread. Embedded builds may use a fixed-capacity SPSC ring drained by an ISR/scheduler into a serial port.

**Why:** complete ordered records are more important than allowing every context to own the device.

The SPSC publication rules must follow the modern C++ memory model.

## D-30 — Avoid embedded frameworks for small infrastructure

**Decision:** prefer small local implementations for straightforward bounded embedded infrastructure such as logging transport.

**Why:** small audited code is easier to port and reason about than introducing a large OSS dependency solely to obtain a queue/logger abstraction.

## D-31 — Terminal fallback is a typed const-borrowed provider

**Decision:** Map takes a `FallbackT` template parameter constrained by the `LayerFallbackProvider` concept (`src/world/layer_fallback.hpp`) and borrows one provider object as `const`, owning nothing. The provider answers `value<LayerT>(world_position) -> exactly LayerT::value_type` for every Map layer; absence is not an outcome and no `std::optional`/`std::expected` is involved. The old vague `Generator&` placeholder is not part of the contract, and the provider has no public Map accessor.

**Why:** the operation Map actually needs at the end of per-layer resolution is small and total: procedural baseline generation and fixed layer defaults both reduce to "give me the untouched baseline value". Pinning that exact, always-answering shape keeps the provider ignorant of Cache, Patch, Placement and Storage, makes the value type a compile-time-checked property of the layer (no implicit conversion at the seam), and lets a build or layer mix deterministic generation and constants behind one seam.

## D-32 — Map preserves lower-level error domains

**Decision:** checked Map access returns `MapResult<T>` (`src/world/map_result.hpp`). `MapError` is `std::variant<MapErrorCode, LayerSourceError, AuthoredLayerSourceError, RuntimeLayerSourceError>`: Map adds only its own local outcomes (`OutOfBounds`, `CacheFull`) as a small enum and preserves the exact lower-level source errors rather than flattening all failures into one large Map enum. The fourth alternative (D-36) keeps the same shape for the runtime role: `.layer` parser/read failures remain the exact existing `LayerSourceError` regardless of source role, authored Patch/source geometry failures remain `AuthoredLayerSourceError`, and runtime Map/source geometry failures are `RuntimeLayerSourceError` — no wrapper duplication of `LayerSourceError`. `storage::Error` never appears directly; the layer source reader already maps Storage failures to `LayerSourceError::StorageFailed`.

**Why:** the seam that failed is exactly what the caller needs to react to. Flattening would lose the distinction between "coordinate outside the Map", "bounded cache cannot take another slot yet", a specific `.layer` parse/read diagnosis and a specific Patch/source geometry diagnosis. Keeping each seam's error vocabulary authoritative avoids a duplicate enum that would have to be kept in sync as the lower domains grow.

## D-33 — Placement changes invalidate the whole resident Cache until transformed coverage exists

**Decision:** any successful `Map::set_position()`, `set_rotation()`, `set_reflection()`, or `set_orientation()` call invalidates the entire resident Cache through `Cache::invalidate_all()`, not just the coordinates the moved or re-oriented Patch covers.

**Why:** the exact affected region is the union of the Patch's old and new transformed footprints, and the transformed-Patch-coverage code does not exist yet. Approximating that coverage risks leaving stale resident layer data behind, and inventing a transformed-bounds policy would guess at a mapping seam that is still an explicit stop condition. The bounded Cache has no eviction or write-back policy and only holds explicitly populated data, so discarding all resident data is the simple, safe choice. This is a current implementation decision, not a permanent limit: once transformed Patch coverage exists, finer invalidation should replace it.

## D-34 — Authored precedence follows PlacementId, not array slot order

**Decision:** when several Placements cover the same world coordinate, authored resolution is ordered by stable Placement identity: a higher `PlacementId` has higher precedence, so a later successful placement overlays an earlier one. The rule is per-layer — a Placement only participates in a layer it binds — and it follows the monotonic id, not the array-slot order a Placement happens to occupy. A Placement declines a cell, letting lower Placements and then the terminal fallback continue, when its Patch has no binding for the layer, the world coordinate is outside its transformed Patch, or the authored cell carries no contribution (ASCII space `0x20`).

**Why:** `PlacementId`s start at one, increase monotonically and are never reused, so "later successful placement" is already represented by the stable identity the game code carries. Making the id the precedence key removes a hidden dependency on internal array layout, gives story-facing code a predictable answer about which occurrence wins, and matches the intended authored-overwrite reading of overlap. Per-layer participation keeps a missing authored layer distinct from an unsupported layer: a Placement that does not bind a layer hides nothing, and a space cell is authored absence, not a runtime zero.

## D-35 — Runtime overlay identity is MapId + LayerId

**Decision:** the logical identity of a runtime/materialised `.layer` source is `(MapId, LayerId)`:

```text
one Map
    × one Layer
        = at most one logical runtime overlay source
```

The runtime source covers the Map's complete logical `Area`, with its geometry pinned to that area:

```text
P = map.area().min()
D = map.area().max() - map.area().min() + 1
```

Source-local coordinates are therefore `world - map.area().min()`, with source-local `(0, 0)` equal to the Map's minimum world coordinate. No Placement transform and no Patch participates in the identity or the geometry: runtime state is world-oriented. Runtime identity is independent of `CacheChunkSide` and of Cache residency; the Cache is temporary residency, not persistence identity.

Map carries the binding catalogue as a borrowed `std::span<const RuntimeLayerBinding>` (`src/world/runtime_layer_source.hpp`) scoped to the Map instance; a binding `{ layer, source }` means `(Map::id(), layer) -> runtime SourceId source`. The catalogue precondition is asserted in the Map constructor: every binding names a layer supported by the Map type, and a `LayerId` appears at most once in the span. There is no precedence between duplicate bindings; a duplicate is invalid configuration, not "first wins" or "last wins". Zero bindings means the Map currently has no persistent runtime overlay source for those layers; it does not mean the layer is unsupported. `validate_runtime_layer_source()` checks an already-parsed layout against the Map area with wide signed intermediates: it never uses `Area::width()`/`height()` (narrow full extents wrap them) and never narrows the source `P` into the Map coordinate type first, so an out-of-range position reports a mismatch instead of wrapping.

**Why:**

- persistence identity must not depend on the current Cache chunk size, which is an implementation choice and may change independently of saved-world identity;
- moving or replacing authored Placements must not move runtime state: the runtime overlay belongs to Map world coordinates, not to the authored object that originally supplied a value;
- two Placements sharing a Patch must remain independent in world state: a runtime change under one occurrence persists through the Map's layer source, never through the shared Patch's authored source;
- the existing dense `.layer` format and direct addressing remain usable for the runtime role without new physical assumptions;
- the runtime backing source may be much larger than RAM without being loaded, rewritten or materialised in whole, and Storage remains free to represent the logical source however its backend requires.

The decision pins the **logical** source. It does not require a filesystem backend to allocate or load the complete object in RAM, and it does not pin physical file naming, `SourceId` allocation/registration, file creation, or write-back policy.

## D-36 — Runtime materialized state precedes authored and fallback state

**Decision:** in checked Map reads, the implemented resolution order is:

```text
resident Cache
    -> runtime/materialized overlay
    -> authored Placements, highest PlacementId first
    -> terminal fallback
```

A non-space runtime cell is the authoritative current world value for its cell: it outranks every authored Placement and the terminal fallback. A space runtime cell (`0x20`) contributes nothing — it is "this source contributes nothing here" — so the cell continues to authored Placements and then to the fallback. There is exactly one logical runtime source per `(MapId, LayerId)` (D-35), so there is no runtime-vs-runtime precedence. Precedence is per-cell, not per-source or per-Chunk: within one canonical Chunk, some cells may resolve from the runtime overlay, others from authored state, and others from the fallback.

For one missing layer Chunk the bound runtime source is opened and geometry-validated exactly once, before any per-cell reading; the per-cell loop then issues bounded `read_cells()` reads through the shared reader, never touching Storage directly. Cache padding cells outside `Map::area()` are never runtime-read, authored-read or fallback-generated: they remain value-initialized.

A missing binding (`runtime_binding<LayerT>() == nullptr`) is normal absence: no runtime overlay exists for that Map layer, and authored/fallback resolution is exactly as before. A present binding whose source cannot be opened, parsed or geometry-validated is a real checked-access failure — the exact `LayerSourceError` or `RuntimeLayerSourceError` propagates instead of falling through to authored state. When the runtime overlay resolves every logical cell of the requested Chunk, no authored source is opened and the fallback is never queried; when it resolves only some cells, authored resolution sees only the still-unresolved cells through the existing resolved-mask seam.

Runtime sources are read-only from Map's perspective in this decision: it pins read resolution only. No mutation, dirty state, write-back, runtime file creation or `SourceId` allocation is introduced.

## D-37 — A dirty resident plane is never silently discarded

**Decision:** the Cache tracks one dirty bit per resident layer plane per spatial slot, not per cell. A changed `Cache::set<LayerT>(position, value)` marks the whole resident layer plane dirty; a same-value set is a no-op on a clean plane and a plane that is already dirty stays dirty. There is no flush, clear or write-back API: dirty state persists until the Cache is destroyed. `fill<LayerT>()` refuses to overwrite a dirty target plane and returns `false` without writing. Both invalidation forms are atomic and return a `bool`: if any slot that would be discarded contains a dirty resident plane, the entire operation is refused and the Cache remains unchanged; a dirty slot outside the invalidated area does not block `invalidate(area)`.

`Map::set<LayerT>(position, value)` is the first mutable world-state path: checked coordinate, then `ensure_resident<LayerT>()` installs a clean plane when the Chunk is missing, then the Cache mutates the resident value and marks the plane. No Storage write happens. Dirty state is not a `MapError`: checked access never fails because state is dirty, so the dirty resident value is authoritative for subsequent reads of that Chunk without rereading runtime, authored or fallback state.

Placement creation and transform mutation consume the refusal in their own management error domains: `place()` validates the Patch and capacity first, then performs the whole-cache invalidation before assigning a `PlacementId` and constructing the Placement, and a refused invalidation fails with `MapPlacementError::DirtyState` without creating a Placement or consuming an id; `set_position()`, `set_rotation()`, `set_reflection()` and `set_orientation()` reject unknown ids with `MapPlacementMutationError::UnknownPlacement`, treat a no-op request as a success without invalidation (it succeeds even while dirty state exists), and otherwise perform the whole-cache invalidation before mutating the Placement, failing with `MapPlacementMutationError::DirtyState` and leaving the Placement unchanged when it is refused.

**Why:**

- the bounded Cache holds explicitly populated resident state and has no eviction, flush or write-back policy yet (an explicit stop condition, not an omission to paper over); discarding a mutated value to make room would silently lose working world state, so the safe default is refusal, not replacement;
- per-plane granularity matches the unit the Cache actually fills and invalidates: a plane is installed whole by `fill()` and read whole by `value<LayerT>()`/`tile()`, and per-cell dirty masks would be speculative bookkeeping with no consumer until a write-back slice exists;
- once dirty, a plane stays dirty because there is no operation that could legitimately remove the dirt; a sticky bit makes every consumer's decision (fill, invalidate, placement mutation) total without any flush bookkeeping;
- atomic all-or-nothing refusal keeps the Cache state machine simple: a caller that checks and then acts sees a consistent Cache either before or after the operation, never a partially invalidated one;
- surfacing the refusal in the placement-management error domains (`MapPlacementError::DirtyState`, `MapPlacementMutationError::DirtyState`) rather than in `MapError` keeps checked world access honest — reading or mutating one live cell is never wrong because other state is unflushed — while still making the blockage deterministic and visible to the Placement lifecycle, including the no-op transform case, which needs no invalidation at all.
