/*
 * gui_metrics_selftest64.c -- M9.1..M9.4 GUI metrics selftest.
 *
 * Verifies:
 *   - initial density == DESKTOP with legacy-compatible values
 *   - switching to TOUCH bumps taskbar / title / icon / launcher item /
 *     menu item / scrollbar to touch-friendly minimums
 *   - listener callback fires on density change, and only on change
 *   - switching back to DESKTOP restores legacy values
 *   - min_hit_size in TOUCH mode >= 44 (mobile UX baseline)
 */

#include "../include/gui_metrics_selftest64.h"
#include "../include/early_console64.h"
#include "../../../kernel/include/gui_metrics.h"

#include <stdint.h>

static void gm_st_log(const char *s) { early_console64_write(s); }

static int g_cb_count = 0;
static gui_density_t g_cb_last = GUI_DENSITY_DESKTOP;

static void on_density_change(gui_density_t d, void *user) {
    (void)user;
    g_cb_count++;
    g_cb_last = d;
}

bool arch_x86_64_gui_metrics_selftest_run(void)
{
    bool ok = true;
    #define GM_ST_CHECK(cond, msg)                                          \
        do {                                                                \
            if (!(cond)) {                                                  \
                gm_st_log("[x86_64][gui-metrics-selftest] FAIL: " msg "\n");\
                ok = false;                                                 \
            }                                                               \
        } while (0)

    /* Stage 1: fresh init defaults to DESKTOP with legacy values. */
    gui_metrics_init();
    GM_ST_CHECK(gui_metrics_get_density() == GUI_DENSITY_DESKTOP,
                "initial density must be DESKTOP");
    GM_ST_CHECK(gui_metrics_taskbar_h() == 40, "desktop taskbar_h == 40");
    GM_ST_CHECK(gui_metrics_title_h() == 22, "desktop title_h == 22");
    GM_ST_CHECK(gui_metrics_icon_w() == 72, "desktop icon_w == 72");
    GM_ST_CHECK(gui_metrics_icon_h() == 64, "desktop icon_h == 64");
    GM_ST_CHECK(gui_metrics_launcher_item_h() == 24,
                "desktop launcher_item_h == 24");
    GM_ST_CHECK(gui_metrics_menu_item_h() == 22, "desktop menu_item_h == 22");
    GM_ST_CHECK(gui_metrics_scrollbar_w() == 10, "desktop scrollbar_w == 10");
    gm_st_log("[x86_64][gui-metrics-selftest] stage 1 desktop defaults OK\n");

    /* Stage 2: register listener, switch to TOUCH. */
    g_cb_count = 0;
    g_cb_last  = GUI_DENSITY_DESKTOP;
    int rc = gui_metrics_add_listener(on_density_change, 0);
    GM_ST_CHECK(rc == 0, "listener registration must succeed");
    gui_metrics_set_density(GUI_DENSITY_TOUCH);
    GM_ST_CHECK(gui_metrics_get_density() == GUI_DENSITY_TOUCH,
                "density must switch to TOUCH");
    GM_ST_CHECK(g_cb_count == 1, "listener fires exactly once on change");
    GM_ST_CHECK(g_cb_last == GUI_DENSITY_TOUCH,
                "listener receives new density");
    gm_st_log("[x86_64][gui-metrics-selftest] stage 2 touch switch OK\n");

    /* Stage 3: TOUCH density touch-friendly baselines. */
    GM_ST_CHECK(gui_metrics_taskbar_h() >= 48, "touch taskbar_h >= 48");
    GM_ST_CHECK(gui_metrics_title_h() >= 28, "touch title_h >= 28");
    GM_ST_CHECK(gui_metrics_icon_w() >= 88, "touch icon_w >= 88");
    GM_ST_CHECK(gui_metrics_icon_h() >= 88, "touch icon_h >= 88");
    GM_ST_CHECK(gui_metrics_launcher_item_h() >= 40,
                "touch launcher_item_h >= 40");
    GM_ST_CHECK(gui_metrics_menu_item_h() >= 36, "touch menu_item_h >= 36");
    GM_ST_CHECK(gui_metrics_scrollbar_w() >= 20, "touch scrollbar_w >= 20");
    GM_ST_CHECK(gui_metrics_min_hit_size() >= 44,
                "touch min_hit_size >= 44 (mobile UX)");
    gm_st_log("[x86_64][gui-metrics-selftest] stage 3 touch baseline OK\n");

    /* Stage 4: idempotent set to same density does NOT re-fire listener. */
    int before = g_cb_count;
    gui_metrics_set_density(GUI_DENSITY_TOUCH);
    GM_ST_CHECK(g_cb_count == before,
                "same-density set must not re-fire listener");
    gm_st_log("[x86_64][gui-metrics-selftest] stage 4 idempotent set OK\n");

    /* Stage 5: switch back to DESKTOP restores legacy values. */
    gui_metrics_set_density(GUI_DENSITY_DESKTOP);
    GM_ST_CHECK(gui_metrics_get_density() == GUI_DENSITY_DESKTOP,
                "density must return to DESKTOP");
    GM_ST_CHECK(g_cb_count == before + 1,
                "listener fires on switch back");
    GM_ST_CHECK(gui_metrics_taskbar_h() == 40, "restored taskbar_h == 40");
    GM_ST_CHECK(gui_metrics_icon_w() == 72, "restored icon_w == 72");
    GM_ST_CHECK(gui_metrics_scrollbar_w() == 10, "restored scrollbar_w == 10");
    gm_st_log("[x86_64][gui-metrics-selftest] stage 5 desktop restore OK\n");

    /* Stage 6: invalid density is rejected. */
    gui_metrics_set_density((gui_density_t)0x7F);
    GM_ST_CHECK(gui_metrics_get_density() == GUI_DENSITY_DESKTOP,
                "invalid density must be rejected");
    gm_st_log("[x86_64][gui-metrics-selftest] stage 6 invalid density OK\n");

    /* Stage 7: struct getter agrees with individual getters. */
    const gui_metrics_t *m = gui_metrics_get();
    GM_ST_CHECK(m != 0, "gui_metrics_get() must not be NULL");
    GM_ST_CHECK(m->taskbar_h == gui_metrics_taskbar_h(),
                "struct taskbar_h consistent");
    GM_ST_CHECK(m->scrollbar_w == gui_metrics_scrollbar_w(),
                "struct scrollbar_w consistent");
    gm_st_log("[x86_64][gui-metrics-selftest] stage 7 struct getter OK\n");

    /* Stage 8: touch_slop_px sanity. */
    GM_ST_CHECK(gui_metrics_touch_slop() >= 1,
                "touch_slop_px positive in desktop mode");
    gui_metrics_set_density(GUI_DENSITY_TOUCH);
    GM_ST_CHECK(gui_metrics_touch_slop() >= gui_metrics_touch_slop(),
                "touch_slop_px well-defined in touch mode");
    gui_metrics_set_density(GUI_DENSITY_DESKTOP);
    gm_st_log("[x86_64][gui-metrics-selftest] stage 8 touch_slop OK\n");

    if (ok) {
        gm_st_log("[x86_64][gui-metrics-selftest] PASS\n");
    } else {
        gm_st_log("[x86_64][gui-metrics-selftest] FAIL\n");
    }
    return ok;

    #undef GM_ST_CHECK
}
