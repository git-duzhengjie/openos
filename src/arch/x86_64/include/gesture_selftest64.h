#ifndef OPENOS_ARCH_X86_64_GESTURE_SELFTEST64_H
#define OPENOS_ARCH_X86_64_GESTURE_SELFTEST64_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M8-B.4 gesture engine selftest. Feeds synthetic touch frames into the
 * gesture state machine and asserts the correct high-level events are
 * produced in the correct order. Pure logic; no HID hardware required.
 * Returns true on PASS.
 *
 * Six stages:
 *   1) TAP           — press+release <200ms, <8px movement
 *   2) LONG_PRESS    — press held >=500ms, still finger
 *   3) DRAG          — DRAG_BEGIN → DRAG_MOVE (×N) → DRAG_END
 *   4) SWIPE_RIGHT   — from left edge, inward >=80px
 *   5) SWIPE_UP      — from bottom edge, inward >=80px
 *   6) TAP-then-drop — release outside tap window emits nothing
 */
bool arch_x86_64_gesture_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif
