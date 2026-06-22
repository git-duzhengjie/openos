#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKIA_PIN="$ROOT/ports/chromium-openos/skia.official.pin"
V8_PIN="$ROOT/ports/chromium-openos/v8.official.pin"
BLINK_PIN="$ROOT/ports/chromium-openos/blink.official.pin"
CONTENT_PIN="$ROOT/ports/chromium-openos/content_shell.official.pin"
KERNEL_C="$ROOT/src/kernel/kernel.c"
BUILD_SH="$ROOT/build.sh"
CONTENT_BINARY_DEFAULT="$ROOT/.openos-deps/chromium/src/out/OpenOSContentShell-i386/content_shell"
CONTENT_BINARY="${OPENOS_CONTENT_SHELL_BINARY:-$CONTENT_BINARY_DEFAULT}"

usage() {
    cat <<USAGE
Usage: scripts/chromium-real-switch-gate.sh [--check]

Final gate for the real Chromium engine switch. This gate must fail until
OpenOS /bin/chromium is backed by an official Chromium Content/Blink/V8/Skia
artifact instead of src/user/chromium.c demo.

Environment:
  OPENOS_CONTENT_SHELL_BINARY  Official content_shell binary path.
                               Default: $CONTENT_BINARY_DEFAULT
USAGE
}

fail=0

ok() { echo "  OK   $*"; }
miss() { echo "  MISS $*" >&2; fail=1; }
info() { echo "  INFO $*"; }

check_real_pin() {
    local pin="$1"
    local label="$2"
    if [ ! -s "$pin" ]; then
        miss "$label pin missing: $pin"
        return
    fi
    if grep -Eq '(<pending>|status=pending_|repository=<pending)' "$pin"; then
        miss "$label pin is still pending: $pin"
        return
    fi
    if grep -Eq '^commit=[0-9a-f]{40}$' "$pin"; then
        ok "$label pin has real 40-hex commit"
    else
        miss "$label pin lacks real 40-hex commit: $pin"
    fi
}

check_content_binary() {
    if [ ! -f "$CONTENT_BINARY" ]; then
        miss "official content_shell binary missing: $CONTENT_BINARY"
        return
    fi
    if [ ! -s "$CONTENT_BINARY" ]; then
        miss "official content_shell binary is empty: $CONTENT_BINARY"
        return
    fi
    local size sha
    size="$(wc -c < "$CONTENT_BINARY" | tr -d ' ')"
    if command -v sha256sum >/dev/null 2>&1; then
        sha="$(sha256sum "$CONTENT_BINARY" | awk '{print $1}')"
        ok "official content_shell artifact exists size=${size} sha256=${sha}"
    else
        ok "official content_shell artifact exists size=${size}"
    fi
}

check_demo_not_claiming_chromium_path() {
    if grep -Fq 'vfs_open("/bin/chromium", O_CREAT | O_RDWR, 0755)' "$KERNEL_C" && \
       grep -Fq 'chromium_elf' "$KERNEL_C"; then
        miss "demo chromium_elf is still installed as /bin/chromium in src/kernel/kernel.c"
    else
        ok "src/user/chromium.c demo is not installed as /bin/chromium"
    fi

    if grep -Eq 'embed_chromium\.h[[:space:]]+chromium_elf|chromium\.elf.*embed_chromium\.h' "$BUILD_SH"; then
        miss "build.sh still embeds src/user/chromium.c as chromium_elf/chromium.elf"
    else
        ok "build.sh no longer embeds demo chromium as chromium_elf/chromium.elf"
    fi
}

check_args() {
    local args="$ROOT/ports/chromium-openos/args.content-shell-openos-i386.gn"
    if [ ! -s "$args" ]; then
        miss "GN args missing: $args"
        return
    fi
    grep -Fq 'target_os = "openos"' "$args" && ok "GN target_os=openos" || miss "GN target_os=openos missing"
    grep -Fq 'target_cpu = "x86"' "$args" && ok "GN target_cpu=x86" || miss "GN target_cpu=x86 missing"
    grep -Fq 'use_x11 = false' "$args" && ok "GN disables X11" || miss "GN use_x11=false missing"
    grep -Fq 'use_wayland = false' "$args" && ok "GN disables Wayland" || miss "GN use_wayland=false missing"
    grep -Fq 'use_ozone = true' "$args" && ok "GN enables Ozone/headless path" || miss "GN use_ozone=true missing"
}

run_check() {
    echo "OpenOS real Chromium switch final gate"
    check_real_pin "$SKIA_PIN" "Skia"
    check_real_pin "$V8_PIN" "V8"
    check_real_pin "$BLINK_PIN" "Blink"
    check_real_pin "$CONTENT_PIN" "content_shell"
    check_content_binary
    check_demo_not_claiming_chromium_path
    check_args
    if [ "$fail" -eq 0 ]; then
        echo "OpenOS /bin/chromium is eligible to be treated as a real Chromium engine path."
    else
        echo "OpenOS /bin/chromium is NOT yet switched to the real Chromium engine path." >&2
    fi
    return "$fail"
}

case "${1:---check}" in
    --check|check)
        run_check
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
