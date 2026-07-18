/* ============================================================
 * openos - M9 GUI Metrics (density abstraction)
 * ------------------------------------------------------------
 * Two densities: DESKTOP (default, matches legacy) and TOUCH.
 * All getters are pure functions of the current density.
 * Legacy #define values in gui.h are preserved to avoid visual
 * regressions in existing code paths. New code should prefer the
 * metrics API.
 * ============================================================ */

#ifndef OPENOS_GUI_METRICS_H
#define OPENOS_GUI_METRICS_H

#include <stdint.h>

typedef enum {
    GUI_DENSITY_DESKTOP = 0,
    GUI_DENSITY_TOUCH   = 1,
} gui_density_t;

typedef struct {
    int taskbar_h;
    int title_h;
    int icon_w;
    int icon_h;
    int launcher_item_h;
    int menu_item_h;
    int scrollbar_w;
    int scrollbar_knob_w;
    int scrollbar_knob_h;
    int scrollbar_thumb_min;
    int min_hit_size;
    int touch_slop_px;
} gui_metrics_t;

void gui_metrics_init(void);
void gui_metrics_set_density(gui_density_t d);
gui_density_t gui_metrics_get_density(void);
const gui_metrics_t *gui_metrics_get(void);

/* Convenience getters (const-fold friendly) */
int gui_metrics_taskbar_h(void);
int gui_metrics_title_h(void);
int gui_metrics_icon_w(void);
int gui_metrics_icon_h(void);
int gui_metrics_launcher_item_h(void);
int gui_metrics_menu_item_h(void);
int gui_metrics_scrollbar_w(void);
int gui_metrics_scrollbar_knob_w(void);
int gui_metrics_scrollbar_knob_h(void);
int gui_metrics_scrollbar_thumb_min(void);
int gui_metrics_min_hit_size(void);
int gui_metrics_touch_slop(void);

/* Notify metrics changed; listeners re-layout. */
typedef void (*gui_metrics_listener_t)(gui_density_t new_density, void *user);
int gui_metrics_add_listener(gui_metrics_listener_t cb, void *user);

#endif /* OPENOS_GUI_METRICS_H */
