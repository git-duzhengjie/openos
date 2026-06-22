#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REQUIRED_DOC="$ROOT/docs/chromium-engine-reality-gate.md"
CHROMIUM_DEMO="$ROOT/src/user/chromium.c"
SKIA_PIN="$ROOT/ports/chromium-openos/skia.official.pin"
V8_PIN="$ROOT/ports/chromium-openos/v8.official.pin"
BLINK_PIN="$ROOT/ports/chromium-openos/blink.official.pin"
CONTENT_PIN="$ROOT/ports/chromium-openos/content_shell.official.pin"

usage() {
    cat <<USAGE
Usage: scripts/chromium-engine-gate.sh [--check|--strict]

--check   Verify repository truth-in-advertising markers for the Chromium route.
--strict  Additionally require official Skia/V8/Blink/content_shell pins.

The current OpenOS /bin/chromium is allowed to exist only as a demo until the
strict pins are present.
USAGE
}

check_contains() {
    local path="$1"
    local needle="$2"
    local label="$3"
    if grep -Fq "$needle" "$path"; then
        echo "  OK   $label"
    else
        echo "  MISS $label" >&2
        return 1
    fi
}

check_real_pin() {
    local pin="$1"
    if [ ! -s "$pin" ]; then
        echo "  MISS strict pin: $pin" >&2
        return 1
    fi
    if grep -Eq '(<pending>|status=pending_|repository=<pending)' "$pin"; then
        echo "  MISS strict pin is still pending: $pin" >&2
        return 1
    fi
    if grep -Eq '^commit=[0-9a-f]{40}$' "$pin"; then
        echo "  OK   strict pin: $pin"
        return 0
    fi
    echo "  MISS strict pin lacks a real 40-hex commit: $pin" >&2
    return 1
}

check_gate() {
    local strict="${1:-0}"
    local fail=0
    echo "OpenOS Chromium engine reality gate"
    check_contains "$REQUIRED_DOC" '当前 `/bin/chromium` 不是真实 Chrome/Chromium 引擎' "reality doc states demo status" || fail=1
    check_contains "$CHROMIUM_DEMO" "OpenOS Chromium Demo" "window title marks chromium as demo" || fail=1
    check_contains "$CHROMIUM_DEMO" "Not Chrome engine" "runtime UI disclaims Chrome engine" || fail=1
    if [ "$strict" = "1" ]; then
        for pin in "$SKIA_PIN" "$V8_PIN" "$BLINK_PIN" "$CONTENT_PIN"; do
            check_real_pin "$pin" || fail=1
        done
    else
        echo "  INFO strict official-engine pins are not required in --check mode"
    fi
    return "$fail"
}

case "${1:---check}" in
    --check|check)
        check_gate 0
        ;;
    --strict|strict)
        check_gate 1
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
