#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/transient/pipeline3/builds/static}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
CMAKE_OPT_CLANG_TIDY="${CMAKE_OPT_CLANG_TIDY:-ENABLE_CLANG_TIDY}"
CMAKE_OPT_CPPCHECK="${CMAKE_OPT_CPPCHECK:-ENABLE_CPPCHECK}"

MODE="full"
CHECK_CLANG_TIDY=1
CHECK_CPPCHECK=1
TARGET_FILE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --file)
            MODE="file"
            TARGET_FILE="${2:?missing value for --file}"
            shift 2
            ;;
        --all)
            MODE="full"
            shift
            ;;
        --clang-tidy)
            CHECK_CLANG_TIDY=1
            CHECK_CPPCHECK=0
            shift
            ;;
        --cppcheck)
            CHECK_CLANG_TIDY=0
            CHECK_CPPCHECK=1
            shift
            ;;
        *)
            echo "check-static: unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

cmake_args=(
    -S "$PROJECT_DIR"
    -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

if [[ "$CHECK_CLANG_TIDY" -eq 1 ]]; then
    cmake_args+=("-D${CMAKE_OPT_CLANG_TIDY}=ON")
fi
if [[ "$CHECK_CPPCHECK" -eq 1 ]]; then
    cmake_args+=("-D${CMAKE_OPT_CPPCHECK}=ON")
fi

cmake "${cmake_args[@]}"
cmake --build "$BUILD_DIR" -j

cd "$PROJECT_DIR"
HELPER="$BUILD_DIR/tools/run_static_checks.sh"
if [[ -f "$HELPER" ]]; then
    # shellcheck disable=SC1090
    source "$HELPER"
fi

files=()
if [[ "$MODE" == "file" ]]; then
    files+=("$TARGET_FILE")
else
    while IFS= read -r file; do
        [[ -n "$file" ]] && files+=("$file")
    done < <(git ls-files -- '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx')
fi

status=0

if [[ "$CHECK_CLANG_TIDY" -eq 1 ]]; then
    echo "== clang-tidy =="
    if declare -f run_clang_tidy >/dev/null 2>&1; then
        if ! run_clang_tidy "${files[@]}"; then
            status=1
        fi
    elif command -v clang-tidy >/dev/null 2>&1; then
        for file in "${files[@]}"; do
            echo "clang-tidy: $file"
            if ! clang-tidy "$file" -p "$BUILD_DIR" 2>&1; then
                status=1
            fi
        done
    else
        echo "check-static: clang-tidy not found" >&2
        status=1
    fi
fi

if [[ "$CHECK_CPPCHECK" -eq 1 ]]; then
    echo "== cppcheck =="
    if declare -f run_cppcheck >/dev/null 2>&1; then
        if ! run_cppcheck "${files[@]}"; then
            status=1
        fi
    elif command -v cppcheck >/dev/null 2>&1; then
        if [[ "$MODE" == "full" ]]; then
            if ! cppcheck \
                --project="$BUILD_DIR/compile_commands.json" \
                --enable=warning,performance,portability,style \
                --inline-suppr \
                --suppress=missingIncludeSystem \
                --error-exitcode=1 \
                2>&1; then
                status=1
            fi
        else
            if ! cppcheck \
                --enable=warning,performance,portability,style \
                --inline-suppr \
                --suppress=missingIncludeSystem \
                --error-exitcode=1 \
                "$TARGET_FILE" \
                2>&1; then
                status=1
            fi
        fi
    else
        echo "check-static: cppcheck not found" >&2
        status=1
    fi
fi

if [[ "$status" -eq 0 ]]; then
    echo "Static analysis: PASSED"
else
    echo "Static analysis: FAILED" >&2
fi

exit "$status"
