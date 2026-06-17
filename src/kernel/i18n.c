/* ============================================================
 * openos - i18n (Internationalization) - Phase 1
 *
 * Lookup table based translation. Two locales bundled:
 *   - EN (English)   : default, used at boot to preserve existing visuals
 *   - ZH (Simplified Chinese): stored for future use; current bitmap
 *                              font is ASCII-only so non-ASCII glyphs
 *                              will not render until a CJK font is
 *                              wired in (Phase 2+).
 *
 * Design rules:
 *   - i18n_t() always returns a non-NULL string.
 *   - If the requested locale lacks a key, fall back to EN.
 *   - If EN also lacks the key (table mismatch), return a literal
 *     "?<key>" placeholder string so it is visible in the UI.
 * ============================================================ */

#include "i18n.h"

static i18n_locale_t g_i18n_locale = I18N_LOCALE_EN;
static int g_i18n_inited = 0;

/* English (default, ASCII) -------------------------------------------------- */
static const char *const k_strings_en[I18N_KEY_COUNT] = {
    /* notifications */
    [I18N_KEY_NOTIFY_WELCOME]            = "Welcome to OpenOS desktop",
    [I18N_KEY_NOTIFY_THEME_TIP]          = "Tip: click Theme icon to switch wallpaper",
    [I18N_KEY_NOTIFY_DESKTOP_REFRESHED]  = "Desktop refreshed",

    /* desktop icons */
    [I18N_KEY_ICON_FILES]                = "Files",
    [I18N_KEY_ICON_RECYCLE_BIN]          = "Recycle Bin",

    /* launcher */
    [I18N_KEY_LAUNCHER_TITLE]            = "OpenOS Launcher",
    [I18N_KEY_APP_TERMINAL]              = "Terminal",
    [I18N_KEY_APP_WINDOW_DEMO]           = "Window Demo",
    [I18N_KEY_APP_ABOUT_OPENOS]          = "About OpenOS",

    /* welcome banner */
    [I18N_KEY_BANNER_LINE0]              = "Welcome to OpenOS",
    [I18N_KEY_BANNER_LINE1]              = "Desktop environment is ready.",
    [I18N_KEY_BANNER_LINE2]              = "Use menu icon to launch tools.",

    /* context menu */
    [I18N_KEY_CTXMENU_OPEN_FILES]        = "Open Files",
    [I18N_KEY_CTXMENU_OPEN_TERMINAL]     = "Open Terminal",
    [I18N_KEY_CTXMENU_CHANGE_WALLPAPER]  = "Change Wallpaper",
    [I18N_KEY_CTXMENU_REFRESH]           = "Refresh",
    [I18N_KEY_CTXMENU_ABOUT]             = "About OpenOS",
};

/* Simplified Chinese (UTF-8) ----------------------------------------------- */
/* Stored for later use; current renderer is ASCII-only.                     */
static const char *const k_strings_zh[I18N_KEY_COUNT] = {
    [I18N_KEY_NOTIFY_WELCOME]            = "\xe6\xac\xa2\xe8\xbf\x8e\xe4\xbd\xbf\xe7\x94\xa8 OpenOS \xe6\xa1\x8c\xe9\x9d\xa2",
    [I18N_KEY_NOTIFY_THEME_TIP]          = "\xe6\x8f\x90\xe7\xa4\xba\xef\xbc\x9a\xe7\x82\xb9\xe5\x87\xbb\xe4\xb8\xbb\xe9\xa2\x98\xe5\x9b\xbe\xe6\xa0\x87\xe5\x88\x87\xe6\x8d\xa2\xe5\xa3\x81\xe7\xba\xb8",
    [I18N_KEY_NOTIFY_DESKTOP_REFRESHED]  = "\xe6\xa1\x8c\xe9\x9d\xa2\xe5\xb7\xb2\xe5\x88\xb7\xe6\x96\xb0",

    [I18N_KEY_ICON_FILES]                = "\xe6\x96\x87\xe4\xbb\xb6",          /* 文件 */
    [I18N_KEY_ICON_RECYCLE_BIN]          = "\xe5\x9b\x9e\xe6\x94\xb6\xe7\xab\x99", /* 回收站 */

    [I18N_KEY_LAUNCHER_TITLE]            = "OpenOS \xe5\xba\x94\xe7\x94\xa8\xe5\x90\xaf\xe5\x8a\xa8\xe5\x99\xa8",
    [I18N_KEY_APP_TERMINAL]              = "\xe7\xbb\x88\xe7\xab\xaf",          /* 终端 */
    [I18N_KEY_APP_WINDOW_DEMO]           = "\xe7\xaa\x97\xe5\x8f\xa3\xe6\xbc\x94\xe7\xa4\xba",
    [I18N_KEY_APP_ABOUT_OPENOS]          = "\xe5\x85\xb3\xe4\xba\x8e OpenOS",

    [I18N_KEY_BANNER_LINE0]              = "\xe6\xac\xa2\xe8\xbf\x8e\xe6\x9d\xa5\xe5\x88\xb0 OpenOS",
    [I18N_KEY_BANNER_LINE1]              = "\xe6\xa1\x8c\xe9\x9d\xa2\xe7\x8e\xaf\xe5\xa2\x83\xe5\xb7\xb2\xe5\xb0\xb1\xe7\xbb\xaa\xe3\x80\x82",
    [I18N_KEY_BANNER_LINE2]              = "\xe7\x82\xb9\xe5\x87\xbb\xe8\x8f\x9c\xe5\x8d\x95\xe5\x9b\xbe\xe6\xa0\x87\xe5\x90\xaf\xe5\x8a\xa8\xe5\xba\x94\xe7\x94\xa8\xe3\x80\x82",

    [I18N_KEY_CTXMENU_OPEN_FILES]        = "\xe6\x89\x93\xe5\xbc\x80\xe6\x96\x87\xe4\xbb\xb6\xe7\xae\xa1\xe7\x90\x86",
    [I18N_KEY_CTXMENU_OPEN_TERMINAL]     = "\xe6\x89\x93\xe5\xbc\x80\xe7\xbb\x88\xe7\xab\xaf",
    [I18N_KEY_CTXMENU_CHANGE_WALLPAPER]  = "\xe6\x9b\xb4\xe6\x8d\xa2\xe5\xa3\x81\xe7\xba\xb8",
    [I18N_KEY_CTXMENU_REFRESH]           = "\xe5\x88\xb7\xe6\x96\xb0",
    [I18N_KEY_CTXMENU_ABOUT]             = "\xe5\x85\xb3\xe4\xba\x8e OpenOS",
};

static const char *const *k_locale_tables[I18N_LOCALE_COUNT] = {
    [I18N_LOCALE_EN] = k_strings_en,
    [I18N_LOCALE_ZH] = k_strings_zh,
};

void i18n_init(void) {
    if (g_i18n_inited) return;
    g_i18n_locale = I18N_LOCALE_EN;
    g_i18n_inited = 1;
}

int i18n_set_locale(i18n_locale_t locale) {
    if ((int)locale < 0 || (int)locale >= I18N_LOCALE_COUNT) return -1;
    g_i18n_locale = locale;
    return 0;
}

i18n_locale_t i18n_current(void) {
    return g_i18n_locale;
}

const char *i18n_t(i18n_key_t key) {
    if ((int)key < 0 || (int)key >= I18N_KEY_COUNT) {
        return "?key";
    }

    /* current locale lookup */
    i18n_locale_t loc = g_i18n_locale;
    if ((int)loc < 0 || (int)loc >= I18N_LOCALE_COUNT) {
        loc = I18N_LOCALE_EN;
    }
    const char *s = k_locale_tables[loc][key];
    if (s && s[0]) return s;

    /* fallback to EN */
    s = k_strings_en[key];
    if (s && s[0]) return s;

    /* table mismatch */
    return "?missing";
}
