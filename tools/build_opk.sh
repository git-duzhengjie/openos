#!/usr/bin/env bash
#
# build_opk.sh : build the host-side .opk packager (opkg-build), its unit
# test (test_opk), and run a self-contained smoke test.
#
# This is host-only tooling (system cc, hosted libc). Invoked by:
#     ./build.sh opkg
# or directly:
#     bash tools/build_opk.sh
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

OUT_DIR="build/host"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

mkdir -p "$OUT_DIR"

CC="${CC:-cc}"
CFLAGS="-std=c11 -Wall -Wextra -Werror -O2"

echo "[opk] compiling opkg-build ..."
$CC $CFLAGS -o "$OUT_DIR/opkg-build" tools/opkg-build.c

echo "[opk] compiling test_opk ..."
$CC $CFLAGS -o "$OUT_DIR/test_opk" tools/test_opk.c

echo "[opk] building smoke package ..."
printf 'hello from opk file one' > "$TMP_DIR/a.txt"
printf 'second payload content here!!' > "$TMP_DIR/b.bin"
"$OUT_DIR/opkg-build" -o "$TMP_DIR/demo.opk" -n demopkg \
    "$TMP_DIR/a.txt" -e "$TMP_DIR/b.bin:bin/prog"

echo "[opk] running unit test ..."
"$OUT_DIR/test_opk" "$TMP_DIR/demo.opk"

echo "[opk] OK: opkg-build and test_opk built, unit test ALL PASS"
