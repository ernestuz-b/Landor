# Landor — Project Status

This file describes **what exists now**, not the final architecture.

Reviewed against repository head:

```text
3fb53801631e641c49f07e9525f71055eaf6af71
Mostly moving old tests to gtest.
```

The source tree is ahead of several older design documents. The documentation refresh
containing this file is intended to remove that drift.

## Current repository state

### Build

The current `CMakeLists.txt` builds:

- `Landor`;
- `landor_tests`.

GoogleTest v1.18.0 is fetched through CMake `FetchContent`.

At the reviewed head, CMake still declares C++20 and explicitly disables GoogleMock. Those
settings predate the latest design decisions and are listed under **Pending alignment**
below.

### Tests

The latest commit migrated the old hand-written geometry tests into GoogleTest and removed
the duplicate `tests/test_geo_headers.cpp`.

The current test target contains:

- `tests/test_world_headers.cpp`;
- `tests/test_coord.cpp`;
- `tests/test_area.cpp`.

The repository's latest recorded run reports 53 discovered passing tests.

The final project rule is that tests mirror the source tree, so these files should
eventually move to:

```text
tests/world/test_coord.cpp
tests/world/test_area.cpp
```

with CMake updated explicitly. That move has not happened yet.

### Current source contracts

The substantive contracts currently under `src/` are:

```text
src/storage/
    types.hpp
    storage_contract.hpp

src/platform/storage/
    storage_filesystem.hpp

src/world/
    coord.hpp
    area.hpp
    layer.hpp
    orientation.hpp
    tile.hpp
    patch.hpp
    placement.hpp
    patchset.hpp
    place.hpp
    map.hpp

    cache.hpp       placeholder
    chunk.hpp       placeholder
```

`src/renderers/utf-8/` exists but is not yet implemented.

The managed heap is under:

```text
include/managed_heap/
```

and includes its own spec, user guide, examples and implementation header.

## World architecture represented in source

The current headers establish the following model.

### `landor::geo::Layer`

A layer is a type-level description of a spatial property. It has a stable `LayerId` and a
`value_type`. It owns no storage or simulation policy.

### `landor::geo::Patch`

An immutable reusable authored region.

A patch has:

- stable `PatchId`;
- authored name;
- natural position;
- local rectangular extent;
- zero or more `LayerBinding { LayerId, storage::SourceId }` entries.

Missing authored data for a layer is represented by absence of a binding, not by an
invented invalid id.

### `landor::geo::Placement`

One live occurrence of a patch.

It owns:

- `PlacementId`;
- `PatchId`;
- current position;
- current `Orientation`.

Transform order is:

```text
reflection -> rotation -> translation
```

Placement is deliberately unaware of narrative/story identity.

### `landor::geo::PatchSet`

An immutable composition recipe with roles, patch candidates and minimum/maximum counts.

Selection/composition is deterministic policy outside `PatchSet`.

### `landor::world::Place`

A story-facing identity over one or more `PlacementId`s.

Actors, schedules, ownership, dialogue and missions should be able to refer to a stable
place without knowing which authored patch or transform implements it.

### `landor::geo::Tile`

A synthetic value.

`Tile<Layers...>` contains owned copies of the resolved values for the layers supported by
that map/build. Modifying the returned tile does not mutate the map.

There is no authoritative stored array of complete tiles.

### `landor::geo::Map`

The logical spatial surface.

The current contract says that `Map`:

- owns live placements;
- keeps authored patch descriptors by non-owning span;
- resolves layers independently;
- synthesizes complete tile values;
- may use a disposable synthetic tile cache;
- routes placement mutation through `Map` so derived cached answers can be invalidated.

The implementation of `value()`, `at()`, placement mutation and storage integration is
still pending.

## Storage architecture represented in source

`landor::storage::StorageBackend` is a compile-time concept with logical-object operations:

```text
read(SourceId, Offset, span<byte>)
write(SourceId, Offset, span<const byte>)
size(SourceId, Size&)
```

Operations are whole-range success/failure. Paths, file handles, sectors, pages, erase
blocks and other physical details remain below the boundary.

`StorageFilesystem` is the current concrete host implementation contract.

It exposes:

```cpp
using Storage = StorageFilesystem;
```

The build is intended to select the file that provides the concrete `Storage` alias.

## Pending alignment after the latest design discussion

These are **known mismatches**, not invitations to re-design the architecture.

### C++23

Current CMake and `.clang-format` still say C++20.

Required direction:

- C++23 project;
- supported portable subset validated on supported embedded toolchains;
- `std::expected` available for recoverable value-or-error results;
- aggressive use of `constexpr`/`consteval` where it improves clarity.

### Exceptions and RTTI

Required direction:

- no exceptions in Landor code;
- no RTTI;
- no `dynamic_cast`;
- no `typeid`;
- GoogleTest/GoogleMock themselves may use their normal implementation facilities.

CMake should eventually enforce the Landor-side contract.

### GoogleMock

Current CMake sets `BUILD_GMOCK OFF`.

Required direction: GoogleMock is allowed and should not be forcibly disabled.

### Warning policy

Required direction: aggressive compiler warnings, warning-free Landor code, warnings as
errors.

Third-party dependencies should not inherit Landor's warning policy.

### Formatting

Current `.clang-format` attaches braces to control statements.

Required direction:

```cpp
if (condition)
{
    ...
}

for (...)
{
    ...
}

while (...)
{
    ...
}
```

`.clang-format` should be changed so the formatter itself is authoritative.

### Enum values

Some current enums still use lowercase values, for example the storage `Error` enum.

Required direction: scoped enum values use PascalCase, following the Qt naming convention:

```cpp
Error::InvalidSource
Rotation::None
Dir::East
```

### Error/result style

`Map::place()` currently returns `std::optional<PlacementId>`.

The latest design direction avoids `std::optional` as a general failure mechanism.
Capacity exhaustion is a normal bounded-system outcome and should eventually use the
clearest domain representation; `std::expected` is preferred when a value and meaningful
recoverable reason are both needed.

Do not churn APIs merely to remove `optional`; change them when implementing the actual
contract and the better domain result is clear.

### Storage seam in `Map`

`map.hpp` currently forward-declares `Storage` in `landor::geo`, while the storage
contract now lives in `landor::storage`.

The intended seam is for common code to use the build-selected `landor::storage::Storage`
type. This still needs to be wired cleanly.

### Test layout

Current geometry tests are GoogleTest but still live directly under `tests/`.

Final rule: mirror `src/` under `tests/`.

### Managed heap policy

The managed heap is an imported component from another project.

Current rule:

- use fixed-capacity/value storage first;
- use managed heap only when dynamic allocation is genuinely unavoidable;
- do not modify the component casually;
- defects or required changes should be handled explicitly and separately;
- whether it eventually becomes a submodule/subrepo is still undecided.

### Logging

No Landor logger is implemented yet.

The intended design is recorded in `DESIGN_STATE.md` and `DESIGN_DECISIONS.md`: compile-out
logging grades, RAII function tracing, monotonic event timestamps and serialized host/UART
output through bounded transport.

## Near-term code work

A sensible sequence from the current tree is:

1. align CMake and `.clang-format` with the settled project rules;
2. move tests into the mirrored `tests/world/` layout;
3. normalize scoped enum spelling as affected code is touched;
4. wire `Map` to the build-selected storage type;
5. implement and test the filesystem storage backend;
6. implement the first real `Map` resolution slice without prematurely adding cache or
   procedural-generation machinery;
7. add logging only when a real diagnostic consumer needs it.

Keep each step small and independently testable.
