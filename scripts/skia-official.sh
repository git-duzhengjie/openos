#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="${OPENOS_CHROMIUM_DEPS_DIR:-$ROOT/.openos-deps}"
DEPOT_TOOLS_DIR="${OPENOS_DEPOT_TOOLS_DIR:-$DEPS_DIR/depot_tools}"
SKIA_ROOT="${OPENOS_SKIA_ROOT:-$DEPS_DIR/skia}"
SKIA_OUT="${OPENOS_SKIA_OUT:-$SKIA_ROOT/out/openos-host-raster}"
PIN_FILE="$ROOT/ports/chromium-openos/skia.official.pin"
MIN_FREE_GB="${OPENOS_SKIA_MIN_FREE_GB:-12}"

usage() {
    cat <<USAGE
Usage: scripts/skia-official.sh [--check|--fetch|--pin|--gn-gen|--build]

Default action: --check

This is the official Skia intake entrypoint for OpenOS' Chromium route. It does
not use src/user/skia_demo.c. That file remains an OpenOS GUI smoke test only.

Environment:
  OPENOS_CHROMIUM_DEPS_DIR  External dependency cache, default: $ROOT/.openos-deps
  OPENOS_DEPOT_TOOLS_DIR    depot_tools path, default: \$OPENOS_CHROMIUM_DEPS_DIR/depot_tools
  OPENOS_SKIA_ROOT          official Skia checkout, default: \$OPENOS_CHROMIUM_DEPS_DIR/skia
  OPENOS_SKIA_MIN_FREE_GB   required free space, default: 12
USAGE
}

have() { command -v "$1" >/dev/null 2>&1; }
free_gb() {
    mkdir -p "$DEPS_DIR"
    df -BG "$DEPS_DIR" | awk 'NR==2 {gsub("G", "", $4); print $4}'
}
with_depot_path() {
    if [ -d "$DEPOT_TOOLS_DIR" ]; then
        export PATH="$DEPOT_TOOLS_DIR:$PATH"
    fi
}
print_env() {
    cat <<ENV
OpenOS official Skia intake
  deps_dir:        $DEPS_DIR
  depot_tools_dir: $DEPOT_TOOLS_DIR
  skia_root:       $SKIA_ROOT
  skia_out:        $SKIA_OUT
  pin_file:        $PIN_FILE
  min_free_gb:     $MIN_FREE_GB
ENV
}
check_common() {
    local fail=0
    with_depot_path
    print_env
    echo
    echo "Host tool check:"
    for tool in git python3 curl tar xz; do
        if have "$tool"; then
            echo "  OK   $tool: $(command -v "$tool")"
        else
            echo "  MISS $tool"
            fail=1
        fi
    done
    for tool in gn ninja; do
        if have "$tool"; then
            echo "  OK   $tool: $(command -v "$tool")"
        else
            echo "  MISS $tool (normally provided by depot_tools or host package)"
            fail=1
        fi
    done
    if have clang++ || have g++; then
        echo "  OK   c++ compiler: $(command -v clang++ || command -v g++)"
    else
        echo "  MISS c++ compiler: need clang++ or g++"
        fail=1
    fi
    local free
    free="$(free_gb)"
    echo "  INFO free_space: ${free}GB at $DEPS_DIR"
    if [ "$free" -lt "$MIN_FREE_GB" ]; then
        echo "  MISS free space: need at least ${MIN_FREE_GB}GB for Skia checkout/build" >&2
        fail=1
    fi
    if [ -d "$DEPOT_TOOLS_DIR/.git" ]; then
        echo "  OK   depot_tools checkout"
    else
        echo "  MISS depot_tools checkout: run scripts/chromium-source.sh --fetch-depot-tools"
        fail=1
    fi
    if [ -d "$SKIA_ROOT/.git" ]; then
        echo "  OK   official Skia checkout"
        git -C "$SKIA_ROOT" rev-parse --short HEAD | sed 's/^/  INFO skia_head: /'
    else
        echo "  INFO official Skia checkout missing: $SKIA_ROOT"
    fi
    if [ -s "$PIN_FILE" ]; then
        echo "  OK   Skia official pin: $PIN_FILE"
    else
        echo "  INFO Skia official pin not written yet"
    fi
    return "$fail"
}
fetch_skia() {
    mkdir -p "$DEPS_DIR"
    if [ ! -d "$DEPOT_TOOLS_DIR/.git" ]; then
        echo "depot_tools missing; fetching it first..."
        "$ROOT/scripts/chromium-source.sh" --fetch-depot-tools
    fi
    if [ -d "$SKIA_ROOT/.git" ]; then
        git -C "$SKIA_ROOT" fetch --tags origin
    else
        git clone https://skia.googlesource.com/skia.git "$SKIA_ROOT"
    fi
    cd "$SKIA_ROOT"
    python3 tools/git-sync-deps || true
}
write_pin() {
    if [ ! -d "$SKIA_ROOT/.git" ]; then
        echo "Skia checkout missing: $SKIA_ROOT" >&2
        exit 1
    fi
    mkdir -p "$(dirname "$PIN_FILE")"
    {
        echo "official_component=skia"
        echo "repository=https://skia.googlesource.com/skia.git"
        echo "commit=$(git -C "$SKIA_ROOT" rev-parse HEAD)"
        echo "commit_short=$(git -C "$SKIA_ROOT" rev-parse --short HEAD)"
        echo "source_path=$SKIA_ROOT"
        echo "generated_at_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo "note=Official Skia source pin for OpenOS Chromium route."
    } > "$PIN_FILE"
    cat "$PIN_FILE"
}
gn_gen() {
    with_depot_path
    if [ ! -d "$SKIA_ROOT/.git" ]; then
        echo "Skia checkout missing: run scripts/skia-official.sh --fetch" >&2
        exit 1
    fi
    if ! have gn; then
        echo "gn missing; install host gn or fetch depot_tools" >&2
        exit 1
    fi
    cd "$SKIA_ROOT"
    gn gen "$SKIA_OUT" --args='is_official_build=false is_debug=false skia_enable_gpu=false skia_use_gl=false skia_use_vulkan=false skia_use_system_libjpeg_turbo=false skia_use_system_libpng=false skia_use_system_zlib=false skia_use_system_freetype2=false skia_use_system_harfbuzz=false skia_use_system_expat=false skia_use_icu=false skia_enable_fontmgr_empty=true extra_cflags=["-DSK_DISABLE_LEGACY_SHADERCONTEXT"]'
}
build_skia() {
    with_depot_path
    if ! have ninja; then
        echo "ninja missing; install host ninja or fetch depot_tools" >&2
        exit 1
    fi
    gn_gen
    ninja -C "$SKIA_OUT" libskia modules
    write_pin
}

case "${1:---check}" in
    --check|check)
        check_common
        ;;
    --fetch|fetch)
        fetch_skia
        ;;
    --pin|pin)
        write_pin
        ;;
    --gn-gen|gn-gen)
        gn_gen
        ;;
    --build|build)
        build_skia
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
