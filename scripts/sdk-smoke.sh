#!/bin/bash
# Validate that the exported OpenOS userland SDK can build a minimal user ELF.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/target"
SDK_DIR="${OPENOS_SDK_DIR:-$BUILD/openos-sdk}"
CC="${OPENOS_CC:-gcc}"
LD="${OPENOS_LD:-ld}"

require_tool() {
    local tool="$1"
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: required tool '$tool' was not found." >&2
        exit 1
    fi
}

require_tool "$CC"
require_tool "$LD"
require_tool readelf

bash "$ROOT/scripts/export-openos-sdk.sh"

mkdir -p "$BUILD/sdk-smoke"
cat > "$BUILD/sdk-smoke/main.c" <<'EOF_SMOKE'
#include <openos.h>

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;
    openos_write_str("openos sdk smoke\n");
    return 0;
}
EOF_SMOKE

"$CC" -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
    -fno-stack-protector -fno-builtin \
    -I "$SDK_DIR/include/openos" \
    -c "$BUILD/sdk-smoke/main.c" -o "$BUILD/sdk-smoke/main.o"

"$LD" -m elf_i386 -T "$SDK_DIR/ld/user.ld" -nostdlib \
    "$SDK_DIR/crt/crt0.o" \
    "$BUILD/sdk-smoke/main.o" \
    "$SDK_DIR/lib/libopenos.a" \
    "$SDK_DIR/lib/libopenos_c.a" \
    "$SDK_DIR/lib/libopenos_cxx.a" \
    -o "$BUILD/sdk-smoke/sdk-smoke.elf"

if ! readelf -h "$BUILD/sdk-smoke/sdk-smoke.elf" | grep -q "Class:.*ELF32"; then
    echo "ERROR: sdk smoke output is not ELF32." >&2
    exit 1
fi
if ! readelf -h "$BUILD/sdk-smoke/sdk-smoke.elf" | grep -q "Machine:.*Intel 80386"; then
    echo "ERROR: sdk smoke output is not i386." >&2
    exit 1
fi
if ! readelf -s "$BUILD/sdk-smoke/sdk-smoke.elf" | grep -q " _start$"; then
    echo "ERROR: sdk smoke output has no _start symbol." >&2
    exit 1
fi

printf 'OpenOS SDK smoke passed: %s\n' "$BUILD/sdk-smoke/sdk-smoke.elf"
if command -v file >/dev/null 2>&1; then
    file "$BUILD/sdk-smoke/sdk-smoke.elf"
fi
