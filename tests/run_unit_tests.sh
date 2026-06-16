#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="$ROOT_DIR/tests/unit"
BUILD_DIR="$ROOT_DIR/target/unit-tests"

mkdir -p "$BUILD_DIR"

if [[ ! -d "$TEST_DIR" ]]; then
    echo "Unit test directory not found: $TEST_DIR" >&2
    exit 1
fi

mapfile -t TEST_SOURCES < <(find "$TEST_DIR" -maxdepth 1 -name 'test_*.c' -type f | sort)

if [[ ${#TEST_SOURCES[@]} -eq 0 ]]; then
    echo "No unit tests found in $TEST_DIR" >&2
    exit 1
fi

CFLAGS=(
    -std=c11
    -Wall
    -Wextra
    -Werror
    -O2
    -DOPENOS_UNIT_TEST=1
    -I"$TEST_DIR"
)

PASS_COUNT=0
FAIL_COUNT=0

for source in "${TEST_SOURCES[@]}"; do
    name="$(basename "${source%.c}")"
    binary="$BUILD_DIR/$name"
    log="$BUILD_DIR/$name.log"

    echo "[UNIT] build $name"
    gcc "${CFLAGS[@]}" "$source" "$TEST_DIR/unit_test.c" -o "$binary"

    echo "[UNIT] run   $name"
    if "$binary" >"$log" 2>&1; then
        cat "$log"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        status=$?
        cat "$log" >&2 || true
        echo "[UNIT] FAIL $name status=$status" >&2
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
    echo
done

if [[ $FAIL_COUNT -ne 0 ]]; then
    echo "[UNIT] failed: $FAIL_COUNT, passed: $PASS_COUNT" >&2
    exit 1
fi

echo "[UNIT] all tests passed: $PASS_COUNT"
