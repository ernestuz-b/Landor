# C++ Coding Style

This document defines how the code should look and read.

For engineering constraints, safety policy, testing policy, allocation discipline, and agent behavior, use `coding_rules.md`. This file only defines style, layout, naming, and project idioms.

Examples in this file use the project namespace `example_namespace`.

### Scope note (Landor)

For Landor, read `example_namespace` as the top-level namespace `landor`, with one
namespace per layer (`landor::world`, `landor::game`, `landor::render`,
`landor::actor`, `landor::npc`, `landor::host`) and internals under `::detail`.

Landor is a **dependency-free C++20** project. Every mention of Qt in this file refers to
conventions, never to a dependency: *"Qt-like brace style"* names a brace placement, and
Qt Creator is supported as an IDE that reads the CMake project (`CMAKE_EXPORT_COMPILE_COMMANDS=ON`).
No Qt type appears in Landor APIs or data.

Qt Creator compatibility is a property of the build files, not of the sources: plain,
conventional CMake that any IDE can load, with no IDE-specific files or variables, and
build directories following `../AGENTS.md` (`transient/pipeline3/builds/<variant>`).

Unit tests use **GoogleTest**, as required by the repository workflow
(`../AGENTS.md`); see §16 for naming.

Engineering constraints, allocation discipline and error policy live in
[`coding_rules.md`](coding_rules.md); design decisions and their rationale live in
[`IMPLEMENTATION.md`](IMPLEMENTATION.md). This file states only how code should read.

## 1. Style Goals

Code should be readable, deterministic, testable, numerically explicit, easy to review, and difficult to misuse.

The public API should be pleasant to use. The implementation should be conservative.

## 2. Formatting

Use Qt-like brace style for functions, classes, namespaces, and control blocks.

This section is machine-encoded in [`.clang-format`](.clang-format) (`IndentWidth: 4`,
`UseTab: Never`, `ColumnLimit: 100`, custom brace wrapping reproducing the example below,
project headers grouped last); run it rather than formatting by hand. The formatter does
not enforce the "braces on every control block" rule below — that is a review and
clang-tidy concern.

```cpp
namespace example_namespace {

class MatrixView
{
public:
    constexpr MatrixView() = default;

    constexpr auto rows() const noexcept -> std::size_t
    {
        return m_rows;
    }

private:
    std::size_t m_rows = 0;
};

} // namespace example_namespace
```

Use braces for all control blocks, even one-line bodies.

```cpp
if (pivot == Scalar{}) {
    return LinearStatus::Singular;
}
```

## 3. Indentation and Line Length

Use:

```text
4 spaces
no tabs
preferred line length: 100 columns
hard maximum: 120 columns
```

Break long expressions for clarity.

```cpp
const auto residual =
    norm(multiply(a, x) - b);
```

For long template declarations, split parameters vertically when useful.

```cpp
template <
    std::size_t Rows,
    std::size_t Cols,
    typename Scalar>
class StaticMatrix;
```

## 4. File Naming and Headers

Use lowercase snake case for file names.

Good:

```text
static_matrix.hpp
dynamic_matrix.hpp
numeric_traits.hpp
joseph_update.hpp
```

Bad:

```text
StaticMatrix.hpp
numericTraits.hpp
JosephUpdate.hpp
```

Header files use:

```cpp
#pragma once
```

Every public header must be self-contained.

```cpp
#include <example/core/bcache_model.hpp>

int main()
{
}
```

Standard-library headers and headers belonging to installed third-party libraries, found through the include path, shall be included using angle brackets. Headers belonging to the current project shall be included using quotation marks.

Examples:

```cpp
// Inside the project itself
#include "detail/storage.hpp"

// A system include
#include <cstdio>
```

C++ only headers should have the 'hpp' extension.

## 5. Namespaces

All public symbols live under:

```cpp
namespace example_namespace {
}
```

Implementation details live under:

```cpp
namespace example_namespace::detail {
}
```

Never expose `detail` symbols as part of the public API.

Use closing namespace comments.

```cpp
} // namespace example_namespace
```

## 6. Naming Conventions

Enum value casing follows [`coding_rules.md`](coding_rules.md) §Naming: scoped enums use
PascalCase values (`OpKind::PaveRoad`, `Mark::Remembered`), unscoped enums use `ALL_CAPS`.
Flag sets that combine with `|` are ordinary lowercase `snake_case` constants rather than
enums.

Use `PascalCase` for public types.

```cpp
StaticMatrix
DynamicMatrix
SmallMatrix
LinearResult
SolvePolicy
MatrixStructure
ScalarDomain
```

Use `snake_case` for functions.

```cpp
solve()
inverse()
determinant()
condition_estimate()
rank_estimate()
elem_exp()
matrix_exp()
lu_decompose()
cholesky_decompose()
```

Use `snake_case` for local variables and function parameters.

```cpp
const auto pivot_row = find_pivot_row(a, column);
```

Private member variables use `m_`.

```cpp
std::size_t m_rows = 0;
std::size_t m_cols = 0;
std::vector<Scalar> m_data;
```

Use `snake_case` for `constexpr` constants.

```cpp
inline constexpr auto default_tolerance = 1.0e-6;
```

Avoid macro constants.

`ALL_CAPS` is therefore reserved for the preprocessor. Configuration knobs are ordinary
lowercase `snake_case` constants (`landor::cfg::cell_bits`) — this deliberately matches
the generated config header described in `IMPLEMENTATION.md` §8, where `configure_file`
makes macro-versus-constant confusion likely.

No member prefix other than `m_`. Never use `_Foo` (leading underscore plus uppercase) or
`__foo`: both are reserved to the implementation, so such names are undefined behaviour
rather than a style choice.

Use `struct` for passive data — all members public, no invariants, brace-initialised at
compile time — which covers tile descriptors, render cells and terrain-operation records.
Use `class` as soon as a type owns an invariant or behaviour (`World`, `Game`, renderers).

## 7. Source Layout

The project uses a standard include/src split.

Rules:

- public headers live under `include/example/`;
- implementation (.cpp) lives under `src/<subsystem>/`;
- every public header must be self-contained;
- prefer `.hpp`/`.cpp` splits for non-trivial code;
- trivially inline functions may remain in headers;
- avoid global mutable state;
- avoid ODR hazards.

## 8. Includes

Include only what is needed.

Good:

```cpp
#include <array>
#include <cstddef>
#include <type_traits>

#include "example/core/result.hpp"
```

Bad inside an internal library header:

```cpp
#include "example/example.hpp"
```

Public headers must not depend on include order.

Avoid cyclic includes. If a cycle appears, fix the dependency structure.

## 9. Use of `auto`

Use `auto` when the type is obvious or irrelevant.

Good:

```cpp
auto result = StaticMatrix<Rows, Cols, Scalar>{};
```

Prefer explicit scalar types when conversion or precision matters.

```cpp
const Scalar pivot = a(row, col);
```

Avoid:

```cpp
auto pivot = a(row, col);
```

when the scalar type matters.

## 10. Const Correctness

Use `const` aggressively.

```cpp
constexpr auto rows() const noexcept -> std::size_t
{
    return Rows;
}
```

Prefer immutable locals unless mutation is required.

```cpp
const auto row_count = a.rows();
```

Do not reuse variables for different meanings.

## 11. Control Flow Style

Prefer simple control flow and early validation.

```cpp
if (!is_square(a)) {
    return LinearStatus::DimensionMismatch;
}

if (!is_supported_scalar<Scalar>) {
    return LinearStatus::UnsupportedScalarDomain;
}

return detail::solve_square(a, b);
```

Avoid deeply nested logic, `goto`, and complex side effects inside conditions.

## 12. Error Result Style

Failure examples should preserve diagnostics.

Good:

```cpp
if (pivot_abs <= tolerance) {
    return LinearResult<Matrix>{
        .value = {},
        .status = LinearStatus::Singular,
        .condition_estimate = {},
        .residual_norm = {},
        .pivot_min_abs = pivot_abs
    };
}
```

Bad:

```cpp
if (pivot_abs <= tolerance) {
    return {};
}
```

## 13. Numerical API Naming

Prefer solve over inverse when solving systems.

```cpp
auto x = solve(a, b);
```

Avoid:

```cpp
auto x = inverse(a) * b;
```

Distinguish elementwise and matrix functions.

```cpp
elem_exp(a)
matrix_exp(a)
elem_pow(a, 2)
matrix_power(a, 2)
```

Unsafe numerical paths must be explicit.

```cpp
auto inv = inverse(a, SolvePolicy::FastUnchecked);
```

## 14. Public API Style

Prefer free functions for mathematical operations.

```cpp
solve(a, b)
inverse(a)
transpose(a)
determinant(a)
```

Use member functions for object properties and direct access.

```cpp
matrix.rows()
matrix.cols()
matrix.data()
matrix.size()
```

Avoid APIs that imply hidden mutation unless mutation is explicit.

```cpp
auto b = transpose(a);
transpose_in_place(a);
```

Operators are allowed for ordinary matrix arithmetic.

```cpp
a + b
a - b
a * b
scalar * a
```

Avoid operators for operations where mathematical meaning is domain-dependent or ambiguous.

## 15. Comments

Comments should explain why, not repeat what the code says.

Good:

```cpp
// Use pivoted LU by default. The no-pivot path is only valid when the caller
// explicitly accepts the risk through SolvePolicy::FastUnchecked.
```

Bad:

```cpp
// Increment i.
++i;
```

Every non-trivial numerical algorithm should include a short algorithm block.

```cpp
// Algorithm: LU decomposition with partial pivoting
// Domain: floating-point and complex floating-point
// Structure: general square matrix
// Complexity: O(N^3)
// Allocation: none for StaticMatrix; workspace-controlled for DynamicMatrix
// Failure: returns Singular or NearSingular on poor pivots
```

## 16. Test Naming Style

Landor uses **GoogleTest**. Test names still describe behaviour: the suite name encodes
the module and unit under test, the test name states the observed behaviour.

Good:

```cpp
TEST(WorldCell, ExposesHeightAndOverlayFields)
TEST(ChunkStorage, ReadsUnpatchedTileFromCompressedBase)
TEST(TerrainOp, RejectsExcavationOfUnbreakableBedrock)
TEST(RenderFrame, BuildsViewWithOneTileHalo)
TEST(StaticMatrix, MultiplicationDoesNotAllocate)
```

The last line shows the intended form for any test inherited from the source examples of
this document; `TEST_CASE(...)` syntax belongs to another framework and is not used here.

Bad:

```cpp
TEST(CellTest, Test1)
TEST(world, cell)
```

## 17. Final Rule

When style and safety conflict, choose safety.

When cleverness and readability conflict, choose readability.

When speed and numerical correctness conflict, choose numerical correctness by default and expose the fast unsafe path explicitly.
