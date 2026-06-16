#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT_DIR/src/kernel/include/version.h}"
BASE_VERSION="$(bash "$ROOT_DIR/scripts/version.sh" --base)"
FULL_VERSION="$(bash "$ROOT_DIR/scripts/version.sh" --full)"
GIT_COMMIT="$(bash "$ROOT_DIR/scripts/version.sh" --commit)"
GIT_DIRTY="$(bash "$ROOT_DIR/scripts/version.sh" --dirty)"

mkdir -p "$(dirname "$OUT")"
cat > "$OUT.tmp" <<HEADER
#ifndef OPENOS_VERSION_H
#define OPENOS_VERSION_H

#define OPENOS_VERSION_BASE "$BASE_VERSION"
#define OPENOS_VERSION_FULL "$FULL_VERSION"
#define OPENOS_VERSION_GIT_COMMIT "$GIT_COMMIT"
#define OPENOS_VERSION_GIT_DIRTY $GIT_DIRTY

#endif /* OPENOS_VERSION_H */
HEADER

if [[ ! -f "$OUT" ]] || ! cmp -s "$OUT.tmp" "$OUT"; then
    mv "$OUT.tmp" "$OUT"
else
    rm -f "$OUT.tmp"
fi

echo "$OUT"
