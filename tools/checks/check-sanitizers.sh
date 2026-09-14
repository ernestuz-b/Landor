#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/transient/pipeline3/builds/sanitize}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DENABLE_SANITIZERS=ON

cmake --build "$BUILD_DIR" -j

ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}" \
UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}" \
ctest --test-dir "$BUILD_DIR" --output-on-failure
