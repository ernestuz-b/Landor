# Documentation Refresh Manifest

This bundle was prepared against Landor head:

```text
3fb53801631e641c49f07e9525f71055eaf6af71
Mostly moving old tests to gtest.
```

It contains proposed replacements/additions:

```text
README.md
AGENTS.md
STATUS.md
dev-docs/DESIGN_STATE.md
dev-docs/DESIGN_DECISIONS.md
dev-docs/IMPLEMENTATION.md
dev-docs/coding_rules.md
dev-docs/coding_style.md
```

The bundle deliberately does **not** modify:

```text
docs/GAMEPLAY.md
include/managed_heap/*
```

`docs/GAMEPLAY.md` should be revisited separately because gameplay/content design is a
different editorial task from repairing the engineering/architecture documentation.

`include/managed_heap/` is treated as an imported component.

## Mechanical code/config changes implied by these documents

The docs describe settled direction that is not all implemented yet. The main follow-up
patches are:

1. CMake C++20 -> C++23;
2. enforce no exceptions / no RTTI for Landor code;
3. stop forcing `BUILD_GMOCK=OFF`;
4. aggressive warnings + `-Werror` for Landor targets;
5. `.clang-format`: control-statement braces on next line, C++23;
6. move geometry tests into `tests/world/`;
7. scoped enum values -> PascalCase as affected code is touched;
8. fix `Map` to use build-selected `landor::storage::Storage`;
9. reconsider `std::optional` result APIs when their implementation slice is written,
   preferring direct domain outcomes / `std::expected` where useful.

These should remain separate, reviewable changes rather than one documentation-driven
mega-refactor.
