# Coding Rules

Follow these rules when modifying this project.

This file defines how coding agents should work. For formatting, naming, and layout, use `coding_style.md`.

## 1. Work in Small Slices

Implement one small, reviewable change at a time.

Before editing:

- identify the requested behavior;
- locate the relevant files and tests;
- check nearby code style and project rules.

Do not rewrite unrelated code.
Do not perform opportunistic refactors.
Do not mix formatting-only changes with logic changes.
Do not weaken tests, warnings, assertions, diagnostics, or static-analysis checks to make a patch pass.

After editing, summarize:

- what changed;
- what was tested;
- any known limitations.

## 2. Prefer Simple, Explicit C++

Use modern C++ when it improves safety, clarity, or compile-time checking.

Preferred tools:

- `constexpr` / `consteval`
- `std::array`
- `std::span`
- `std::optional`
- `std::variant` when appropriate
- concepts for API constraints
- `enum class`
- RAII
- strong types or tag types
- `[[nodiscard]]`
- `static_assert`

Avoid clever code that hides ownership, allocation, control flow, failure modes, or generated-code cost.

## 3. Ownership and Lifetime

Ownership must be explicit.

Rules:

- prefer value types, stack objects, and RAII;
- use `std::span` for non-owning array views;
- use `std::unique_ptr` only when dynamic ownership is intentional;
- avoid shared ownership unless genuinely required;
- do not return references, pointers, or spans to temporaries;
- document lifetime assumptions when storing non-owning pointers or references;
- destructors must not throw.

- keep a **stable identifier** distinct from a runtime storage index: identifiers may be
  persisted, compared across runs and hashed; slot indices exist only inside one process
  and must never reach a save file, a digest, or an NPC's long-lived memory;
- resolve a handle to storage at the point of use rather than caching the resolved object,
  so that storage maintenance (see `IMPLEMENTATION.md` §2.3) can move objects safely;
- distinguish kinds by an explicit tag or id when no shared polymorphic interface is
  needed; runtime type queries are unavailable (§13).

## 4. Error Handling

Failure must be visible to the caller.

Rules:

- distinguish recoverable errors from programming faults;
- mark important result/status types `[[nodiscard]]`;
- do not silently discard errors;
- avoid vague boolean failures when the error kind matters;
- use assertions for violated internal invariants, not normal runtime failures;
- keep error paths deterministic;
- do not continue with invalid state after failed initialization.

Use one consistent error model inside a subsystem.

## 5. Numeric Safety

Treat numeric conversions and arithmetic as design decisions.

Rules:

- avoid implicit narrowing;
- validate range before converting runtime values;
- avoid casual signed/unsigned mixing;
- document saturation, wraparound, and rounding behavior;
- check overflow where it matters;
- do not compare floating-point values for exact equality unless the domain guarantees it;
- use tolerances based on scale, type, and conditioning;
- test boundary, zero, singular, near-singular, and dimension-mismatch cases.
- Use the sympy-math-oracle skill to validate math results and tests *(inherited tooling;
  Landor's v0.x rules are integer-only, so it normally does not apply — it becomes
  relevant only if a phase introduces real-valued or probabilistic math)*.

## 6. Allocation Discipline

Hidden allocation is forbidden in deterministic paths.

Rules:

- prefer fixed-capacity storage when maximum size is known;
- use caller-provided workspace for reusable scratch memory;
- use the managed heap (`managed-heap/`, `IMPLEMENTATION.md` §2.4) as the only
  sanctioned store for variable-length dynamic objects in `world`/`game`; no other
  allocator or allocating container is allowed there;
- treat `managed-heap/` as an externally supplied, frozen component: never modify
  `managed_heap.hpp`, `SPEC.md`, `USER_GUIDE.md`, or the examples; consume the
  public API per its user guide, and record gaps or defects for the component's
  owner instead of patching the component;
- make allocation visible in the API or documentation;
- do not allocate in real-time, embedded-critical, or hot inner-loop paths unless explicitly allowed;
- do not allocate in error paths unless the subsystem policy allows it;
- test no-allocation guarantees where practical.

## 7. Concurrency

Concurrent code must be bounded and reviewable.

Rules:

- prefer single ownership and message passing;
- use mutexes for ordinary shared data;
- use atomics only with documented memory-ordering reasons;
- do not hold locks across callbacks, blocking calls, or user code;
- define lock ordering when multiple locks exist;
- avoid detached threads;
- make shutdown deterministic.

## 8. Templates, Traits, and Concepts

Template code must remain understandable.

Rules:

- use concepts to express API constraints;
- use traits to describe facts, not run heavy computation;
- keep constraints close to public APIs;
- make unsupported combinations fail with clear diagnostics;
- prefer `if constexpr`, traits, tags, and named helper types over fragile overload tricks;
- avoid recursive template machinery when a `constexpr` loop is clearer.

When template code becomes fragile:

1. identify the logical objects;
2. capture them as named types;
3. pass those types to helpers;
4. compute through traits or static accessors;
5. materialize the result;
6. add a compile-time regression test.

## 9. Compile-Time Code

Use compile-time computation deliberately.

Rules:

- use `consteval` when runtime execution must be impossible;
- use `constexpr` when runtime execution is also acceptable;
- do not assume `constexpr` means compile-time only;
- materialize compile-time results into explicit values or types;
- bound compile-time work;
- avoid type explosions for large objects;
- keep compile-time tests small and focused.

## 10. Testing

Every public feature needs tests.

Rules:

- every bug fix should add a regression test when practical;
- test behavior and invariants, not implementation details;
- use compile-time tests for traits, concepts, dimensions, and invalid operations;
- use property or differential tests for algebraic and numerical code;
- keep benchmarks separate from correctness tests;
- use sanitizers where practical;
- do not weaken tests to pass the build;
- the framework is **GoogleTest**, configured through `-DX_GTEST_DIR` or FetchContent as
  described in `../AGENTS.md`; test naming follows `coding_style.md` §16;
- determinism is asserted, not assumed: identical scripted input must produce identical
  world digests, and any container iteration that can influence logic output or rendered
  state must have a defined order (`IMPLEMENTATION.md` §7.1).

For numerical code, test residuals and invariants, not only element-by-element equality.

## 11. CMake

Use target-based CMake.

Rules:

- avoid global include directories and global compiler flags;
- keep tests, examples, benchmarks, and tools as separate targets;
- export compile commands for LSP and static-analysis tools;
- register public headers explicitly;
- build directories stay under `transient/pipeline3/builds/<variant>` and `cmake --build`
  / `ctest` run *from* the build directory, per `../AGENTS.md` — snippets elsewhere in
  this document show `build/` only as shorthand;
- do not add required `.cpp` files to a header-only library core;
- keep optional dependencies from leaking into core public headers;
- the project must load unchanged in any IDE, including Qt Creator: conventional
  targets, cache variables for options, no absolute paths, no generator-specific logic,
  no IDE-owned files committed — compatibility comes from being ordinary CMake, not from
  IDE-specific configuration.

## 12. Public API Design

Public APIs should make correct use obvious and unsafe use explicit.

Rules:

- prefer explicit names over clever overloads;
- keep overload sets small and predictable;
- make ownership, allocation, and lifetime visible;
- distinguish mathematical, structural, and elementwise operations by name;
- avoid APIs that look cheap but perform expensive work;
- do not expose implementation policy types unless users need them.

## 13. Platform and Portability

Landor targets Linux PC, Raspberry Pi Zero, Raspberry Pi Pico (RP2044) and an STM32
Disco-class board. The microcontroller targets are **portable but not built daily**: the
working build is Linux, with periodic device builds. The consequence is that a target
constrains what core code may rely on, not which toolchain is used every commit.

Rules:

- core layers (`world`, `game`, `render`) build with `-fno-exceptions -fno-rtti`; host
  tooling (`host`, `tools/`, tests) may use exceptions;
- no `dynamic_cast`, `typeid`, or dependency on RTTI for dispatch;
- game logic time advances in **steps**. Wall-clock and millisecond values belong to the
  host layer for display and pacing only and never enter rules, saves, or digests;
- core code avoids facilities that are unavailable or heap-hungry on small targets:
  formatting and stream I/O live in `host` (`std::format` included), while `world`/`game`
  use integer arithmetic, fixed buffers and explicit accessors;
- colours are palette indices agreed between the frame builder and the renderer, never
  colour classes from a GUI toolkit;
- a core-only build (`-std=c++20 -fno-exceptions -fno-rtti`, excluding `host`) runs in CI
  so portability drift is caught in hours rather than at the next device build;
- impossible memory configurations should fail at configure time, not on hardware nobody
  has attached (`IMPLEMENTATION.md` §8).

## 14. Forbidden Patterns

Avoid:

- broad rewrites for local bugs;
- generic abstractions before real repeated use exists;
- macros for algorithm dispatch or type logic;
- hidden global configuration that changes behavior unpredictably;
- unchecked casts used to silence warnings;
- raw owning pointers without policy;
- dangling views or references;
- hidden heap allocation in deterministic paths;
- weakening tests, warnings, assertions, or diagnostics;
- mixing formatting-only changes with logic changes.
