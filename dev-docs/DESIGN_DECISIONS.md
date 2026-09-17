# Landor — Design Decisions

This is a compact record of important **settled** architectural choices and why they were
made.

It is not a diary and it does not record every idea considered.

When an implementation choice looks unusual, check here before replacing it with a more
conventional pattern.

## D-01 — C++23, portable subset verified by target toolchains

**Decision:** Landor is a C++23 project.

**Why:** One purpose of the project is to use and relearn modern C++ rather than freezing
the code at an older language version.

**Constraint:** "C++23" does not mean every library feature is assumed present on every
embedded compiler. Facilities used by portable core code must be validated on the
supported embedded toolchains.

## D-02 — No exceptions

**Decision:** Landor code does not use exceptions.

**Why:**

- control flow should remain visible at the call site;
- exception catch boundaries are easy to place at the wrong architectural level;
- small embedded targets benefit from avoiding exception runtime machinery;
- explicit result types are a better match for recoverable domain outcomes.

Recoverable outcomes are values/results. Broken invariants are programming faults.

External test frameworks may internally use exceptions; that does not make exceptions part
of Landor's design.

## D-03 — No RTTI

**Decision:** Landor does not use RTTI for architecture or dispatch.

No `dynamic_cast` or `typeid`.

**Why:** Runtime type discovery is unnecessary when most variation is already known at
build time or through the type system.

## D-04 — Choose polymorphism by when the choice is known

**Decision:**

- build-time choice -> CMake selects a concrete implementation;
- compile-time type variation -> templates/concepts;
- runtime-unknown choice -> virtual/runtime polymorphism.

**Why:** Virtual dispatch solves a real problem when the concrete type is unknown at
runtime. Using it for a build-known device/storage backend adds machinery without adding
information.

Storage is the first concrete example.

## D-05 — Build-selected concrete platform aliases

**Decision:** platform implementations use descriptive concrete names and the selected
implementation exposes the common alias.

Example:

```cpp
using Storage = StorageFilesystem;
```

**Why:** Common code sees one clear type, while another coder can still see which concrete
implementation exists in the selected platform file.

Do not build preprocessor selector mazes in common code.

## D-06 — Filesystem directories do not mirror namespaces

**Decision:** project directories group responsibilities; namespaces express semantic
ownership.

`src/world/` may contain both `landor::geo` and `landor::world`.

**Why:** forcing directory and namespace hierarchies to mirror each other creates churn
without improving the model.

## D-07 — Layer-oriented world, synthetic tiles

**Decision:** authoritative map state remains layer-oriented.

`Map::at()` synthesizes an owned `Tile` value from independently resolved layers.

**Why:**

- unrelated layers have different sources/lifecycles;
- authored absence must not mean capability absence;
- simulations can materialize state independently;
- a complete stored tile structure would couple systems that do not need coupling.

A full-tile cache is allowed only as disposable derived state.

## D-08 — Patch is authored content; Placement is live geography

**Decision:** immutable authored data and live occurrence state are separate types.

**Why:** one authored house/barn/well/etc. should be reusable at several coordinates and
orientations without copying authored layer data.

Transform order is:

```text
reflection -> rotation -> translation
```

## D-09 — Place is narrative identity

**Decision:** story systems use `PlaceId` rather than `PatchId`.

**Why:** "the player's home" or "GoodMagePalace" should remain the same narrative place
even when its physical implementation uses several placements or changes geometry.

## D-10 — PatchSet is a recipe, not an owner or generator

**Decision:** `PatchSet` describes roles/candidates/counts. Composition policy is separate.

**Why:** authored content, composition recipe and deterministic generation algorithm have
different responsibilities and should be independently replaceable.

## D-11 — Storage uses opaque logical sources

**Decision:** geography addresses storage through `SourceId`, `Offset` and `Size`.

**Why:** patches/maps should not know whether a source is a path, SD-card file, flash
region or NAND object.

No physical storage geometry leaks into authored world descriptors.

## D-12 — Fixed capacity becomes type information

**Decision:** if a value is fixed at compile time and affects layout/capacity, make it a
template/non-type template parameter.

When several related capacities exist, prefer a named structural policy object.

**Why:** the dependency stays visible, compiler checks relationships, and there is no
hidden global capacity lookup in low-level code.

## D-13 — Dynamic allocation is exceptional

**Decision:** prefer bounded value/fixed storage. Use the managed heap only when genuine
dynamic allocation is unavoidable.

**Why:** predictable memory is important on embedded targets and usually makes ownership
clearer on hosts too.

The managed heap is imported from another project and is not to be casually edited.

## D-14 — Managed heap is not automatically an STL allocator

**Decision:** allocating standard containers require an allocator whose semantics actually
match the container.

**Why:** the managed heap may relocate objects; standard containers normally retain
addresses into their allocation. "It allocates memory" is not enough to make it a valid
allocator.

## D-15 — Contained global configuration is allowed

**Decision:** application-level global configuration is acceptable when contained and
preferably read-only after initialization.

Deep subsystems do not reach into it.

**Why:** passing the entire application configuration through every layer is busywork, but
hidden deep configuration access becomes a service locator and destroys local reasoning.

Pass the specific value or policy a subsystem needs.

## D-16 — CMake selects the configuration mechanism

**Decision:**

- simple build switch -> compile definition;
- typed generated values -> `configure_file`;
- whole build-known implementation -> selected source/header.

**Why:** these are different kinds of configuration and should not be forced into one
mechanism.

## D-17 — Recoverable failure is explicit; `std::expected` is preferred when rich

**Decision:** use domain-specific direct outcomes for simple cases and `std::expected<T,E>`
when a caller needs either a value or a meaningful recoverable reason.

`std::optional` is not the default error mechanism.

**Why:** optional often obscures the reason something did not happen and spreads
presence-check ceremony through code.

Simple natural absence may still be represented by `nullptr` or a bool.

## D-18 — Invariants are not recoverable errors

**Decision:** programming faults and corrupted internal invariants are asserted in
diagnostic builds rather than returned as ordinary domain failures.

Production adds a special fatal path only when the platform has meaningful semantics such
as safe-state entry, retained diagnostics or reset policy.

**Why:** pretending corrupted state is a recoverable business outcome makes code continue
after its assumptions are already false.

## D-19 — Full-throttle warnings

**Decision:** Landor code should compile warning-free under aggressive compiler warnings,
with warnings treated as errors.

**Why:** narrowing, signedness, shadowing, bad casts and portability assumptions are cheap
to catch at compile time and expensive on embedded hardware.

Third-party dependencies do not inherit this policy.

## D-20 — `noexcept` is semantic

**Decision:** use `noexcept` when a function genuinely promises non-throwing behaviour;
do not stamp it everywhere mechanically.

**Why:** declarations should document semantics and remain easy to graft/reuse in other
projects.

## D-21 — Prefer direct includes

**Decision:** include the header that defines a contract rather than aggressively
forward-declaring everything.

**Why:** explicit dependencies are easier to read and usually require less ceremony.
Forward declarations are still valid when they materially reduce coupling.

## D-22 — Headers are architecture documentation

**Decision:** public/architectural headers explain responsibility, ownership, lifetime,
invariants, rationale and extension seams.

**Why:** the next developer should be able to learn how a class belongs in the system from
the declaration.

Historical rationale that would otherwise clutter headers belongs here.

## D-23 — GoogleTest and GoogleMock

**Decision:** tests use GoogleTest. GoogleMock is allowed when interactions are what the
test needs to specify.

**Why:** concrete fakes are often clearer for state behaviour; mocks are useful for
interaction contracts. Host development resources are not a meaningful constraint.

## D-24 — Test tree mirrors source tree

**Decision:**

```text
src/world/coord.hpp
tests/world/test_coord.cpp
```

**Why:** test ownership remains obvious as the tree grows.

CMake lists test sources explicitly rather than globbing.

## D-25 — Logging is globally reachable instrumentation

**Decision:** logging may be global/cross-cutting provided it cannot alter domain
semantics.

**Why:** forcing a logger through every constructor adds noise without improving ownership
of game state.

Configuration is different: deep code must not use a global configuration object to
change behaviour.

## D-26 — Logging grades can disappear at compile time

**Decision:** the outer logging API may be macros so disabled grades eliminate the entire
statement including argument evaluation.

**Why:** constrained builds should pay zero runtime/code-size cost for disabled verbose
diagnostics.

## D-27 — Function tracing is RAII

**Decision:** very verbose builds may use an RAII scope tracer to record function entry and
exit.

**Why:** scope lifetime automatically pairs entry/exit across early returns and gives
developers/agents a powerful bug reconstruction trace.

Modern implementation should use stack lifetime and `std::source_location`.

## D-28 — Real-time logs preserve event timing

**Decision:** diagnostic records use a monotonic timestamp captured when the event is
emitted, before slow output.

**Why:** hardware/real-time failures may need to be replayed with timing preserved.
Delta-from-previous-record may be printed, but is derived from event timestamps.

## D-29 — Logging output is serialized through bounded transport

**Decision:** callers do not concurrently scribble directly to the physical output.

Host builds may serialize with a mutex or logging thread. Embedded builds may use a
fixed-capacity SPSC ring drained by an ISR/scheduler into a serial port.

**Why:** complete ordered records are more important than allowing every context to own the
device.

The SPSC publication rules must follow the modern C++ memory model.

## D-30 — Avoid embedded frameworks for small infrastructure

**Decision:** prefer small local implementations for straightforward bounded embedded
infrastructure such as logging transport.

**Why:** small audited code is easier to port and reason about than introducing a large OSS
dependency solely to obtain a queue/logger abstraction.
