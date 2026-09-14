#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/transient/pipeline3/builds/default}"

if [[ ! -f "$BUILD_DIR/CTestTestfile.cmake" ]]; then
    echo "check-tests: build tree is not configured for CTest: $BUILD_DIR" >&2
    echo "Run tools/checks/check-build.sh first." >&2
    exit 2
fi

ctest --test-dir "$BUILD_DIR" --output-on-failure
