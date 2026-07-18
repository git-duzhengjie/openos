/* ============================================================
 * openos - M9.5 GUI input bridge
 * ------------------------------------------------------------
 * Bypass consumer that subscribes to the IAL bus and forwards
 * GESTURE events into GUI-level counters (or, in the future,
 * direct window-manager messages). This is deliberately a *tee*:
 * it never mutates the legacy g_mouse state machine, so pre-M9
 * paths remain 100% intact.
 * ============================================================ */

#ifndef OPENOS_GUI_INPUT_BRIDGE_H
#define OPENOS_GUI_INPUT_BRIDGE_H

#include <stdint.h>
#include "input_core.h"

/* Initialise and subscribe to the input core. Idempotent. */
void gui_input_bridge_init(void);

/* Shut down the subscription (used by selftest to isolate state). */
void gui_input_bridge_shutdown(void);

/* Diagnostics: counters incremented as gestures flow through. */
uint32_t gui_input_bridge_stat_tap(void);
uint32_t gui_input_bridge_stat_long_press(void);
uint32_t gui_input_bridge_stat_drag_begin(void);
uint32_t gui_input_bridge_stat_drag_move(void);
uint32_t gui_input_bridge_stat_drag_end(void);
uint32_t gui_input_bridge_stat_swipe_left(void);
uint32_t gui_input_bridge_stat_swipe_right(void);
uint32_t gui_input_bridge_stat_swipe_up(void);
uint32_t gui_input_bridge_stat_swipe_down(void);
uint32_t gui_input_bridge_stat_pinch(void);
uint32_t gui_input_bridge_stat_rotate(void);
uint32_t gui_input_bridge_stat_total(void);

/* Reset stats (selftest support). */
void gui_input_bridge_reset_stats(void);

#endif /* OPENOS_GUI_INPUT_BRIDGE_H */
