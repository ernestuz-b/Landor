# Landor C++ Coding Style

This document defines how Landor code should look and read.

Engineering constraints live in [`coding_rules.md`](coding_rules.md).

## 1. General style

The code should be:

- explicit;
- readable;
- const-correct;
- easy to review;
- easy to debug;
- unsurprising about allocation and ownership.

The project borrows useful conventions from Qt-era C++ style but has no Qt runtime
dependency.

## 2. Formatting

`.clang-format` is the formatting authority.

The intended brace style puts declaration **and control-flow** braces on the next line:

```cpp
namespace landor::geo
{

class Patch
{
public:
    void example()
    {
        if (condition)
        {
            do_something();
        }
        else
        {
            do_something_else();
        }

        for (const auto& item : items)
        {
            use(item);
        }
    }
};

} // namespace landor::geo
```

Always use braces for control blocks, including one-line bodies.

Use:

```text
4 spaces
no tabs
100-column normal limit
```

Break long expressions where it improves reading.

## 3. Files and directories

Use lowercase snake_case file names:

```text
storage_contract.hpp
storage_filesystem.cpp
design_helpers.hpp
```

Use `.hpp` for C++ headers and `.cpp` for non-trivial out-of-line implementation.

Default rule:

- templates and compile-time contracts live in headers;
- tiny obvious accessors may be inline;
- non-trivial implementation goes in `.cpp` when it does not need to be visible for
  templates/compile-time use.

The physical directory tree does **not** have to mirror namespaces.

`src/world/` may contain `landor::geo` and `landor::world`.

## 4. Headers

Use:

```cpp
#pragma once
```

Every header should include what it needs directly.

Prefer direct includes over extensive forward declarations.

Order includes in formatter-managed groups:

1. C/system headers where applicable;
2. C++ standard/external installed headers;
3. current-project headers.

Project headers use quotes. Standard/external installed headers use angle brackets.

## 5. Namespaces

Public project symbols live under `landor` with meaningful subsystem namespaces, for
example:

```cpp
landor::geo
landor::world
landor::storage
landor::render
```

Do not maintain a fixed master list of allowed namespaces. Add a subsystem namespace when
the model needs one.

Implementation-only helpers may use a local `detail` namespace when useful.

Always use a closing namespace comment.

## 6. Type and identifier naming

### Types

PascalCase:

```cpp
StorageFilesystem
LayerBinding
PatchSetRole
MapCapacity
```

### Functions and methods

snake_case:

```cpp
natural_position()
placement_count()
set_orientation()
```

### Variables and parameters

snake_case:

```cpp
source_size
natural_position
placement_id
```

### Private data members

Prefix with `m_`:

```cpp
MapId m_id;
area_type m_area;
```

### Compile-time constants

lowercase snake_case:

```cpp
inline constexpr std::size_t default_capacity = 64;
```

Avoid macro constants.

## 7. Enums

Use scoped enums by default.

Scoped enum values use **PascalCase**, following the Qt convention:

```cpp
enum class Error : std::uint8_t
{
    None,
    InvalidSource,
    OutOfRange,
    ReadOnly,
    NoSpace,
    ReadFailed,
    WriteFailed
};
```

```cpp
enum class Rotation : std::uint8_t
{
    None,
    Clockwise90,
    Clockwise180,
    Clockwise270
};
```

Do not use snake_case enum values.

## 8. `struct` versus `class`

Use `struct` for passive data whose public members are the representation and which has no
invariant requiring encapsulation.

Use `class` when the type owns behaviour/invariants or controls mutation.

This is a semantic choice, not a rule about object size.

## 9. `const`, `constexpr` and `consteval`

Prefer immutable locals.

Use `const` aggressively when a value does not change.

Use `constexpr` wherever compile-time and runtime use are both natural.

Use `consteval` when runtime execution would be a misuse.

Examples:

```cpp
[[nodiscard]] constexpr PatchId id() const noexcept
{
    return m_id;
}

template<Layer LayerT>
[[nodiscard]] static consteval bool supports() noexcept
{
    return (...);
}
```

Do not reuse a variable for a different meaning merely to avoid declaring another const
local.

## 10. `noexcept`

`noexcept` documents a semantic promise.

Use it where that promise is meaningful, especially for:

- simple accessors;
- value operations;
- destructors;
- moves where applicable;
- low-level/platform operations that are intentionally non-throwing.

Do not mechanically append `noexcept` to every function merely because this project builds
without exceptions.

## 11. Casts

Use C++ casts:

```cpp
static_cast<T>(value)
```

Do not use C-style casts in Landor code except at an unavoidable C/vendor interop boundary.

Never cast merely to silence a warning without first understanding the conversion.

## 12. `auto`

Use `auto` when the type is obvious from the expression or the exact spelling is noise.

Prefer an explicit type when:

- integer width matters;
- signedness matters;
- conversion is part of the reasoning;
- ownership/lifetime is clearer with the explicit type.

## 13. Control flow

Prefer shallow control flow and early rejection.

Good:

```cpp
if (!contains(position))
{
    return false;
}

const auto* placement = find_placement(id);
if (placement == nullptr)
{
    return false;
}

return apply_change(*placement);
```

Avoid clever compound conditions with side effects.

Avoid `goto`.

## 14. Comments

Comments explain **why**, ownership and invariants; they do not narrate obvious syntax.

Use:

- `//` for a single-line comment;
- `/* ... */` for a multi-line implementation comment;
- `///` or `/** ... */` for Doxygen/public API documentation.

Public/architectural type comments should explain:

- what the type represents;
- what it owns;
- what it borrows;
- lifetime assumptions;
- invariants;
- how it composes with neighbouring types;
- important rejected alternatives when knowing them prevents misuse.

Example:

```cpp
/**
 * One live occurrence of an authored Patch.
 *
 * Placement owns only identity and transform. Authored layer data remains in
 * Patch. Spatial mutation goes through Map so derived caches cannot become
 * stale.
 */
```

Do not turn source comments into a chronological design diary. Broader rationale belongs
in `DESIGN_DECISIONS.md`.

## 15. Public class surfaces

Landor is not being designed as a general-purpose library, so do not contort classes to
minimize an exported ABI.

Still, a class's public part should be complete and pleasant enough that another subsystem
can use it without reaching into internals.

Expose the concepts callers genuinely need. Hide policy/representation that they do not.

## 16. Includes versus forward declarations

Prefer the direct include when the type is genuinely part of the contract.

Forward-declare when it provides a concrete coupling/build benefit.

Do not forward-declare merely because a style guide says fewer includes are always better.

## 17. Error/result code at call sites

Keep recoverable control flow visible:

```cpp
auto result = storage.read(source, offset, destination);

if (!result)
{
    return std::unexpected(result.error());
}
```

For `std::expected`, prefer explicit checks, dereference and `.error()` over `.value()`.

Simple lookup absence may remain simple:

```cpp
if (const auto* binding = patch.binding(layer))
{
    use(*binding);
}
```

## 18. Logging call style

The final logger is not implemented yet, but expected call sites look like normal
instrumentation:

```cpp
LANDOR_LOG_INFO("patch loaded");
LANDOR_TRACE_FUNCTION();
```

`LANDOR_TRACE_FUNCTION()` is intentionally RAII-backed and may compile to nothing at
disabled grades.

Do not write game logic that depends on whether a log call executes.

## 19. Tests

GoogleTest names describe behaviour:

```cpp
TEST(Coord32, Neighbour8CoversEveryOctantExactlyOnce)
TEST(Area32, IntersectionReturnsEmptyForDisjointAreas)
TEST(StorageFilesystem, RejectsUnknownSource)
```

GoogleMock is appropriate when the interaction itself is the contract.

Avoid generic names such as:

```cpp
TEST(CoordTest, Test1)
```

Test files mirror the source tree and use `test_` prefixes:

```text
src/world/coord.hpp
tests/world/test_coord.cpp
```

## 20. Final preference order

When rules compete:

1. correctness;
2. clear ownership and control flow;
3. portability;
4. readability;
5. measured performance;
6. cleverness.

Prefer code the next person can safely modify.
