#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION_FILE="$ROOT_DIR/VERSION"

if [[ ! -f "$VERSION_FILE" ]]; then
    echo "VERSION file not found" >&2
    exit 1
fi

BASE_VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
if [[ -z "$BASE_VERSION" ]]; then
    echo "VERSION file is empty" >&2
    exit 1
fi

GIT_COMMIT="unknown"
GIT_DIRTY="0"
if git -C "$ROOT_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    GIT_COMMIT="$(git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    if [[ -n "$(git -C "$ROOT_DIR" status --short 2>/dev/null)" ]]; then
        GIT_DIRTY="1"
    fi
fi

FULL_VERSION="$BASE_VERSION+$GIT_COMMIT"
if [[ "$GIT_DIRTY" = "1" ]]; then
    FULL_VERSION="$FULL_VERSION.dirty"
fi

case "${1:---full}" in
    --base)
        echo "$BASE_VERSION"
        ;;
    --commit)
        echo "$GIT_COMMIT"
        ;;
    --dirty)
        echo "$GIT_DIRTY"
        ;;
    --full)
        echo "$FULL_VERSION"
        ;;
    --help|-h)
        echo "Usage: scripts/version.sh [--base|--commit|--dirty|--full]"
        ;;
    *)
        echo "Unknown argument: $1" >&2
        exit 2
        ;;
esac
