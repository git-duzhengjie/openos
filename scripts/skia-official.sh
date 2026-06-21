#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="${OPENOS_CHROMIUM_DEPS_DIR:-$ROOT/.openos-deps}"
DEPOT_TOOLS_DIR="${OPENOS_DEPOT_TOOLS_DIR:-$DEPS_DIR/depot_tools}"
SKIA_ROOT="${OPENOS_SKIA_ROOT:-$DEPS_DIR/skia}"
SKIA_OUT="${OPENOS_SKIA_OUT:-$SKIA_ROOT/out/openos-host-raster}"
PIN_FILE="$ROOT/ports/chromium-openos/skia.official.pin"
HOST_TOOLS_DIR="${OPENOS_HOST_TOOLS_DIR:-$DEPS_DIR/host-tools}"
HOST_TOOLS_ROOT="$HOST_TOOLS_DIR/root"
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
  OPENOS_HOST_TOOLS_DIR     optional no-sudo host tools dir, default: .openos-deps/host-tools
USAGE
}

have() { command -v "$1" >/dev/null 2>&1; }
free_gb() {
    mkdir -p "$DEPS_DIR"
    df -BG "$DEPS_DIR" | awk 'NR==2 {gsub("G", "", $4); print $4}'
}
with_depot_path() {
    if [ -d "$HOST_TOOLS_ROOT/usr/bin" ]; then
        export PATH="$HOST_TOOLS_ROOT/usr/bin:$HOST_TOOLS_ROOT/usr/lib/gcc/x86_64-linux-gnu/15:$HOST_TOOLS_ROOT/usr/libexec/gcc/x86_64-linux-gnu/15:$PATH"
    fi
    if [ -d "$DEPOT_TOOLS_DIR" ]; then
        export PATH="$DEPOT_TOOLS_DIR:$PATH"
    fi
}
print_env() {
    cat <<ENV
OpenOS official Skia intake
  deps_dir:        $DEPS_DIR
  depot_tools_dir: $DEPOT_TOOLS_DIR
  host_tools_dir:  $HOST_TOOLS_DIR
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
            echo "       Try: scripts/bootstrap-host-tools.sh --download"
            fail=1
        fi
    done
    if have clang++ || have g++; then
        echo "  OK   c++ compiler: $(command -v clang++ || command -v g++)"
    else
        echo "  MISS c++ compiler: need clang++ or g++"
        echo "       Try: scripts/bootstrap-host-tools.sh --download"
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
        echo "  OK   official Skia git checkout"
        git -C "$SKIA_ROOT" rev-parse --short HEAD | sed 's/^/  INFO skia_head: /'
    elif [ -s "$SKIA_ROOT/.openos-source-commit" ]; then
        echo "  OK   official Skia source snapshot"
        sed 's/^/  INFO skia_head: /' "$SKIA_ROOT/.openos-source-commit"
    else
        echo "  INFO official Skia source missing: $SKIA_ROOT"
    fi
    if [ -s "$PIN_FILE" ]; then
        echo "  OK   Skia official pin: $PIN_FILE"
    else
        echo "  INFO Skia official pin not written yet"
    fi
    return "$fail"
}
resolve_skia_commit() {
    curl --http1.1 -fsSL --retry 5 --retry-delay 3 --connect-timeout 30 \
        https://api.github.com/repos/google/skia/commits/main \
        | python3 -c 'import json,sys; print(json.load(sys.stdin)["sha"])'
}
fetch_skia_snapshot() {
    local commit archive_dir archive_file extract_dir
    commit="$(resolve_skia_commit)"
    if [ -z "$commit" ]; then
        echo "Unable to resolve official Skia main commit from GitHub API" >&2
        return 1
    fi
    archive_dir="$DEPS_DIR/skia-archive"
    archive_file="$archive_dir/skia-$commit.tar.gz"
    extract_dir="$archive_dir/skia-$commit"
    rm -rf "$SKIA_ROOT" "$archive_dir"
    mkdir -p "$archive_dir"
    echo "Downloading official Skia GitHub snapshot: $commit"
    curl --http1.1 -fL --retry 8 --retry-delay 5 --connect-timeout 30 \
        -o "$archive_file" \
        "https://codeload.github.com/google/skia/tar.gz/$commit"
    tar -xzf "$archive_file" -C "$archive_dir"
    mv "$extract_dir" "$SKIA_ROOT"
    printf '%s\n' "$commit" > "$SKIA_ROOT/.openos-source-commit"
    printf '%s\n' "https://github.com/google/skia.git" > "$SKIA_ROOT/.openos-source-repository"
    printf '%s\n' "github-codeload-tarball" > "$SKIA_ROOT/.openos-source-kind"
}
fetch_skia() {
    mkdir -p "$DEPS_DIR"
    if [ ! -d "$DEPOT_TOOLS_DIR/.git" ]; then
        echo "depot_tools missing; fetching it first..."
        "$ROOT/scripts/chromium-source.sh" --fetch-depot-tools || true
    fi
    if [ -d "$SKIA_ROOT/.git" ]; then
        if git -C "$SKIA_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
            git -C "$SKIA_ROOT" fetch --depth 1 --tags origin || true
            if git -C "$SKIA_ROOT" config --bool core.sparseCheckout | grep -qi '^true$'; then
                echo "Disabling sparse checkout for official Skia source tree..."
                git -C "$SKIA_ROOT" sparse-checkout disable
                git -C "$SKIA_ROOT" checkout --force HEAD
            fi
        else
            echo "Broken Skia .git directory detected; cleaning it before refetch..." >&2
            rm -rf "$SKIA_ROOT"
        fi
    fi
    if [ ! -d "$SKIA_ROOT/.git" ] && [ ! -s "$SKIA_ROOT/.openos-source-commit" ]; then
        if ! git clone --depth 1 --filter=blob:none https://github.com/google/skia.git "$SKIA_ROOT"; then
            echo "GitHub Skia clone failed; falling back to official GitHub source snapshot..." >&2
            rm -rf "$SKIA_ROOT"
            fetch_skia_snapshot
        fi
    fi
    if [ -d "$SKIA_ROOT/.git" ]; then
        cd "$SKIA_ROOT"
        python3 tools/git-sync-deps || true
    fi
    write_pin
}
write_pin() {
    local repository commit commit_short source_kind
    if [ -d "$SKIA_ROOT/.git" ]; then
        repository="$(git -C "$SKIA_ROOT" config --get remote.origin.url)"
        commit="$(git -C "$SKIA_ROOT" rev-parse HEAD)"
        commit_short="$(git -C "$SKIA_ROOT" rev-parse --short HEAD)"
        source_kind="git-checkout"
    elif [ -s "$SKIA_ROOT/.openos-source-commit" ]; then
        repository="$(cat "$SKIA_ROOT/.openos-source-repository")"
        commit="$(cat "$SKIA_ROOT/.openos-source-commit")"
        commit_short="$(printf '%s' "$commit" | cut -c1-12)"
        source_kind="$(cat "$SKIA_ROOT/.openos-source-kind")"
    else
        echo "Skia official source missing: $SKIA_ROOT" >&2
        exit 1
    fi
    mkdir -p "$(dirname "$PIN_FILE")"
    {
        echo "official_component=skia"
        echo "repository=$repository"
        echo "commit=$commit"
        echo "commit_short=$commit_short"
        echo "source_kind=$source_kind"
        echo "source_path=$SKIA_ROOT"
        echo "generated_at_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo "note=Official Skia source pin for OpenOS Chromium route."
    } > "$PIN_FILE"
    cat "$PIN_FILE"
}
gn_gen() {
    with_depot_path
    if [ ! -d "$SKIA_ROOT/.git" ] && [ ! -s "$SKIA_ROOT/.openos-source-commit" ]; then
        echo "Skia source missing: run scripts/skia-official.sh --fetch" >&2
        exit 1
    fi
    if ! have gn; then
        echo "gn missing; install host gn or fetch depot_tools" >&2
        exit 1
    fi
    cd "$SKIA_ROOT"
    gn gen "$SKIA_OUT" --args='is_official_build=false is_debug=false skia_enable_ganesh=false skia_enable_graphite=false skia_use_gl=false skia_use_vulkan=false skia_use_metal=false skia_use_dawn=false skia_use_direct3d=false skia_use_fontconfig=false skia_use_freetype=false skia_use_harfbuzz=false skia_use_icu=false skia_enable_fontmgr_empty=true skia_enable_fontmgr_fontconfig=false skia_enable_fontmgr_FontConfigInterface=false skia_use_libjpeg_turbo_decode=false skia_use_libjpeg_turbo_encode=false skia_use_libpng_decode=false skia_use_libpng_encode=false skia_use_libwebp_decode=false skia_use_libwebp_encode=false skia_use_zlib=false skia_use_system_libjpeg_turbo=false skia_use_system_libpng=false skia_use_system_zlib=false skia_use_system_freetype2=false skia_use_system_harfbuzz=false skia_use_system_expat=false extra_cflags=["-DSK_DISABLE_LEGACY_SHADERCONTEXT"]'
}
build_skia() {
    with_depot_path
    if ! have ninja; then
        echo "ninja missing; install host ninja or fetch depot_tools" >&2
        exit 1
    fi
    gn_gen
    ninja -C "$SKIA_OUT" libskia.a
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
