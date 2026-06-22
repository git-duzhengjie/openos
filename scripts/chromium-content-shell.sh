#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="${OPENOS_CHROMIUM_DEPS_DIR:-$ROOT/.openos-deps}"
DEPOT_TOOLS_DIR="${OPENOS_DEPOT_TOOLS_DIR:-$DEPS_DIR/depot_tools}"
CHROMIUM_ROOT="${OPENOS_CHROMIUM_ROOT:-$DEPS_DIR/chromium}"
CHROMIUM_SRC="${OPENOS_CHROMIUM_SRC:-$CHROMIUM_ROOT/src}"
CONTENT_OUT="${OPENOS_CONTENT_SHELL_OUT:-$CHROMIUM_SRC/out/OpenOSContentShell-i386}"
ARGS_FILE="$ROOT/ports/chromium-openos/args.content-shell-openos-i386.gn"
BLINK_PIN_FILE="$ROOT/ports/chromium-openos/blink.official.pin"
CONTENT_PIN_FILE="$ROOT/ports/chromium-openos/content_shell.official.pin"
MIN_FREE_GB="${OPENOS_CONTENT_SHELL_MIN_FREE_GB:-180}"

usage() {
    cat <<USAGE
Usage: scripts/chromium-content-shell.sh [--check|--pin|--gn-gen|--build|--smoke]

Default action: --check

This is the official Blink/content_shell single-process software-rendering
bootstrap entrypoint for the OpenOS Chromium route. It requires a real Chromium
checkout and never treats src/user/chromium.c as a Chromium engine.

Environment:
  OPENOS_CHROMIUM_DEPS_DIR       External dependency cache, default: $ROOT/.openos-deps
  OPENOS_DEPOT_TOOLS_DIR         depot_tools path, default: \$OPENOS_CHROMIUM_DEPS_DIR/depot_tools
  OPENOS_CHROMIUM_ROOT           Chromium checkout root, default: \$OPENOS_CHROMIUM_DEPS_DIR/chromium
  OPENOS_CHROMIUM_SRC            Chromium src path, default: \$OPENOS_CHROMIUM_ROOT/src
  OPENOS_CONTENT_SHELL_OUT       GN output dir, default: \$OPENOS_CHROMIUM_SRC/out/OpenOSContentShell-i386
  OPENOS_CONTENT_SHELL_MIN_FREE_GB Required free space, default: 180
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
OpenOS Blink/content_shell intake
  deps_dir:         $DEPS_DIR
  depot_tools_dir:  $DEPOT_TOOLS_DIR
  chromium_src:     $CHROMIUM_SRC
  content_out:      $CONTENT_OUT
  args_file:        $ARGS_FILE
  blink_pin_file:   $BLINK_PIN_FILE
  content_pin_file: $CONTENT_PIN_FILE
  min_free_gb:      $MIN_FREE_GB
ENV
}

check_common() {
    local fail=0
    local free
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
    free="$(free_gb)"
    echo "  INFO free_space: ${free}GB at $DEPS_DIR"
    if [ "$free" -lt "$MIN_FREE_GB" ]; then
        echo "  MISS free space: need at least ${MIN_FREE_GB}GB for Chromium/content_shell" >&2
        fail=1
    fi
    if [ -d "$DEPOT_TOOLS_DIR/.git" ]; then
        echo "  OK   depot_tools checkout"
    else
        echo "  MISS depot_tools checkout: run scripts/chromium-source.sh --fetch-depot-tools"
        fail=1
    fi
    if [ -d "$CHROMIUM_SRC/.git" ]; then
        echo "  OK   Chromium src checkout"
        git -C "$CHROMIUM_SRC" rev-parse --short HEAD | sed 's/^/  INFO chromium_head: /'
    else
        echo "  MISS Chromium src checkout: run scripts/chromium-source.sh --fetch"
        fail=1
    fi
    if [ -s "$ARGS_FILE" ]; then
        echo "  OK   OpenOS content_shell GN args: $ARGS_FILE"
    else
        echo "  MISS OpenOS content_shell GN args: $ARGS_FILE"
        fail=1
    fi
    if [ -s "$BLINK_PIN_FILE" ]; then
        echo "  OK   Blink official pin: $BLINK_PIN_FILE"
    else
        echo "  INFO Blink official pin not written yet"
    fi
    if [ -s "$CONTENT_PIN_FILE" ]; then
        echo "  OK   content_shell official pin: $CONTENT_PIN_FILE"
    else
        echo "  INFO content_shell official pin not written yet"
    fi
    return "$fail"
}

require_chromium_checkout() {
    with_depot_path
    if [ ! -d "$CHROMIUM_SRC/.git" ]; then
        echo "Chromium src checkout missing: run scripts/chromium-source.sh --fetch" >&2
        exit 1
    fi
}

write_pin() {
    require_chromium_checkout
    local repository commit commit_short generated
    repository="$(git -C "$CHROMIUM_SRC" config --get remote.origin.url || printf '%s' 'https://chromium.googlesource.com/chromium/src.git')"
    commit="$(git -C "$CHROMIUM_SRC" rev-parse HEAD)"
    commit_short="$(git -C "$CHROMIUM_SRC" rev-parse --short HEAD)"
    generated="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    mkdir -p "$(dirname "$BLINK_PIN_FILE")"
    {
        echo "official_component=blink"
        echo "repository=$repository"
        echo "commit=$commit"
        echo "commit_short=$commit_short"
        echo "source_path=$CHROMIUM_SRC"
        echo "gn_out=$CONTENT_OUT"
        echo "gn_args_file=$ARGS_FILE"
        echo "target=//third_party/blink/public:blink"
        echo "generated_at_utc=$generated"
        echo "note=Official Blink source pin for OpenOS Chromium route; built through content_shell bootstrap."
    } > "$BLINK_PIN_FILE"
    {
        echo "official_component=content_shell"
        echo "repository=$repository"
        echo "commit=$commit"
        echo "commit_short=$commit_short"
        echo "source_path=$CHROMIUM_SRC"
        echo "gn_out=$CONTENT_OUT"
        echo "gn_args_file=$ARGS_FILE"
        echo "target=//content/shell:content_shell"
        echo "generated_at_utc=$generated"
        echo "note=Official Chromium content_shell single-process software-rendering pin for OpenOS."
    } > "$CONTENT_PIN_FILE"
    cat "$BLINK_PIN_FILE"
    echo
    cat "$CONTENT_PIN_FILE"
}

gn_gen() {
    require_chromium_checkout
    if ! have gn; then
        echo "gn missing; fetch depot_tools or install host gn" >&2
        exit 1
    fi
    if [ ! -s "$ARGS_FILE" ]; then
        echo "GN args missing: $ARGS_FILE" >&2
        exit 1
    fi
    cd "$CHROMIUM_SRC"
    gn gen "$CONTENT_OUT" --args="$(cat "$ARGS_FILE")"
}

build_content_shell() {
    require_chromium_checkout
    if ! have ninja; then
        echo "ninja missing; fetch depot_tools or install host ninja" >&2
        exit 1
    fi
    gn_gen
    ninja -C "$CONTENT_OUT" content_shell
    write_pin
}

smoke_content_shell() {
    local binary="$CONTENT_OUT/content_shell"
    if [ ! -x "$binary" ]; then
        echo "content_shell binary missing: run scripts/chromium-content-shell.sh --build" >&2
        exit 1
    fi
    "$binary" \
        --single-process \
        --disable-gpu \
        --disable-software-rasterizer=false \
        --no-sandbox \
        --disable-dev-shm-usage \
        "data:text/html,<html><body><h1>OpenOS content_shell smoke</h1></body></html>"
}

case "${1:---check}" in
    --check|check)
        check_common
        ;;
    --pin|pin)
        write_pin
        ;;
    --gn-gen|gn-gen)
        gn_gen
        ;;
    --build|build)
        build_content_shell
        ;;
    --smoke|smoke)
        smoke_content_shell
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
