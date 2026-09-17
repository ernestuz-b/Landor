# Landor Agent Instructions

This file is the operational entry point for coding agents working on Landor.

Do not treat it as a replacement for the design documents. Its job is to tell you how to approach the repository without accidentally undoing deliberate architecture.

## Read before editing

For any non-trivial change, read in this order:

1. `STATUS.md` — current repository state, known mismatches and current work;
2. the relevant source headers and tests;
3. `dev-docs/DESIGN_STATE.md` — intended architecture;
4. `dev-docs/DESIGN_DECISIONS.md` — settled choices and rationale;
5. `dev-docs/MAPPING_MODEL.md` when working on Map/Tile/Region/Cache/Chunk or world-data residency;
6. `dev-docs/coding_rules.md`;
7. `dev-docs/coding_style.md`.

If an existing design looks odd, **do not immediately replace it with a more standard pattern**. Search nearby comments and the design-decision document first. Several unusual choices are deliberate consequences of embedded targets, relocation, deterministic world generation or build-time platform selection.

## Source and documentation authority

For implementation reality, source and tests are authoritative.

For intended architecture, `DESIGN_STATE.md` and `DESIGN_DECISIONS.md` are authoritative.

For the detailed mapping seam, `MAPPING_MODEL.md` records the current Map/Tile/Region/Cache/Chunk model and must agree with the relevant source headers.

If they disagree:

- do not silently reconcile them;
- identify the mismatch;
- follow the requested task and the most recent explicit design decision;
- update stale documentation in the same patch when the architectural contract changes.

`STATUS.md` is operational state, not architectural law.

## Work in small slices

Make one reviewable change at a time.

Before editing:

- state the requested behaviour or contract;
- identify the smallest set of relevant files;
- inspect nearby tests and comments;
- identify the invariants that must remain true.

Do not:

- perform opportunistic refactors;
- introduce abstractions for hypothetical future use;
- reformat unrelated files;
- weaken tests, warnings, assertions or diagnostics to make a patch pass;
- change an imported component as collateral damage.

After editing, report:

- what changed;
- why;
- exact tests/builds run;
- any remaining mismatch or limitation.

## Architecture guardrails

### Platform selection

Use the simplest form of polymorphism that matches when the choice is known:

- known at build time: CMake selects the concrete source/header and common code uses a descriptive alias such as `using Storage = StorageFilesystem;`;
- known through the C++ type system: templates/concepts;
- genuinely unknown until runtime: runtime polymorphism.

Do not introduce virtual interfaces, manual vtables or `#if PLATFORM_...` forests for a choice already known by the build system.

### Filesystem layout is not namespace layout

Directories group code by project responsibility. Namespaces describe semantic ownership.

`src/world/` intentionally contains world/environment classes from more than one namespace, including `landor::geo` and `landor::world`. Do not reorganize the tree merely to mirror namespaces.

### World model

Keep these distinctions intact:

- `Layer`: type-level spatial property;
- `Patch`: immutable authored reusable content;
- `Placement`: one live positioned/oriented occurrence of a Patch;
- `PatchSet`: immutable composition recipe;
- `Place`: story-facing identity over Placements;
- `Chunk`: aligned cache/I/O unit for one layer;
- `Cache`: bounded resident store that remains layer-oriented internally;
- `Tile`: compact owned value containing one coordinate plus copied layer properties;
- `Map`: logical spatial surface, owner of live Placements, and public boundary hiding Cache/Storage machinery;
- `Region`: intended future game/simulation working-area concept, not yet a settled implemented API.

A missing authored layer is not the same as an unsupported layer.

Do not turn Cache into a resident `Tile[]` merely because Map returns Tiles. Cache stores layer planes; Tile is packed only when presented to callers.

`tile[LayerTag]` accesses a value already inside the Tile. It must not become a proxy operation back into Map or Cache.

### Dynamic allocation

Prefer fixed-capacity/value storage.

Use `include/managed_heap/` only when genuine dynamic allocation is unavoidable. It is an imported component from another project. Do not modify it casually; changes require an explicit task and separate review.

Do not assume the relocatable managed heap can be passed to an STL container as an allocator. Ordinary containers generally expect allocated storage addresses to remain valid.

### Configuration

Global/application configuration is acceptable when contained.

Deep subsystems must not reach into a global config object. Interpret configuration at the composition/root level and pass the specific value or policy the subsystem needs.

If a value is fixed at compile time and changes object layout/type, make it a template parameter. For several related values, prefer a named structural compile-time policy when that is clearer than positional values.

CMake may:

- use compile definitions for genuine build switches;
- generate typed configuration headers with `configure_file`;
- select whole implementation files when the implementation is known at build time.

## C++ policy

Landor is C++23.

Use modern C++ when it clarifies the contract: `constexpr`, `consteval`, concepts, `std::span`, `std::expected`, RAII, `std::source_location`, strong/scoped enums and compile-time validation are welcome.

Do not use exceptions in Landor code.

Do not use RTTI, `dynamic_cast` or `typeid`.

`noexcept` is semantic: use it when the function is contractually non-throwing, not as mechanical decoration.

Use C++ casts. Do not use C-style casts except at an unavoidable C/vendor boundary.

Prefer direct includes over aggressive forward-declaration games. Typing less and making the dependency explicit is a feature.

## Errors and invariants

Normal alternative outcomes are not automatically errors.

Use the representation that makes the domain clear:

- pointer/null for a simple lookup when absence is natural;
- bool when there are exactly two obvious outcomes;
- `std::expected<T, E>` when a caller needs a value or a meaningful recoverable reason;
- named result/status types when they better express the subsystem.

Because Landor does not use exceptions, do not build normal control flow around `std::expected::value()`.

Programming faults and broken internal invariants are not recoverable result values.

Assert them in diagnostic builds. In production, only add special fatal handling when it has real semantics (safe state, retained crash information, reset policy, etc.); otherwise termination is preferable to decorative fatal wrappers.

## Mapping-specific stop conditions

Do not invent policy merely to complete a Map/Cache task.

In particular, stop and report the missing contract if implementation would require guessing:

- how `(source, layer, spatial coordinate)` maps to a Storage byte range;
- Cache replacement/eviction policy;
- dirty-state/write-back policy;
- procedural-state materialisation policy;
- final Tile/Map mutation semantics;
- final Region ownership/view semantics.

The current Cache deliberately fails a new-area fill when all spatial slots are occupied. Do not replace that with LRU/FIFO/random eviction without an explicit design decision.

## Logging and diagnostics

Logging is permitted as globally reachable instrumentation because it must not affect domain semantics.

The intended logging design is:

- severity plus diagnostic category;
- monotonic timestamp captured at the logging event;
- timing suitable for replaying real-time/hardware failures;
- RAII function entry/exit tracing at the most verbose grade;
- disabled grades removable at compile time, including argument evaluation;
- fixed-capacity transport on constrained targets;
- serialized output;
- host sink may use a logging thread;
- embedded sink may use a single-producer/single-consumer ring and UART/ISR consumer;
- no dynamic allocation required by the logging path.

Do not import an embedded logging framework for this.

## Tests

Use GoogleTest; GoogleMock is allowed by project policy.

Tests mirror `src/`:

```text
src/world/cache.hpp
tests/world/test_cache.cpp
```

Every bug fix should add or strengthen a regression test when practical.

After changing code, at minimum:

1. build the affected target;
2. run the relevant GoogleTests;
3. run the full test suite when the change crosses contracts;
4. build Landor with warnings treated as errors once the warning policy is wired into the target.

Do not claim completion from inspection alone.

## Warnings

Landor is expected to build warning-free with aggressive GCC/Clang warnings and warnings-as-errors.

Suppress a warning only when:

- the code is intentionally correct;
- the warning cannot reasonably be avoided without making the code worse;
- the suppression is narrow;
- the reason is documented.

Third-party code such as GoogleTest does not need to inherit Landor's warning policy.

## Documentation is part of the change

Headers are architectural documents.

When a change alters ownership, lifetime, invariants, extension seams, platform selection or another architectural contract, update the relevant comments and Markdown documents in the same patch.

A substantial architectural comment that has become false is a bug.
