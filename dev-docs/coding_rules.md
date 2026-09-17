# Landor Coding Rules

These are durable engineering rules for modifying Landor.

Formatting and naming live in [`coding_style.md`](coding_style.md). Architecture lives in
[`DESIGN_STATE.md`](DESIGN_STATE.md) and rationale in
[`DESIGN_DECISIONS.md`](DESIGN_DECISIONS.md).

## 1. Work in small, reviewable slices

Before editing:

- identify the requested behaviour;
- identify the relevant source and tests;
- read nearby architectural comments;
- check `../STATUS.md` for known mismatches.

Do not:

- rewrite unrelated code;
- perform opportunistic refactors;
- introduce abstractions for hypothetical future use;
- mix broad formatting changes with logic changes;
- weaken tests, warnings or diagnostics to make a patch pass.

After editing, state:

- what changed;
- why;
- exact tests/builds run;
- known limitations or remaining mismatches.

## 2. Use modern C++ deliberately

Landor is C++23.

Prefer language/library facilities that make the contract clearer:

- RAII;
- `constexpr`;
- `consteval`;
- concepts;
- `std::array`;
- `std::span`;
- `std::expected`;
- scoped enums;
- `[[nodiscard]]`;
- `static_assert`;
- `std::source_location`;
- named strong/policy types.

"Modern" is not permission for template acrobatics. Cleverness that hides ownership,
allocation, generated code cost, control flow or failure semantics is a regression.

## 3. No exceptions

Landor code does not throw or catch exceptions.

Recoverable outcomes are explicit values/result types.

External development libraries such as GoogleTest/GoogleMock may use their normal
implementation facilities; that does not make exceptions part of Landor.

Do not make normal Landor control flow depend on exception-oriented accessors such as
`std::expected::value()`.

## 4. No RTTI

Do not use:

- `dynamic_cast`;
- `typeid`;
- RTTI-driven dispatch.

Use build selection, templates/concepts, explicit tags/ids or runtime polymorphism according
to when the concrete choice is known.

## 5. Ownership and lifetime

Ownership must be visible.

Prefer:

- values;
- stack lifetime;
- RAII;
- stable ids/handles for relocatable/dynamic objects;
- non-owning `std::span`/views for borrowed contiguous data.

Rules:

- do not return references/pointers/spans to temporaries;
- document stored non-owning lifetime relationships;
- resolve movable managed objects at point of use rather than caching raw addresses;
- raw owning pointers require an explicit ownership policy.

`noexcept` is semantic. Use it where the function contract is genuinely non-throwing.

## 6. Allocation discipline

### Fixed first

If a useful maximum is known, prefer fixed-capacity storage.

If the capacity is fixed at compile time, it normally belongs in the type.

### Managed heap

Use `include/managed_heap/` only when dynamic allocation is genuinely unavoidable.

It is an imported component from another project. Do not modify it as part of ordinary
Landor cleanup or feature work. Changes require an explicit reason and separate review.

Use the component through its public API and documentation.

### Standard containers

Allocating STL containers are allowed only when their cost and allocator semantics are
appropriate.

Do not assume the managed heap can be supplied as an STL allocator merely because it
allocates memory. Relocation can invalidate the pointer stability expected by ordinary
containers.

Avoid hidden allocation in:

- real-time paths;
- embedded-critical paths;
- hot inner loops;
- error paths unless explicitly designed;
- deterministic paths where allocation timing/capacity would affect behaviour.

## 7. Compile-time capacities and policies

When a value affects layout/capacity and is fixed at compile time, make it a template or
non-type template parameter.

For several related values, prefer a named structural policy:

```cpp
struct MapCapacity
{
    std::size_t placements;
    std::size_t tile_cache;
};

template<MapCapacity Capacity>
class Map;
```

Do not hide object-layout decisions behind deep global config lookups.

## 8. Configuration

A contained application/global configuration object is allowed.

Deep subsystems must not access it directly. Interpret configuration at the composition
root and pass the exact value/policy/object needed.

CMake should use:

- compile definitions for genuine build switches;
- `configure_file()` for generated typed configuration;
- file/source selection for whole implementations known at build time.

Generated headers live under the build tree, not the source tree.

## 9. Platform selection

Use the least dynamic mechanism that matches reality.

### Known at build time

CMake selects the concrete platform file/type.

Example:

```cpp
using Storage = StorageFilesystem;
```

### Known through types

Use templates/concepts.

### Unknown until runtime

Use runtime polymorphism.

Do not add a virtual interface merely to abstract a build-known implementation.

Do not build platform-selector macro forests in common code.

Platform code may call C/vendor HAL APIs directly when appropriate. Add a C++ wrapper only
when Landor needs a semantic boundary.

## 10. Error handling

A failure is represented when the caller can meaningfully react.

Use:

- pointer/null for natural lookup absence;
- bool for a simple obvious binary outcome;
- `std::expected<T, E>` for value-or-recoverable-error;
- named status/result structures where they communicate the domain better.

Do not use `std::optional` as a generic error channel.

Mark important ignored-result hazards `[[nodiscard]]`.

Do not:

- silently discard errors;
- encode several distinct failures in an unexplained bool;
- continue after failed initialization with invalid state.

Keep error behaviour deterministic.

## 11. Invariants and assertions

Programming faults are not normal runtime failures.

Assert internal invariants in diagnostic builds.

Do not convert corrupted internal state into an ordinary recoverable result merely to keep
the program running.

In production, only add special fatal handling when it has meaningful semantics such as:

- safe-state transition;
- retained crash information;
- controlled reset.

Otherwise allow termination rather than inventing a decorative fatal framework.

## 12. Numeric safety

Treat conversions and arithmetic as design decisions.

Rules:

- avoid implicit narrowing;
- validate runtime ranges before narrowing;
- use C++ casts;
- avoid casual signed/unsigned mixing;
- document wrap, saturation and rounding contracts;
- never claim signed overflow is defined;
- test boundary and extreme values;
- make promotion width explicit where overflow is possible;
- use compile-time checks when a numeric relationship is static.

C-style casts are forbidden except at an unavoidable C/vendor boundary, and even there a
C++ cast is preferred when practical.

## 13. Concurrency

Concurrent code must have explicit ownership.

Prefer single-writer ownership and message passing.

Mutexes are fine for ordinary host shared data.

Atomics require a documented publication/ordering reason. Do not use relaxed atomics by
habit.

For SPSC queues:

- one context owns the write index;
- one context owns the read index;
- publication ordering must be valid in the C++ memory model;
- fixed capacity is preferred;
- overflow behaviour is explicit.

Do not hold locks across arbitrary callbacks or user code.

Shutdown must be deterministic.

## 14. Logging

Logging is instrumentation, not game state.

Logging may be globally reachable if it does not change program semantics.

Required direction:

- severity and category filtering;
- compile-time elimination of disabled grades;
- monotonic event timestamp;
- RAII function tracing at verbose grades;
- serialized physical output;
- fixed-capacity embedded transport;
- no required dynamic allocation.

On embedded targets, prefer a small local UART/ISR path over introducing a logging
framework.

## 15. Templates, concepts and compile-time code

Templates must remain readable.

Use concepts to express API constraints close to the public declaration.

Prefer:

- named types;
- structural policy objects;
- `if constexpr`;
- `constexpr` loops;
- `consteval` when runtime execution must be impossible.

Avoid recursive metaprogramming when normal compile-time code is clearer.

Use `static_assert` for compile-time architectural relationships.

"Viva la constness" is a project virtue, but do not distort an otherwise simple algorithm
solely to earn a `constexpr` badge.

## 16. Public/class API design

Landor is an application, not a library, but class interfaces still matter.

The public part of a class should be complete enough that another subsystem can use it
naturally without reaching into internals or inventing helpers.

Keep implementation details private.

Prefer explicit names and predictable overloads.

Make ownership, allocation, lifetime and expensive operations visible.

Do not minimize a public surface merely for the sake of minimalism if doing so makes the
class harder to use correctly.

## 17. Includes

Prefer direct includes for actual contracts.

Do not pursue forward declarations as a style goal. Use them when they materially reduce
coupling or solve a real include problem.

Every header must compile independently of accidental include order.

Fix cyclic dependencies structurally.

## 18. Warnings

Landor code must be warning-free with warnings treated as errors.

Enable aggressive GCC/Clang diagnostics as supported, including the families that catch:

- narrowing/conversion;
- signedness;
- shadowing;
- questionable casts;
- format errors;
- undefined preprocessor assumptions;
- implicit fallthrough;
- virtual-interface mistakes where applicable.

Typical baseline:

```text
-Wall
-Wextra
-Wpedantic
-Werror
```

and additional useful warnings such as:

```text
-Wconversion
-Wsign-conversion
-Wshadow
-Wformat=2
-Wundef
-Wcast-align
-Wcast-qual
-Wold-style-cast
-Wdouble-promotion
-Wimplicit-fallthrough
```

should be enabled where the compiler supports them.

Do not globally silence a warning because one piece of code is inconvenient.

Third-party code does not inherit Landor's warning policy.

## 19. Tests

Tests use GoogleTest. GoogleMock is allowed.

The test tree mirrors `src/`:

```text
src/world/coord.hpp
tests/world/test_coord.cpp
```

CMake lists tests explicitly.

Every bug fix should add a regression test when practical.

Test behaviour and invariants rather than private implementation.

Use compile-time tests for:

- concepts;
- type identity;
- capacity/policy relationships;
- `constexpr`/`consteval` contracts.

After modifying code:

- run relevant tests;
- run the full suite for cross-contract changes;
- build with warnings as errors.

Do not add arbitrary project-wide coverage percentages at this stage. Coverage targets may
be introduced later when they serve a concrete purpose.

## 20. CMake and build tree

Use target-based CMake.

Normal build root:

```text
transient/build
```

Keep build/generated files out of the source tree.

Use ordinary portable CMake that IDEs can consume without IDE-owned repository files.

Do not glob source/test files that form part of an explicit target contract.

## 21. Documentation discipline

Headers are architecture documentation.

Public/contract headers should explain:

- responsibility;
- ownership;
- lifetime;
- invariants;
- allocation;
- extension seams;
- why a tempting simpler-looking alternative is wrong when that knowledge matters.

Use `DESIGN_DECISIONS.md` for broader rejected alternatives/rationale.

Do not turn headers into chronological design diaries.

When an architectural change makes an existing substantial comment false, updating the
comment is part of the code change.

The same applies to relevant Markdown documentation.
