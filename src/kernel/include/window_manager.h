#ifndef OPENOS_WINDOW_MANAGER_H
#define OPENOS_WINDOW_MANAGER_H

#include "types.h"
#include "gui.h"

typedef struct wm_stats {
    uint32_t window_count;
    uint32_t visible_windows;
    uint32_t minimized_windows;
    uint32_t active_window_id;
} wm_stats_t;

void window_manager_init(void);
int window_manager_start_desktop(void);
void window_manager_poll(void);
gui_window_t *window_manager_create_window(int x, int y, int w, int h, const char *title);
void window_manager_close_window(gui_window_t *window);
void window_manager_minimize_window(gui_window_t *window);
void window_manager_restore_window(gui_window_t *window);
void window_manager_activate_window(gui_window_t *window);
void window_manager_get_stats(wm_stats_t *stats);
void window_manager_print_info(void);
int window_manager_is_running(void);

#endif /* OPENOS_WINDOW_MANAGER_H */
