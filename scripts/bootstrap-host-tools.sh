#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="${OPENOS_CHROMIUM_DEPS_DIR:-$ROOT/.openos-deps}"
TOOLS_DIR="${OPENOS_HOST_TOOLS_DIR:-$DEPS_DIR/host-tools}"
APT_DIR="$TOOLS_DIR/apt"
ROOT_DIR="$TOOLS_DIR/root"

usage() {
    cat <<USAGE
Usage: scripts/bootstrap-host-tools.sh [--check|--download|--print-env]

Default action: --check

This bootstraps host build tools without sudo by downloading .deb packages with
apt-get download and extracting them under:

  $TOOLS_DIR

It is intended for OpenOS Chromium/Skia bring-up environments where sudo apt
install is not available.

Environment:
  OPENOS_CHROMIUM_DEPS_DIR  External dependency cache, default: $ROOT/.openos-deps
  OPENOS_HOST_TOOLS_DIR     Host tool prefix, default: \$OPENOS_CHROMIUM_DEPS_DIR/host-tools
USAGE
}

print_env() {
    cat <<ENV
OpenOS host-tools bootstrap
  deps_dir:   $DEPS_DIR
  tools_dir:  $TOOLS_DIR
  apt_dir:    $APT_DIR
  root_dir:   $ROOT_DIR
  path_hint:  export PATH="$ROOT_DIR/usr/bin:$ROOT_DIR/usr/lib/gcc/x86_64-linux-gnu/15:$ROOT_DIR/usr/libexec/gcc/x86_64-linux-gnu/15:\$PATH"
ENV
}

have() {
    PATH="$ROOT_DIR/usr/bin:$ROOT_DIR/usr/lib/gcc/x86_64-linux-gnu/15:$ROOT_DIR/usr/libexec/gcc/x86_64-linux-gnu/15:$PATH" command -v "$1" >/dev/null 2>&1
}
where_tool() {
    PATH="$ROOT_DIR/usr/bin:$ROOT_DIR/usr/lib/gcc/x86_64-linux-gnu/15:$ROOT_DIR/usr/libexec/gcc/x86_64-linux-gnu/15:$PATH" command -v "$1" 2>/dev/null || true
}

check_tools() {
    local fail=0
    print_env
    echo
    for t in gn ninja unzip g++ c++; do
        if have "$t"; then
            echo "  OK   $t: $(where_tool "$t")"
        else
            echo "  MISS $t"
            fail=1
        fi
    done
    local cc1plus_path
    cc1plus_path="$(find "$ROOT_DIR/usr/lib/gcc" "$ROOT_DIR/usr/libexec/gcc" -path '*/cc1plus' -type f 2>/dev/null | head -n 1 || true)"
    if [ -n "$cc1plus_path" ]; then
        echo "  OK   cc1plus: $cc1plus_path"
    else
        echo "  MISS cc1plus backend file"
        fail=1
    fi
    if have g++; then
        local smoke_src="$TOOLS_DIR/cxx-smoke.cc"
        local smoke_bin="$TOOLS_DIR/cxx-smoke"
        printf 'int main() { return 0; }\n' > "$smoke_src"
        if PATH="$ROOT_DIR/usr/bin:$ROOT_DIR/usr/lib/gcc/x86_64-linux-gnu/15:$ROOT_DIR/usr/libexec/gcc/x86_64-linux-gnu/15:$PATH" g++ "$smoke_src" -o "$smoke_bin" >/dev/null 2>&1; then
            echo "  OK   g++ smoke compile"
        else
            echo "  MISS g++ smoke compile"
            fail=1
        fi
        rm -f "$smoke_src" "$smoke_bin"
    fi
    return "$fail"
}

download_one() {
    local pkg="$1"
    echo "Downloading package: $pkg"
    apt-get download "$pkg"
}

extract_debs() {
    mkdir -p "$ROOT_DIR"
    for deb in "$APT_DIR"/*.deb; do
        [ -e "$deb" ] || continue
        echo "Extracting: $deb"
        dpkg-deb -x "$deb" "$ROOT_DIR"
    done
}

make_wrappers() {
    mkdir -p "$TOOLS_DIR/bin"
    cat > "$TOOLS_DIR/env.sh" <<ENV
#!/usr/bin/env bash
export OPENOS_HOST_TOOLS_DIR="$TOOLS_DIR"
export PATH="$ROOT_DIR/usr/bin:$ROOT_DIR/usr/lib/gcc/x86_64-linux-gnu/15:$ROOT_DIR/usr/libexec/gcc/x86_64-linux-gnu/15:\$PATH"
ENV
    chmod +x "$TOOLS_DIR/env.sh"

    # Some packages install versioned names only. Add harmless convenience links.
    if [ ! -e "$ROOT_DIR/usr/bin/ninja" ] && [ -e "$ROOT_DIR/usr/bin/ninja-build" ]; then
        ln -sf ninja-build "$ROOT_DIR/usr/bin/ninja"
    fi
    if [ ! -e "$ROOT_DIR/usr/bin/c++" ] && [ -e "$ROOT_DIR/usr/bin/g++" ]; then
        ln -sf g++ "$ROOT_DIR/usr/bin/c++"
    fi
}

download_tools() {
    mkdir -p "$APT_DIR" "$ROOT_DIR"
    cd "$APT_DIR"
    # Keep the list explicit. apt-get download does not install packages and
    # does not require sudo. Existing host libc/gcc runtime packages are reused.
    for pkg in generate-ninja ninja-build unzip g++ g++-15 g++-x86-64-linux-gnu g++-15-x86-64-linux-gnu gcc-15-x86-64-linux-gnu cpp-15-x86-64-linux-gnu libgcc-15-dev libstdc++-15-dev libc6-dev; do
        download_one "$pkg"
    done
    extract_debs
    make_wrappers
    check_tools
}

case "${1:---check}" in
    --check|check)
        check_tools
        ;;
    --download|download|--bootstrap|bootstrap)
        download_tools
        ;;
    --print-env|env)
        print_env
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
