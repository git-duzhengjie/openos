#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

VERSION="${OPENOS_RELEASE_VERSION:-}"
DIST_DIR="${OPENOS_RELEASE_DIR:-$ROOT_DIR/target/release}"
SKIP_BUILD="${OPENOS_RELEASE_SKIP_BUILD:-0}"

usage() {
    cat <<USAGE
Usage: $0 [--version VERSION] [--dist-dir DIR] [--skip-build]

Create a reproducible OpenOS release package under target/release by default.

Environment overrides:
  OPENOS_RELEASE_VERSION     Release version label
  OPENOS_RELEASE_DIR         Output directory, default: target/release
  OPENOS_RELEASE_SKIP_BUILD  Skip bash build.sh when set to 1
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version)
            VERSION="$2"
            shift 2
            ;;
        --dist-dir)
            DIST_DIR="$2"
            shift 2
            ;;
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ -z "$VERSION" ]]; then
    VERSION="$(bash scripts/version.sh --full)"
fi

PKG_NAME="openos-$VERSION"
STAGE_DIR="$DIST_DIR/$PKG_NAME"
ARCHIVE="$DIST_DIR/$PKG_NAME.tar.gz"
SHA_FILE="$ARCHIVE.sha256"
MANIFEST="$STAGE_DIR/MANIFEST.txt"

if [[ "$SKIP_BUILD" != "1" ]]; then
    bash build.sh
fi

required_artifacts=(
    target/openos.img
    target/kernel.elf
    target/kernel.bin
)

for artifact in "${required_artifacts[@]}"; do
    if [[ ! -s "$artifact" ]]; then
        echo "Missing release artifact: $artifact" >&2
        exit 1
    fi
done

rm -rf "$STAGE_DIR" "$ARCHIVE" "$SHA_FILE"
mkdir -p "$STAGE_DIR/bin" "$STAGE_DIR/docs" "$STAGE_DIR/scripts" "$DIST_DIR"

cp target/openos.img "$STAGE_DIR/bin/"
cp target/kernel.elf "$STAGE_DIR/bin/"
cp target/kernel.bin "$STAGE_DIR/bin/"
cp README.md TODOLIST.md "$STAGE_DIR/docs/"

for optional_doc in docs/gdb-debugging.md docs/release.md tests/README.md; do
    if [[ -f "$optional_doc" ]]; then
        cp "$optional_doc" "$STAGE_DIR/docs/"
    fi
done

for optional_script in \
    scripts/qemu-smoke.sh \
    scripts/gdb-openos.sh \
    scripts/openos.gdb \
    scripts/gdb-openos.gdb \
    scripts/debug-qemu-gdb.sh; do
    if [[ -f "$optional_script" ]]; then
        cp "$optional_script" "$STAGE_DIR/scripts/"
    fi
done

{
    echo "OpenOS Release Manifest"
    echo "version=$VERSION"
    echo "created_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        echo "git_commit=$(git rev-parse HEAD)"
        echo "git_status_short_begin"
        git status --short
        echo "git_status_short_end"
    fi
    echo "files_begin"
    (cd "$STAGE_DIR" && find . -type f -print | sort)
    echo "files_end"
} > "$MANIFEST"

(
    cd "$DIST_DIR"
    tar --sort=name --owner=0 --group=0 --numeric-owner \
        --mtime='UTC 1970-01-01' \
        -czf "$PKG_NAME.tar.gz" "$PKG_NAME"
    sha256sum "$PKG_NAME.tar.gz" > "$PKG_NAME.tar.gz.sha256"
)

echo "[RELEASE] package: $ARCHIVE"
echo "[RELEASE] sha256 : $SHA_FILE"
