#!/usr/bin/env bash
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
  echo "ERROR: apt-get not found. This helper is intended for Ubuntu/Debian WSL." >&2
  exit 1
fi

sudo apt-get update
sudo apt-get install -y \
  gcc-aarch64-linux-gnu \
  binutils-aarch64-linux-gnu \
  qemu-system-arm

cat <<'MSG'
AArch64 toolchain installed.
Next verification command:
  ./tools/smoke_aarch64_hello.sh
MSG
