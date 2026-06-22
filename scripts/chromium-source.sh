#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="${OPENOS_CHROMIUM_DEPS_DIR:-$ROOT/.openos-deps}"
DEPOT_TOOLS_DIR="${OPENOS_DEPOT_TOOLS_DIR:-$DEPS_DIR/depot_tools}"
CHROMIUM_ROOT="${OPENOS_CHROMIUM_ROOT:-$DEPS_DIR/chromium}"
CHROMIUM_SRC="$CHROMIUM_ROOT/src"
PIN_FILE="$ROOT/docs/chromium-upstream-pin.md"
MIN_FREE_GB="${OPENOS_CHROMIUM_MIN_FREE_GB:-180}"

usage() {
    cat <<USAGE
Usage: scripts/chromium-source.sh [--check|--print-env|--fetch-depot-tools|--fetch]

Default action: --check

Environment:
  OPENOS_CHROMIUM_DEPS_DIR   External dependency cache, default: $ROOT/.openos-deps
  OPENOS_DEPOT_TOOLS_DIR     depot_tools path, default: \$OPENOS_CHROMIUM_DEPS_DIR/depot_tools
  OPENOS_CHROMIUM_ROOT       Chromium checkout root, default: \$OPENOS_CHROMIUM_DEPS_DIR/chromium
  OPENOS_CHROMIUM_MIN_FREE_GB Required free space for --check/--fetch, default: 180

Notes:
  --check never downloads Chromium.
  --fetch-depot-tools downloads depot_tools only, using official Chromium Gitiles git clone first and Gitiles archive fallback.
  --fetch downloads Chromium source and can consume 100GB+ disk and many hours.
USAGE
}

have() { command -v "$1" >/dev/null 2>&1; }
free_gb() {
    mkdir -p "$DEPS_DIR"
    df -BG "$DEPS_DIR" | awk 'NR==2 {gsub("G", "", $4); print $4}'
}
print_env() {
    cat <<ENV
OpenOS Chromium upstream source configuration
  pin_file:            $PIN_FILE
  deps_dir:            $DEPS_DIR
  depot_tools_dir:     $DEPOT_TOOLS_DIR
  chromium_root:       $CHROMIUM_ROOT
  chromium_src:        $CHROMIUM_SRC
  min_free_gb:         $MIN_FREE_GB
ENV
}
check_common() {
    local missing=0
    print_env
    echo
    echo "Host tool check:"
    for tool in git python3 curl tar unzip xz; do
        if have "$tool"; then
            echo "  OK   $tool: $(command -v "$tool")"
        else
            echo "  MISS $tool"
            missing=1
        fi
    done
    if have lsb_release; then
        echo "  INFO distro: $(lsb_release -ds 2>/dev/null || true)"
    fi
    local free
    free="$(free_gb)"
    echo "  INFO free_space: ${free}GB at $DEPS_DIR"
    if [ "$free" -lt "$MIN_FREE_GB" ]; then
        echo "  MISS free space: need at least ${MIN_FREE_GB}GB for Chromium checkout" >&2
        missing=1
    fi
    if [ -x "$DEPOT_TOOLS_DIR/fetch" ] && [ -x "$DEPOT_TOOLS_DIR/gclient" ]; then
        echo "  OK   depot_tools: $DEPOT_TOOLS_DIR"
    else
        echo "  MISS depot_tools: $DEPOT_TOOLS_DIR"
        echo "       Run: scripts/chromium-source.sh --fetch-depot-tools"
        missing=1
    fi
    if [ -d "$CHROMIUM_SRC/.git" ]; then
        echo "  OK   chromium src: $CHROMIUM_SRC"
        if [ -f "$CHROMIUM_SRC/DEPS" ]; then
            echo "  OK   Chromium DEPS file present"
        fi
    else
        echo "  INFO chromium src not checked out yet: $CHROMIUM_SRC"
    fi
    return "$missing"
}
fetch_depot_tools() {
    mkdir -p "$DEPS_DIR"
    if [ -x "$DEPOT_TOOLS_DIR/fetch" ] && [ -x "$DEPOT_TOOLS_DIR/gclient" ]; then
        if [ -d "$DEPOT_TOOLS_DIR/.git" ]; then
            git -C "$DEPOT_TOOLS_DIR" pull --ff-only || true
        fi
        echo "depot_tools ready: $DEPOT_TOOLS_DIR"
        return 0
    fi

    if [ -e "$DEPOT_TOOLS_DIR" ]; then
        echo "Removing stale depot_tools path: $DEPOT_TOOLS_DIR" >&2
        rm -rf "$DEPOT_TOOLS_DIR" || true
    fi

    local tmp_dir tarball extracted
    tmp_dir="$DEPS_DIR/depot_tools.tmp.$$"
    tarball="$DEPS_DIR/depot_tools-main.tar.gz"
    extracted="$DEPS_DIR/depot_tools-main"
    rm -rf "$tmp_dir" "$extracted"
    find "$DEPS_DIR" -maxdepth 1 -type d -name 'depot_tools.tmp.*' -exec rm -rf {} + 2>/dev/null || true

    echo "Cloning depot_tools from official Chromium Gitiles..."
    if timeout "${OPENOS_CHROMIUM_GIT_TIMEOUT_SEC:-180}" \
        git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "$tmp_dir"; then
        rm -rf "$DEPOT_TOOLS_DIR"
        mv "$tmp_dir" "$DEPOT_TOOLS_DIR"
        echo "depot_tools ready from git: $DEPOT_TOOLS_DIR"
        return 0
    fi

    echo "Git clone of depot_tools failed; falling back to official Gitiles archive." >&2
    rm -rf "$tmp_dir" "$extracted"
    if command -v curl >/dev/null 2>&1; then
        curl -L --retry 3 --connect-timeout 20 --max-time "${OPENOS_CHROMIUM_CURL_MAX_TIME_SEC:-120}" \
            -o "$tarball" \
            https://chromium.googlesource.com/chromium/tools/depot_tools/+archive/refs/heads/main.tar.gz
    else
        python3 - <<PY
import urllib.request
urllib.request.urlretrieve('https://chromium.googlesource.com/chromium/tools/depot_tools/+archive/refs/heads/main.tar.gz', r'$tarball')
PY
    fi
    tar -xzf "$tarball" -C "$DEPS_DIR"
    rm -rf "$DEPOT_TOOLS_DIR"
    mv "$extracted" "$DEPOT_TOOLS_DIR"
    chmod +x "$DEPOT_TOOLS_DIR/fetch" "$DEPOT_TOOLS_DIR/gclient" || true
    echo "depot_tools ready from Gitiles archive: $DEPOT_TOOLS_DIR"
}
fetch_chromium() {
    check_common || true
    fetch_depot_tools
    export PATH="$DEPOT_TOOLS_DIR:$PATH"
    mkdir -p "$CHROMIUM_ROOT"
    if [ -d "$CHROMIUM_SRC/.git" ]; then
        echo "Chromium already exists: $CHROMIUM_SRC"
        echo "Run manually if needed: cd '$CHROMIUM_SRC' && gclient sync --no-history --with_branch_heads --with_tags"
    else
        cd "$CHROMIUM_ROOT"
        fetch --nohooks --no-history chromium
        cd "$CHROMIUM_SRC"
        gclient sync --no-history --with_branch_heads --with_tags
    fi
}

case "${1:---check}" in
    --check|check)
        check_common
        ;;
    --print-env|env)
        print_env
        ;;
    --fetch-depot-tools|depot_tools)
        fetch_depot_tools
        ;;
    --fetch|fetch)
        fetch_chromium
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
