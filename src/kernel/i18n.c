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
    [I18N_KEY_APP_SETTINGS]              = "Settings",
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
    [I18N_KEY_CTXMENU_SETTINGS]          = "Settings",
    [I18N_KEY_CTXMENU_ABOUT]             = "About OpenOS",

    /* Phase 2: Window layer ------------------------------------------------ */
    [I18N_KEY_WINDOW_DEFAULT]            = "Window",

    /* demo window */
    [I18N_KEY_WIN_CONTROL_CENTER]        = "OpenOS Control Center",
    [I18N_KEY_DEMO_WELCOME]              = "Welcome to OpenOS Window",
    [I18N_KEY_DEMO_DRAG_HINT]            = "Drag the title bar to move me",
    [I18N_KEY_DEMO_BTN_CLICK]            = "Click",
    [I18N_KEY_DEMO_BTN_MINIMIZE]         = "Minimize",
    [I18N_KEY_DEMO_MVP]                  = "Lightweight Window Manager (MVP)",
    [I18N_KEY_DEMO_FRAMEBUFFER]          = "Framebuffer Composition",

    /* about */
    [I18N_KEY_WIN_ABOUT]                 = "About OpenOS",
    [I18N_KEY_ABOUT_TAGLINE]             = "OpenOS - The lightweight desktop kernel",
    [I18N_KEY_ABOUT_VERSION]             = "Version: 0.17.x",
    [I18N_KEY_ABOUT_BUILD]                = "Build: dev",
    [I18N_KEY_ABOUT_LICENSE]             = "License: MIT",

    /* settings */
    [I18N_KEY_WIN_SETTINGS]              = "Settings",
    [I18N_KEY_SETTINGS_LANGUAGE]          = "Language",
    [I18N_KEY_SETTINGS_TEXT_SIZE]         = "Text size",
    [I18N_KEY_SETTINGS_CURRENT_LANGUAGE]  = "Current language",
    [I18N_KEY_SETTINGS_CURRENT_TEXT_SIZE] = "Current text size",
    [I18N_KEY_SETTINGS_LANGUAGE_ENGLISH]  = "English",
    [I18N_KEY_SETTINGS_LANGUAGE_CHINESE]  = "Chinese",
    [I18N_KEY_SETTINGS_TEXT_SIZE_SMALL]   = "Small",
    [I18N_KEY_SETTINGS_TEXT_SIZE_MEDIUM]  = "Medium",
    [I18N_KEY_SETTINGS_TEXT_SIZE_LARGE]   = "Large",
    [I18N_KEY_SETTINGS_APPLIED]           = "Settings applied",

    /* recycle */
    [I18N_KEY_WIN_RECYCLE_BIN]           = "Recycle Bin",
    [I18N_KEY_RECYCLE_EMPTY]             = "Recycle Bin is empty",

    /* notifications window */
    [I18N_KEY_WIN_NOTIFICATIONS]         = "Notifications",
    [I18N_KEY_NOTIF_TOTAL]               = "Total",
    [I18N_KEY_NOTIF_UNREAD]              = "Unread",
    [I18N_KEY_BTN_CLEAR]                 = "Clear",

    /* terminal */
    [I18N_KEY_WIN_TERMINAL]              = "Terminal",

    /* files browser */
    [I18N_KEY_WIN_FILES]                 = "Files",
    [I18N_KEY_WIN_FILE_VIEWER]           = "File Viewer",
    [I18N_KEY_WIN_FILE_EDITOR]           = "File Editor",
    [I18N_KEY_HEADER_PATH]               = "Path: ",
    [I18N_KEY_HEADER_FILE]               = "File: ",
    [I18N_KEY_HEADER_EDIT]               = "Edit: ",
    [I18N_KEY_PAGE]                      = "Page ",
    [I18N_KEY_PAGE_OF]                   = "/",
    [I18N_KEY_PAGE_OPEN_PAREN]           = " (",
    [I18N_KEY_PAGE_ITEMS]                = " items)",
    [I18N_KEY_LINE]                      = "Line ",
    [I18N_KEY_LINE_DASH]                 = "-",
    [I18N_KEY_LINE_OF]                   = " / ",
    [I18N_KEY_TYPE_UP]                   = "<up>",
    [I18N_KEY_TYPE_FILE]                 = "<file>",
    [I18N_KEY_BTN_NEXT]                  = "Next >",
    [I18N_KEY_BTN_PREV]                  = "< Prev",
    [I18N_KEY_BTN_BACK]                  = "< Back",
    [I18N_KEY_BTN_ENGLISH]               = "English",
    [I18N_KEY_BTN_CHINESE]               = "Chinese",
    [I18N_KEY_BTN_FONT_SMALL]            = "Small",
    [I18N_KEY_BTN_FONT_MEDIUM]           = "Medium",
    [I18N_KEY_BTN_FONT_LARGE]            = "Large",
    [I18N_KEY_BTN_NEW_FILE]              = "New File",
    [I18N_KEY_BTN_NEW_DIR]               = "New Dir",
    [I18N_KEY_BTN_RENAME]                = "Rename",
    [I18N_KEY_BTN_DELETE]                = "Delete",
    [I18N_KEY_BTN_REFRESH]               = "Refresh",
    [I18N_KEY_BTN_EDIT]                  = "Edit",
    [I18N_KEY_BTN_SAVE]                  = "Save",
    [I18N_KEY_BTN_CANCEL]                = "Cancel",
    [I18N_KEY_BTN_OK]                    = "OK",
    [I18N_KEY_BTN_CLOSE]                 = "Close",
    [I18N_KEY_PROMPT_NEW_FILE]           = "Create file name:",
    [I18N_KEY_PROMPT_NEW_DIR]            = "Create directory name:",
    [I18N_KEY_PROMPT_RENAME]             = "Rename to:",
    [I18N_KEY_PROMPT_DELETE_CONFIRM]     = "Delete the selected item?",
    [I18N_KEY_STATUS_INVALID_NAME]       = "Invalid name",
    [I18N_KEY_STATUS_ALREADY_EXISTS]     = "Already exists",
    [I18N_KEY_STATUS_CREATE_FAILED]      = "Create failed",
    [I18N_KEY_STATUS_FILE_CREATED]       = "File created",
    [I18N_KEY_STATUS_MKDIR_FAILED]       = "mkdir failed",
    [I18N_KEY_STATUS_DIR_CREATED]        = "Directory created",
    [I18N_KEY_STATUS_TARGET_EXISTS]      = "Target exists",
    [I18N_KEY_STATUS_RENAME_FAILED]      = "Rename failed",
    [I18N_KEY_STATUS_RENAMED]            = "Renamed",
    [I18N_KEY_STATUS_RMDIR_FAILED]       = "rmdir failed",
    [I18N_KEY_STATUS_DELETE_FAILED]      = "Delete failed",
    [I18N_KEY_STATUS_DELETED]            = "Deleted",
    [I18N_KEY_STATUS_ENTER_TARGET]       = "Enter target inside the dialog",
    [I18N_KEY_STATUS_CLICK_FILE_FIRST]   = "Click a file first",
    [I18N_KEY_STATUS_REFRESHED]          = "Refreshed",
    [I18N_KEY_STATUS_SAVED_PREFIX]       = "Saved: ",

    [I18N_KEY_APP_DEMO_NAME]             = "Window Demo",
};

/* Simplified Chinese (UTF-8) ----------------------------------------------- */
/* Rendered by the generated 16x16 CJK bitmap font backend.                  */
static const char *const k_strings_zh[I18N_KEY_COUNT] = {
    /* notifications */
    [I18N_KEY_NOTIFY_WELCOME]            = "欢迎使用 OpenOS 桌面",
    [I18N_KEY_NOTIFY_THEME_TIP]          = "提示：点击主题图标切换壁纸",
    [I18N_KEY_NOTIFY_DESKTOP_REFRESHED]  = "桌面已刷新",

    /* desktop icons */
    [I18N_KEY_ICON_FILES]                = "文件",
    [I18N_KEY_ICON_RECYCLE_BIN]          = "回收站",

    /* launcher */
    [I18N_KEY_LAUNCHER_TITLE]            = "OpenOS 应用启动器",
    [I18N_KEY_APP_TERMINAL]              = "终端",
    [I18N_KEY_APP_SETTINGS]              = "设置",
    [I18N_KEY_APP_WINDOW_DEMO]           = "窗口演示",
    [I18N_KEY_APP_ABOUT_OPENOS]          = "关于 OpenOS",

    /* welcome banner */
    [I18N_KEY_BANNER_LINE0]              = "欢迎来到 OpenOS",
    [I18N_KEY_BANNER_LINE1]              = "桌面环境已就绪。",
    [I18N_KEY_BANNER_LINE2]              = "点击菜单图标启动应用。",

    /* context menu */
    [I18N_KEY_CTXMENU_OPEN_FILES]        = "打开文件",
    [I18N_KEY_CTXMENU_OPEN_TERMINAL]     = "打开终端",
    [I18N_KEY_CTXMENU_CHANGE_WALLPAPER]  = "更换壁纸",
    [I18N_KEY_CTXMENU_REFRESH]           = "刷新",
    [I18N_KEY_CTXMENU_SETTINGS]          = "设置",
    [I18N_KEY_CTXMENU_ABOUT]             = "关于 OpenOS",

    /* Phase 2: Window layer ------------------------------------------------ */
    [I18N_KEY_WINDOW_DEFAULT]            = "窗口",

    /* demo window */
    [I18N_KEY_WIN_CONTROL_CENTER]        = "OpenOS 控制中心",
    [I18N_KEY_DEMO_WELCOME]              = "欢迎使用 OpenOS 窗口",
    [I18N_KEY_DEMO_DRAG_HINT]            = "拖动标题栏可以移动窗口",
    [I18N_KEY_DEMO_BTN_CLICK]            = "点击",
    [I18N_KEY_DEMO_BTN_MINIMIZE]         = "最小化",
    [I18N_KEY_DEMO_MVP]                  = "轻量级窗口管理器（MVP）",
    [I18N_KEY_DEMO_FRAMEBUFFER]          = "帧缓冲合成",

    /* about */
    [I18N_KEY_WIN_ABOUT]                 = "关于 OpenOS",
    [I18N_KEY_ABOUT_TAGLINE]             = "OpenOS - 轻量级桌面内核",
    [I18N_KEY_ABOUT_VERSION]             = "版本：0.17.x",
    [I18N_KEY_ABOUT_BUILD]               = "构建：dev",
    [I18N_KEY_ABOUT_LICENSE]             = "许可证：MIT",

    /* settings */
    [I18N_KEY_WIN_SETTINGS]              = "设置",
    [I18N_KEY_SETTINGS_LANGUAGE]          = "语言",
    [I18N_KEY_SETTINGS_TEXT_SIZE]         = "文字大小",
    [I18N_KEY_SETTINGS_CURRENT_LANGUAGE]  = "当前语言",
    [I18N_KEY_SETTINGS_CURRENT_TEXT_SIZE] = "当前文字大小",
    [I18N_KEY_SETTINGS_LANGUAGE_ENGLISH]  = "英文",
    [I18N_KEY_SETTINGS_LANGUAGE_CHINESE]  = "中文",
    [I18N_KEY_SETTINGS_TEXT_SIZE_SMALL]   = "小",
    [I18N_KEY_SETTINGS_TEXT_SIZE_MEDIUM]  = "中",
    [I18N_KEY_SETTINGS_TEXT_SIZE_LARGE]   = "大",
    [I18N_KEY_SETTINGS_APPLIED]           = "设置已应用",

    /* recycle */
    [I18N_KEY_WIN_RECYCLE_BIN]           = "回收站",
    [I18N_KEY_RECYCLE_EMPTY]             = "回收站为空",

    /* notifications window */
    [I18N_KEY_WIN_NOTIFICATIONS]         = "通知",
    [I18N_KEY_NOTIF_TOTAL]               = "总计",
    [I18N_KEY_NOTIF_UNREAD]              = "未读",
    [I18N_KEY_BTN_CLEAR]                 = "清空",

    /* terminal */
    [I18N_KEY_WIN_TERMINAL]              = "终端",

    /* files browser */
    [I18N_KEY_WIN_FILES]                 = "文件",
    [I18N_KEY_WIN_FILE_VIEWER]           = "文件查看器",
    [I18N_KEY_WIN_FILE_EDITOR]           = "文件编辑器",
    [I18N_KEY_HEADER_PATH]               = "路径：",
    [I18N_KEY_HEADER_FILE]               = "文件：",
    [I18N_KEY_HEADER_EDIT]               = "编辑：",
    [I18N_KEY_PAGE]                      = "第 ",
    [I18N_KEY_PAGE_OF]                   = " / ",
    [I18N_KEY_PAGE_OPEN_PAREN]           = "（",
    [I18N_KEY_PAGE_ITEMS]                = " 项）",
    [I18N_KEY_LINE]                      = "第 ",
    [I18N_KEY_LINE_DASH]                 = "-",
    [I18N_KEY_LINE_OF]                   = " / ",
    [I18N_KEY_TYPE_UP]                   = "<上级>",
    [I18N_KEY_TYPE_FILE]                 = "<文件>",
    [I18N_KEY_BTN_NEXT]                  = "下一页 >",
    [I18N_KEY_BTN_PREV]                  = "< 上一页",
    [I18N_KEY_BTN_BACK]                  = "< 返回",
    [I18N_KEY_BTN_ENGLISH]               = "英文",
    [I18N_KEY_BTN_CHINESE]               = "中文",
    [I18N_KEY_BTN_FONT_SMALL]            = "小",
    [I18N_KEY_BTN_FONT_MEDIUM]           = "中",
    [I18N_KEY_BTN_FONT_LARGE]            = "大",
    [I18N_KEY_BTN_NEW_FILE]              = "新建文件",
    [I18N_KEY_BTN_NEW_DIR]               = "新建目录",
    [I18N_KEY_BTN_RENAME]                = "重命名",
    [I18N_KEY_BTN_DELETE]                = "删除",
    [I18N_KEY_BTN_REFRESH]               = "刷新",
    [I18N_KEY_BTN_EDIT]                  = "编辑",
    [I18N_KEY_BTN_SAVE]                  = "保存",
    [I18N_KEY_BTN_CANCEL]                = "取消",
    [I18N_KEY_BTN_OK]                    = "确定",
    [I18N_KEY_BTN_CLOSE]                 = "关闭",
    [I18N_KEY_PROMPT_NEW_FILE]           = "请输入文件名：",
    [I18N_KEY_PROMPT_NEW_DIR]            = "请输入目录名：",
    [I18N_KEY_PROMPT_RENAME]             = "重命名为：",
    [I18N_KEY_PROMPT_DELETE_CONFIRM]     = "确定删除所选项目？",
    [I18N_KEY_STATUS_INVALID_NAME]       = "名称无效",
    [I18N_KEY_STATUS_ALREADY_EXISTS]     = "已存在",
    [I18N_KEY_STATUS_CREATE_FAILED]      = "创建失败",
    [I18N_KEY_STATUS_FILE_CREATED]       = "文件已创建",
    [I18N_KEY_STATUS_MKDIR_FAILED]       = "创建目录失败",
    [I18N_KEY_STATUS_DIR_CREATED]        = "目录已创建",
    [I18N_KEY_STATUS_TARGET_EXISTS]      = "目标已存在",
    [I18N_KEY_STATUS_RENAME_FAILED]      = "重命名失败",
    [I18N_KEY_STATUS_RENAMED]            = "已重命名",
    [I18N_KEY_STATUS_RMDIR_FAILED]       = "删除目录失败",
    [I18N_KEY_STATUS_DELETE_FAILED]      = "删除失败",
    [I18N_KEY_STATUS_DELETED]            = "已删除",
    [I18N_KEY_STATUS_ENTER_TARGET]       = "请在对话框中输入目标",
    [I18N_KEY_STATUS_CLICK_FILE_FIRST]   = "请先点击一个文件",
    [I18N_KEY_STATUS_REFRESHED]          = "已刷新",
    [I18N_KEY_STATUS_SAVED_PREFIX]       = "已保存：",

    [I18N_KEY_APP_DEMO_NAME]             = "窗口演示",
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
