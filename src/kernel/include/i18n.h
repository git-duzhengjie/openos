/* i18n.h - OpenOS Internationalization (translation keys) */
/* Phase 1: Desktop layer text */
#ifndef OPENOS_I18N_H
#define OPENOS_I18N_H

#include <stdint.h>

typedef enum {
    I18N_LOCALE_EN = 0,
    I18N_LOCALE_ZH = 1,
    I18N_LOCALE_COUNT
} i18n_locale_t;

typedef enum {
    /* Desktop notifications */
    I18N_KEY_NOTIFY_WELCOME = 0,
    I18N_KEY_NOTIFY_THEME_TIP,
    I18N_KEY_NOTIFY_DESKTOP_REFRESHED,

    /* Desktop icons */
    I18N_KEY_ICON_FILES,
    I18N_KEY_ICON_RECYCLE_BIN,

    /* Launcher */
    I18N_KEY_LAUNCHER_TITLE,
    I18N_KEY_APP_TERMINAL,
    I18N_KEY_APP_WINDOW_DEMO,
    I18N_KEY_APP_ABOUT_OPENOS,

    /* Desktop welcome banner (3 lines) */
    I18N_KEY_BANNER_LINE0,
    I18N_KEY_BANNER_LINE1,
    I18N_KEY_BANNER_LINE2,

    /* Desktop right-click context menu */
    I18N_KEY_CTXMENU_OPEN_FILES,
    I18N_KEY_CTXMENU_OPEN_TERMINAL,
    I18N_KEY_CTXMENU_CHANGE_WALLPAPER,
    I18N_KEY_CTXMENU_REFRESH,
    I18N_KEY_CTXMENU_ABOUT,

    I18N_KEY_COUNT
} i18n_key_t;

/* Initialize i18n subsystem; default locale = EN. Idempotent. */
void i18n_init(void);

/* Set current locale (clamped to valid range). Returns 0 on success. */
int i18n_set_locale(i18n_locale_t locale);

/* Get current locale. */
i18n_locale_t i18n_current(void);

/* Translate a key to a UTF-8 / ASCII string in the current locale.
 * Always returns a valid pointer (never NULL); falls back to EN, then key name. */
const char *i18n_t(i18n_key_t key);

#endif /* OPENOS_I18N_H */
