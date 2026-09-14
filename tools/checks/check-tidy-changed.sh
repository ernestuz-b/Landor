#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/transient/pipeline3/builds/static}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
CMAKE_OPT_CLANG_TIDY="${CMAKE_OPT_CLANG_TIDY:-ENABLE_CLANG_TIDY}"

MODE="changed"
TARGET_FILE=""
BASE_REF="${PIPELINE3_BASE_REF:-HEAD}"

usage() {
    cat <<'EOF'
Usage:
  tools/checks/check-tidy-changed.sh [--changed BASE_REF]
  tools/checks/check-tidy-changed.sh --file FILE
  tools/checks/check-tidy-changed.sh --all
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --changed)
            MODE="changed"
            BASE_REF="${2:?missing value for --changed}"
            shift 2
            ;;
        --file)
            MODE="file"
            TARGET_FILE="${2:?missing value for --file}"
            shift 2
            ;;
        --all)
            MODE="all"
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "check-tidy-changed: unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -D"${CMAKE_OPT_CLANG_TIDY}=ON"

cmake --build "$BUILD_DIR" -j

HELPER="$BUILD_DIR/tools/run_static_checks.sh"
if [[ -f "$HELPER" ]]; then
    # shellcheck disable=SC1090
    source "$HELPER"
fi

if declare -f run_clang_tidy >/dev/null 2>&1; then
    run_tidy() {
        run_clang_tidy "$@"
    }
else
    if ! command -v clang-tidy >/dev/null 2>&1; then
        echo "check-tidy-changed: clang-tidy not found and no generated helper is available" >&2
        exit 127
    fi

    run_tidy() {
        local status=0
        local file
        for file in "$@"; do
            [[ -f "$PROJECT_DIR/$file" ]] || [[ -f "$file" ]] || continue
            echo "clang-tidy: $file"
            if ! clang-tidy "$file" -p "$BUILD_DIR" 2>&1; then
                status=1
            fi
        done
        return "$status"
    }
fi

cd "$PROJECT_DIR"
files=()

case "$MODE" in
    file)
        files+=("$TARGET_FILE")
        ;;
    all)
        while IFS= read -r file; do
            [[ -n "$file" ]] && files+=("$file")
        done < <(git ls-files -- '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx')
        ;;
    changed)
        git rev-parse --verify "$BASE_REF^{commit}" >/dev/null
        while IFS= read -r file; do
            [[ -n "$file" ]] && files+=("$file")
        done < <(git diff --name-only --diff-filter=ACMR "$BASE_REF" -- '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx')
        ;;
esac

if [[ "${#files[@]}" -eq 0 ]]; then
    echo "clang-tidy: no matching files to check"
    exit 0
fi

run_tidy "${files[@]}"
