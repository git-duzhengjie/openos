#include "window_manager.h"
#include "serial.h"
#include "string.h"

typedef struct window_manager_state {
    int initialized;
    int running;
} window_manager_state_t;

static window_manager_state_t g_wm;

static void wm_write_dec(uint32_t value)
{
    char buf[16];
    uint32_t i = 0;
    uint32_t j;

    if (value == 0) {
        serial_write("0");
        return;
    }

    while (value > 0 && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    for (j = 0; j < i; j++) {
        char ch = buf[i - j - 1];
        serial_putc(ch);
    }
}

void window_manager_init(void)
{
    memset(&g_wm, 0, sizeof(g_wm));
    gui_init();
    g_wm.initialized = 1;
}

int window_manager_start_desktop(void)
{
    if (!g_wm.initialized) {
        window_manager_init();
    }

    if (gui_start_desktop() != 0) {
        return -1;
    }

    g_wm.running = 1;
    return 0;
}

void window_manager_poll(void)
{
    if (!g_wm.running) {
        return;
    }
    gui_poll();
}

gui_window_t *window_manager_create_window(int x, int y, int w, int h, const char *title)
{
    gui_window_t *window;

    if (!g_wm.initialized) {
        window_manager_init();
    }

    window = gui_create_window(x, y, w, h, title);
    if (window) {
        gui_bring_to_front(window);
        gui_set_active_window(window);
        gui_invalidate_all();
        if (g_wm.running) {
            gui_render();
        }
    }
    return window;
}

void window_manager_close_window(gui_window_t *window)
{
    if (!window) {
        return;
    }
    gui_destroy_window(window);
    gui_invalidate_all();
    if (g_wm.running) {
        gui_render();
    }
}

void window_manager_minimize_window(gui_window_t *window)
{
    if (!window) {
        return;
    }
    gui_minimize_window(window);
    if (g_wm.running) {
        gui_render();
    }
}

void window_manager_restore_window(gui_window_t *window)
{
    if (!window) {
        return;
    }
    gui_restore_window(window);
    gui_bring_to_front(window);
    gui_set_active_window(window);
    if (g_wm.running) {
        gui_render();
    }
}

void window_manager_activate_window(gui_window_t *window)
{
    if (!window) {
        return;
    }
    gui_bring_to_front(window);
    gui_set_active_window(window);
    if (g_wm.running) {
        gui_render();
    }
}

void window_manager_get_stats(wm_stats_t *stats)
{
    const gui_system_t *sys;
    uint32_t i;

    if (!stats) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
    sys = gui_get_system();
    if (!sys) {
        return;
    }

    stats->window_count = sys->window_count;
    stats->active_window_id = sys->active_window ? sys->active_window->id : 0;

    for (i = 0; i < sys->window_count; i++) {
        uint32_t idx = sys->z_order[i];
        const gui_window_t *window;

        if (idx >= GUI_MAX_WINDOWS) {
            continue;
        }
        window = &sys->windows[idx];
        if (!window->used || !window->visible) {
            continue;
        }
        stats->visible_windows++;
        if (window->flags & GUI_WINDOW_FLAG_MINIMIZED) {
            stats->minimized_windows++;
        }
    }
}

void window_manager_print_info(void)
{
    wm_stats_t stats;

    window_manager_get_stats(&stats);
    serial_write("[WM] running=");
    wm_write_dec((uint32_t)g_wm.running);
    serial_write(" windows=");
    wm_write_dec(stats.window_count);
    serial_write(" visible=");
    wm_write_dec(stats.visible_windows);
    serial_write(" minimized=");
    wm_write_dec(stats.minimized_windows);
    serial_write(" active=");
    wm_write_dec(stats.active_window_id);
    serial_write("\n");
}

int window_manager_is_running(void)
{
    return g_wm.running;
}
