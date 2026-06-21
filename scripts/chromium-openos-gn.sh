#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="${OPENOS_CHROMIUM_DEPS_DIR:-$ROOT/.openos-deps}"
CHROMIUM_ROOT="${OPENOS_CHROMIUM_ROOT:-$DEPS_DIR/chromium}"
CHROMIUM_SRC="$CHROMIUM_ROOT/src"
OVERLAY_DIR="$ROOT/ports/chromium-openos"
ARGS_FILE="$OVERLAY_DIR/args.openos-i386.gn"
SDK_DIR="${OPENOS_SDK_DIR:-$ROOT/target/openos-sdk}"

usage() {
    cat <<USAGE
Usage: scripts/chromium-openos-gn.sh [--check|--sync-overlay|--print-args]

Default action: --check

This script manages the OpenOS Chromium GN/toolchain overlay. It does not
pretend that full Chromium builds today; it only installs/checks the bootstrap
GN identity for:

  target_os = "openos"
  target_cpu = "x86"
  toolchain = //build/toolchain/openos:clang_i386

Environment:
  OPENOS_CHROMIUM_ROOT  Chromium checkout root, default: $CHROMIUM_ROOT
  OPENOS_SDK_DIR        OpenOS SDK path, default: $SDK_DIR
USAGE
}

print_args() {
    cat "$ARGS_FILE"
}

check_file() {
    local path="$1"
    if [ -f "$path" ]; then
        echo "  OK   $path"
        return 0
    fi
    echo "  MISS $path"
    return 1
}

check_overlay() {
    local missing=0
    echo "OpenOS Chromium GN overlay check"
    echo "  root:         $ROOT"
    echo "  overlay_dir:  $OVERLAY_DIR"
    echo "  chromium_src: $CHROMIUM_SRC"
    echo "  sdk_dir:      $SDK_DIR"
    echo
    check_file "$OVERLAY_DIR/build/config/BUILDCONFIG.gn" || missing=1
    check_file "$OVERLAY_DIR/build/toolchain/openos/BUILD.gn" || missing=1
    check_file "$ARGS_FILE" || missing=1
    if [ -d "$SDK_DIR" ]; then
        echo "  OK   OpenOS SDK exists"
    else
        echo "  MISS OpenOS SDK: run ./build.sh sdk"
        missing=1
    fi
    if [ -d "$CHROMIUM_SRC" ]; then
        echo "  OK   Chromium checkout exists"
        check_file "$CHROMIUM_SRC/BUILD.gn" || missing=1
        if [ -f "$CHROMIUM_SRC/build/toolchain/openos/BUILD.gn" ]; then
            echo "  OK   OpenOS toolchain overlay installed in Chromium checkout"
        else
            echo "  INFO OpenOS toolchain overlay not installed yet"
            echo "       Run: scripts/chromium-openos-gn.sh --sync-overlay"
        fi
    else
        echo "  INFO Chromium checkout missing; run ./build.sh chromium-source-check first"
    fi
    return "$missing"
}

sync_overlay() {
    if [ ! -d "$CHROMIUM_SRC" ]; then
        echo "Chromium checkout not found: $CHROMIUM_SRC" >&2
        echo "Run scripts/chromium-source.sh --fetch first." >&2
        exit 1
    fi
    mkdir -p "$CHROMIUM_SRC/build/config" "$CHROMIUM_SRC/build/toolchain/openos"
    cp -af "$OVERLAY_DIR/build/config/BUILDCONFIG.gn" "$CHROMIUM_SRC/build/config/BUILDCONFIG.openos.gn"
    cp -af "$OVERLAY_DIR/build/toolchain/openos/BUILD.gn" "$CHROMIUM_SRC/build/toolchain/openos/BUILD.gn"
    cp -af "$ARGS_FILE" "$CHROMIUM_SRC/args.openos-i386.gn"
    cat <<DONE
OpenOS Chromium GN overlay synced.

Next manual command inside Chromium checkout:
  cd "$CHROMIUM_SRC"
  gn gen out/OpenOS-i386 --args="$(cat args.openos-i386.gn)"

Note:
  Full Chromium will still fail until OpenOS libc/POSIX/platform glue reaches
  the required surface. This overlay only fixes target_os/toolchain identity.
DONE
}

case "${1:---check}" in
    --check|check)
        check_overlay
        ;;
    --sync-overlay|sync)
        sync_overlay
        ;;
    --print-args|args)
        print_args
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        echo "Unknown command: $1" >&2
        usage >&2
        exit 2
        ;;
esac
