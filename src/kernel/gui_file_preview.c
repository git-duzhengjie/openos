/*
 * gui_file_preview.c —— 文件预览/文件管理器子系统
 *
 * 从 gui.c 拆分而来（Step 2c 模块化重构）。
 * 职责：文件预览窗口、目录浏览、表格视图、文本/十六进制预览、编辑等。
 *
 * 跨文件接口见 include/gui_internal.h：
 *   - gui.c → 本模块（共享工具）：fp_str_append, fp_itoa, gui_append_uint,
 *     gui_append_hex_byte, gui_format_ipv4_inline, gui_format_mac_inline,
 *     gui_settings_append_field
 *   - 本模块 → gui.c：gui_file_preview_open/_open_file/_open_path（public, gui.h）,
 *     gui_file_preview_window()（访问器，替代跨界 fp_window）
 */
#include "gui.h"
#include "gui_user.h"
#include "font.h"
#include "string.h"
#include "heap.h"
#include "core/fs/vfs.h"
#include "i18n.h"
#include "serial.h"
#include "gui_internal.h"

/* 私有全局：文件预览窗口句柄（原在 gui.c，随模块搬迁） */
static gui_window_t *fp_window;

/* 访问器：供 gui.c 任务栏图标绘制判断窗口类型 */
gui_window_t *gui_file_preview_window(void) { return fp_window; }

/* 通知文本缓冲长度（与 gui.c 中 GUI_NOTIF_TEXT_LEN 保持一致） */
#ifndef GUI_NOTIF_TEXT_LEN
#define GUI_NOTIF_TEXT_LEN 80
#endif

/* 前向声明：目录内容重建（下方定义，事件回调中先行引用） */
static void gui_file_preview_rebuild(void);

/* === File Preview (enhanced) === */

#define GUI_FP_MAX_PATH        256
#define GUI_FP_MAX_NAME        64
#define GUI_FP_MAX_ENTRIES     256
#define GUI_FP_LIST_PER_PAGE   8
#define GUI_FP_VIEW_MAX_LINES  18
#define GUI_FP_VIEW_LINE_CHARS 56
#define GUI_FP_VIEW_BUF_SIZE   8192
#define GUI_FP_EDIT_MAX_LINES  16
#define GUI_FP_EDIT_LINE_CHARS 56

/* enhanced list layout: name | mtime | type | size */
#define GUI_FP_COL_NAME_X      28   /* after 18px icon + 4px gap */
#define GUI_FP_COL_MTIME_X     200
#define GUI_FP_COL_TYPE_X      300
#define GUI_FP_COL_SIZE_X      350
#define GUI_FP_ROW_HEIGHT      20
#define GUI_FP_ICON_SIZE       14


static char          fp_path[GUI_FP_MAX_PATH];
static int           fp_page = 0;
static int           fp_mode = 0;            /* 0=list, 1=view, 2=edit */
static char          fp_view_name[GUI_FP_MAX_NAME];
static int           fp_view_line_offset = 0; /* first visible line index in view mode */
static int           fp_view_total_lines = 0; /* cached total wrapped line count */
static gui_widget_t *fp_edit_textarea = 0;
static gui_widget_t *fp_edit_status = 0;
static int           fp_sort_key  = 0;        /* 0=name, 1=mtime, 2=size */
static int           fp_sort_desc = 0;        /* 0=asc, 1=desc */
static int           fp_sorted_idx[GUI_FP_MAX_ENTRIES];
static int           fp_sorted_count = 0;

/* Prompt overlay: 0=hidden, 1=new file, 2=new dir, 3=rename, 4=confirm delete */
static int           fp_prompt_mode = 0;
static char          fp_prompt_buf[GUI_FP_MAX_NAME];
static int           fp_prompt_len = 0;
static char          fp_prompt_target[GUI_FP_MAX_NAME]; /* original name for rename / delete */
static char          fp_status[80] = {0};
static int           fp_selected = -1;             /* index within current page of selected row */
static int           fp_last_click_global = -1;    /* double-click target in global item index */
static uint32_t      fp_last_click_frame = 0;
static gui_widget_t *fp_prompt_textbox = 0;        /* prompt input textbox */

static int fp_scale_i(int value) {
    return (int)font_scale_value((uint32_t)value);
}

static int fp_line_h(void) {
    int h = (int)font_get_line_height(font_get_default());
    return h > 0 ? h : 10;
}

static int fp_text_row_h(void) {
    return fp_line_h() + fp_scale_i(4);
}

static int fp_button_h(void) {
    int h = fp_line_h() + fp_scale_i(10);
    return h < 22 ? 22 : h;
}

static int fp_panel_gap(void) {
    int gap = fp_scale_i(8);
    return gap < 6 ? 6 : gap;
}

static int fp_view_window_h(void) {
    int h = fp_scale_i(500);
    if (h < 430) h = 430;
    if (h > 560) h = 560;
    return h;
}

static int fp_edit_window_h(void) {
    int h = fp_scale_i(520);
    if (h < 440) h = 440;
    if (h > 580) h = 580;
    return h;
}

static int fp_view_visible_lines(void) {
    int title = fp_line_h() + fp_scale_i(8);
    int nav = fp_button_h();
    int footer = fp_button_h() + fp_panel_gap();
    int available = fp_view_window_h() - GUI_TITLE_HEIGHT - fp_scale_i(16) - title - nav - footer;
    int row_h = fp_text_row_h();
    int lines = available / (row_h > 0 ? row_h : 1);
    if (lines < 4) lines = 4;
    if (lines > GUI_FP_VIEW_MAX_LINES) lines = GUI_FP_VIEW_MAX_LINES;
    return lines;
}

static int fp_edit_visible_lines(void) {
    int title = fp_line_h() + fp_scale_i(8);
    int status = fp_line_h() + fp_panel_gap();
    int footer = fp_button_h() + fp_panel_gap();
    int available = fp_edit_window_h() - GUI_TITLE_HEIGHT - fp_scale_i(16) - title - status - footer;
    int row_h = fp_text_row_h();
    int lines = available / (row_h > 0 ? row_h : 1);
    if (lines < 4) lines = 4;
    if (lines > GUI_FP_EDIT_MAX_LINES) lines = GUI_FP_EDIT_MAX_LINES;
    return lines;
}

/* path helpers --------------------------------------------------- */
static int fp_is_root(void) {
    return fp_path[0] == '/' && fp_path[1] == 0;
}

static void fp_path_set_root(void) {
    fp_path[0] = '/';
    fp_path[1] = 0;
}

static void fp_path_push(const char *name) {
    int len = 0;
    while (fp_path[len]) len++;
    if (!fp_is_root()) {
        if (len < GUI_FP_MAX_PATH - 1) fp_path[len++] = '/';
    }
    while (*name && len < GUI_FP_MAX_PATH - 1) {
        fp_path[len++] = *name++;
    }
    fp_path[len] = 0;
}

static void fp_path_pop(void) {
    int len = 0, last = -1, i;
    while (fp_path[len]) len++;
    if (len <= 1) return;
    for (i = 0; i < len; i++) {
        if (fp_path[i] == '/') last = i;
    }
    if (last <= 0) {
        fp_path_set_root();
    } else {
        fp_path[last] = 0;
    }
}

static void fp_path_join(const char *dir, const char *name, char *out, int cap) {
    int i = 0, j;
    for (j = 0; dir[j] && i < cap - 1; j++) out[i++] = dir[j];
    if (i > 0 && out[i - 1] != '/' && i < cap - 1) out[i++] = '/';
    for (j = 0; name[j] && i < cap - 1; j++) out[i++] = name[j];
    out[i] = 0;
}

static int fp_entry_is_dot(const dentry_t *e) {
    return e->name[0] == '.' &&
           (e->name[1] == 0 ||
            (e->name[1] == '.' && e->name[2] == 0));
}

static int fp_entry_is_dir(const dentry_t *e) {
    return e && e->inode && (e->inode->mode & FS_DIR);
}

/* lowercase compare last suffix; returns 1 if match */
static int fp_str_ieq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static const char *fp_ext(const char *name) {
    const char *dot = 0;
    const char *p = name;
    while (*p) { if (*p == '.') dot = p; p++; }
    return dot ? dot + 1 : "";
}

static gui_icon_id_t fp_pick_icon(const dentry_t *e) {
    const char *ext;
    if (!e) return GUI_ICON_FILE_GENERIC;
    if (fp_entry_is_dir(e)) return GUI_ICON_FOLDER;
    ext = fp_ext(e->name);
    if (!*ext) return GUI_ICON_FILE_GENERIC;
    if (fp_str_ieq(ext, "c") || fp_str_ieq(ext, "h") ||
        fp_str_ieq(ext, "cpp") || fp_str_ieq(ext, "hpp") ||
        fp_str_ieq(ext, "js") || fp_str_ieq(ext, "ts") ||
        fp_str_ieq(ext, "py") || fp_str_ieq(ext, "go") ||
        fp_str_ieq(ext, "rs") || fp_str_ieq(ext, "asm")) return GUI_ICON_FILE_CODE;
    if (fp_str_ieq(ext, "md")) return GUI_ICON_FILE_MARKUP;
    if (fp_str_ieq(ext, "txt") || fp_str_ieq(ext, "log") ||
        fp_str_ieq(ext, "readme")) return GUI_ICON_FILE_TEXT;
    if (fp_str_ieq(ext, "sh") || fp_str_ieq(ext, "bash")) return GUI_ICON_FILE_SHELL;
    if (fp_str_ieq(ext, "conf") || fp_str_ieq(ext, "cfg") ||
        fp_str_ieq(ext, "ini") || fp_str_ieq(ext, "json") ||
        fp_str_ieq(ext, "yaml") || fp_str_ieq(ext, "yml") ||
        fp_str_ieq(ext, "toml")) return GUI_ICON_FILE_CONFIG;
    if (fp_str_ieq(ext, "png") || fp_str_ieq(ext, "jpg") ||
        fp_str_ieq(ext, "jpeg") || fp_str_ieq(ext, "bmp") ||
        fp_str_ieq(ext, "gif") || fp_str_ieq(ext, "ico")) return GUI_ICON_FILE_IMAGE;
    if (fp_str_ieq(ext, "zip") || fp_str_ieq(ext, "tar") ||
        fp_str_ieq(ext, "gz") || fp_str_ieq(ext, "bz2") ||
        fp_str_ieq(ext, "xz") || fp_str_ieq(ext, "7z")) return GUI_ICON_FILE_ARCHIVE;
    if (fp_str_ieq(ext, "elf") || fp_str_ieq(ext, "exe") ||
        fp_str_ieq(ext, "bin") || fp_str_ieq(ext, "o") ||
        fp_str_ieq(ext, "a") || fp_str_ieq(ext, "so")) return GUI_ICON_FILE_EXEC;
    return GUI_ICON_FILE_GENERIC;
}

static const char *fp_type_label(const dentry_t *e) {
    const char *ext;
    if (!e) return "";
    if (fp_entry_is_dir(e)) return i18n_t(I18N_KEY_FILE_TYPE_FOLDER);
    ext = fp_ext(e->name);
    if (!*ext) return i18n_t(I18N_KEY_FILE_TYPE_FILE);
    if (fp_str_ieq(ext, "c") || fp_str_ieq(ext, "h")) return i18n_t(I18N_KEY_FILE_TYPE_C_SOURCE);
    if (fp_str_ieq(ext, "md")) return i18n_t(I18N_KEY_FILE_TYPE_MARKDOWN);
    if (fp_str_ieq(ext, "txt") || fp_str_ieq(ext, "log")) return i18n_t(I18N_KEY_FILE_TYPE_TEXT);
    if (fp_str_ieq(ext, "sh") || fp_str_ieq(ext, "bash")) return i18n_t(I18N_KEY_FILE_TYPE_SHELL);
    if (fp_str_ieq(ext, "json")) return "JSON";
    if (fp_str_ieq(ext, "conf") || fp_str_ieq(ext, "cfg") ||
        fp_str_ieq(ext, "ini") || fp_str_ieq(ext, "toml")) return i18n_t(I18N_KEY_FILE_TYPE_CONFIG);
    if (fp_str_ieq(ext, "yaml") || fp_str_ieq(ext, "yml")) return "YAML";
    if (fp_str_ieq(ext, "png") || fp_str_ieq(ext, "jpg") ||
        fp_str_ieq(ext, "jpeg") || fp_str_ieq(ext, "bmp") ||
        fp_str_ieq(ext, "gif") || fp_str_ieq(ext, "ico")) return i18n_t(I18N_KEY_FILE_TYPE_IMAGE);
    if (fp_str_ieq(ext, "zip") || fp_str_ieq(ext, "tar") ||
        fp_str_ieq(ext, "gz") || fp_str_ieq(ext, "bz2") ||
        fp_str_ieq(ext, "xz") || fp_str_ieq(ext, "7z")) return i18n_t(I18N_KEY_FILE_TYPE_ARCHIVE);
    if (fp_str_ieq(ext, "elf") || fp_str_ieq(ext, "exe") ||
        fp_str_ieq(ext, "bin") || fp_str_ieq(ext, "o") ||
        fp_str_ieq(ext, "a") || fp_str_ieq(ext, "so")) return i18n_t(I18N_KEY_FILE_TYPE_EXEC);
    return i18n_t(I18N_KEY_FILE_TYPE_FILE);
}

/* format file size like "1.2K", "3M" etc */
static void fp_format_size(uint32_t bytes, char *out) {
    char tmp[12];
    int i = 0, j = 0;
    uint32_t n;
    const char *unit = "B";
    if (bytes < 1024) {
        n = bytes;
        unit = "B";
    } else if (bytes < 1024u * 1024u) {
        n = bytes / 1024u;
        unit = "K";
    } else if (bytes < 1024u * 1024u * 1024u) {
        n = bytes / (1024u * 1024u);
        unit = "M";
    } else {
        n = bytes / (1024u * 1024u * 1024u);
        unit = "G";
    }
    if (n == 0) tmp[i++] = '0';
    while (n > 0) { tmp[i++] = (char)('0' + (n % 10)); n /= 10; }
    while (i > 0) out[j++] = tmp[--i];
    while (*unit) out[j++] = *unit++;
    out[j] = 0;
}

static void fp_pad2(char *out, int *pos, int v) {
    out[(*pos)++] = (char)('0' + ((v / 10) % 10));
    out[(*pos)++] = (char)('0' + (v % 10));
}

/* format mtime like "2026-06-17 16:44" or "--" if not set */
static void fp_format_mtime(const vfs_time_t *t, char *out) {
    int pos = 0;
    if (!t || t->year == 0) {
        out[0] = '-'; out[1] = '-'; out[2] = 0;
        return;
    }
    out[pos++] = (char)('0' + ((t->year / 1000) % 10));
    out[pos++] = (char)('0' + ((t->year / 100) % 10));
    out[pos++] = (char)('0' + ((t->year / 10) % 10));
    out[pos++] = (char)('0' + (t->year % 10));
    out[pos++] = '-';
    fp_pad2(out, &pos, t->month);
    out[pos++] = '-';
    fp_pad2(out, &pos, t->day);
    out[pos++] = ' ';
    fp_pad2(out, &pos, t->hour);
    out[pos++] = ':';
    fp_pad2(out, &pos, t->minute);
    out[pos] = 0;
}

/* count real entries (skipping . and ..) ------------------------- */
static int fp_count_entries(void) {
    int i, count = 0;
    dentry_t *e;
    for (i = 0; ; i++) {
        e = vfs_readdir(fp_path, i);
        if (!e) break;
        if (fp_entry_is_dot(e)) continue;
        count++;
    }
    return count;
}

static int fp_total_items(void) {
    int n = fp_count_entries();
    if (!fp_is_root()) n++; /* leading ".." */
    return n;
}

static int fp_row_height(void) {
    int text_h = gui_text_line_height_px();
    int h = text_h + 6;
    if (h < GUI_FP_ROW_HEIGHT) h = GUI_FP_ROW_HEIGHT;
    return h;
}

static int fp_list_per_page(void) {
    int text_h = gui_text_line_height_px();
    int label_h = text_h + 2;
    int button_h = text_h + 6;
    int header_h;
    int row_h = fp_row_height();
    int path_y = 28;
    int nav_y;
    int header_y;
    int sep_y;
    int list_y;
    int prompt_h;
    int usable_h;
    int window_h;
    int rows;

    if (button_h < 20) button_h = 20;
    header_h = button_h;
    nav_y = path_y + label_h + 4;
    header_y = nav_y + button_h + 4;
    sep_y = header_y + header_h + 1;
    list_y = sep_y + 3;
    prompt_h = (fp_prompt_mode == 0) ? label_h : button_h;
    window_h = fp_window ? fp_window->rect.h : 430;

    /* Leave room for toolbar, status/prompt controls and a bottom margin.
     * New File/New Directory/Rename prompts add a textbox plus OK/Cancel
     * buttons at the bottom, so fixed list height can push them outside the
     * window when the default font is scaled up. */
    usable_h = window_h - list_y - button_h - prompt_h - 30;
    if (usable_h < row_h * 3) usable_h = row_h * 3;

    rows = usable_h / row_h;
    if (rows < 3) rows = 3;
    if (rows > GUI_FP_LIST_PER_PAGE) rows = GUI_FP_LIST_PER_PAGE;
    return rows;
}

typedef struct fp_list_layout {
    int x;
    int w;
    int name_x;
    int name_w;
    int mtime_x;
    int mtime_w;
    int type_x;
    int type_w;
    int size_x;
    int size_w;
    int sep_name;
    int sep_mtime;
    int sep_type;
} fp_list_layout_t;

typedef void (*gui_table_sort_handler_t)(gui_widget_t *w, void *ud);
typedef void (*gui_table_row_handler_t)(gui_widget_t *w, void *ud);

typedef struct gui_table_view_column {
    int x;
    int w;
    int sep_x;
    const char *title;
    int sort_key;
    int sortable;
} gui_table_view_column_t;

typedef struct gui_table_view {
    gui_window_t *window;
    int x;
    int y;
    int w;
    int header_h;
    int row_y;
    int row_h;
    int rows;
    int sort_key;
    int sort_desc;
    int selected_row;
    int column_count;
    gui_table_view_column_t columns[4];
    gui_table_sort_handler_t on_sort;
    gui_table_row_handler_t on_row;
} gui_table_view_t;

static int gui_text_width_px(const char *text) {
    int w;
    if (!text) return 0;
    w = (int)font_measure_text_width(font_get_default(), text);
    return w > 0 ? w : (int)strlen(text) * GUI_CHAR_W;
}

static int fp_button_width_for(const char *text, int min_w) {
    int w = gui_text_width_px(text) + 18;
    return w < min_w ? min_w : w;
}

static int fp_default_window_w(void) {
    int pad = fp_scale_i(8);
    int icon_gap = fp_scale_i(24);
    int col_gap = fp_scale_i(8);
    int name_w;
    int mtime_w;
    int type_w;
    int size_w;
    int w;
    if (pad < 6) pad = 6;
    if (icon_gap < 24) icon_gap = 24;
    if (col_gap < 6) col_gap = 6;

    name_w = gui_text_width_px("hello-long-name.txt") + icon_gap + col_gap;
    mtime_w = gui_text_width_px("2026-06-19 23:59") + col_gap * 2;
    type_w = gui_text_width_px(i18n_t(I18N_KEY_FILE_TYPE_FOLDER)) + col_gap * 2;
    size_w = gui_text_width_px("9999K") + col_gap * 2;

    if (name_w < fp_scale_i(160)) name_w = fp_scale_i(160);
    if (mtime_w < fp_scale_i(156)) mtime_w = fp_scale_i(156);
    if (type_w < fp_scale_i(76)) type_w = fp_scale_i(76);
    if (size_w < fp_scale_i(58)) size_w = fp_scale_i(58);

    w = pad * 2 + icon_gap + name_w + mtime_w + type_w + size_w;
    if (w < 560) w = 560;
    if (w > 900) w = 900;
    return w;
}

static void fp_compute_list_layout(fp_list_layout_t *l) {
    int inner_w;
    int pad;
    int icon_gap;
    int col_gap;
    int name_min;
    int mtime_min;
    int type_min;
    int size_min;
    int fixed_w;
    int remaining;
    if (!l) return;

    pad = fp_scale_i(8);
    if (pad < 6) pad = 6;
    col_gap = fp_scale_i(8);
    if (col_gap < 6) col_gap = 6;
    icon_gap = fp_scale_i(24);
    if (icon_gap < 24) icon_gap = 24;

    l->x = pad;
    inner_w = fp_window ? fp_window->rect.w - pad * 2 : fp_default_window_w() - pad * 2;
    if (inner_w < fp_scale_i(320)) inner_w = fp_scale_i(320);
    l->w = inner_w;

    name_min = gui_text_width_px(i18n_t(I18N_KEY_FILE_COL_NAME)) + icon_gap + col_gap;
    mtime_min = gui_text_width_px("2026-06-19 23:59") + col_gap * 2;
    type_min = gui_text_width_px(i18n_t(I18N_KEY_FILE_TYPE_FOLDER)) + col_gap * 2;
    size_min = gui_text_width_px(i18n_t(I18N_KEY_FILE_COL_SIZE)) + col_gap * 2;
    if (name_min < fp_scale_i(132)) name_min = fp_scale_i(132);
    if (mtime_min < fp_scale_i(156)) mtime_min = fp_scale_i(156);
    if (type_min < fp_scale_i(76)) type_min = fp_scale_i(76);
    if (size_min < fp_scale_i(58)) size_min = fp_scale_i(58);

    fixed_w = icon_gap + mtime_min + type_min + size_min;
    remaining = inner_w - fixed_w;
    if (remaining < name_min) {
        remaining = name_min;
        l->w = fixed_w + remaining;
    }

    l->name_x = l->x + col_gap / 2;
    l->name_w = remaining;
    l->sep_name = l->x + icon_gap + l->name_w;
    l->mtime_x = l->sep_name + col_gap;
    l->mtime_w = mtime_min - col_gap;
    l->sep_mtime = l->mtime_x + l->mtime_w + col_gap / 2;
    l->type_x = l->sep_mtime + col_gap;
    l->type_w = type_min - col_gap;
    l->sep_type = l->type_x + l->type_w + col_gap / 2;
    l->size_x = l->sep_type + col_gap;
    l->size_w = l->x + l->w - l->size_x - col_gap / 2;
    if (l->size_w < size_min / 2) l->size_w = size_min / 2;
}

static void gui_table_view_draw_header(gui_table_view_t *table) {
    int i;
    if (!table || !table->window) return;
    gui_add_button(table->window, table->columns[0].x, table->y + 2,
                   table->columns[0].w, table->header_h - 4,
                   table->columns[0].title, table->on_sort, (void*)(intptr_t)table->columns[0].sort_key);
    for (i = 1; i < table->column_count; i++) {
        const gui_table_view_column_t *col = &table->columns[i];
        gui_add_button(table->window, col->x, table->y + 2,
                       col->w, table->header_h - 4,
                       col->title, col->sortable ? table->on_sort : 0,
                       (void*)(intptr_t)col->sort_key);
        gui_raw_line(col->sep_x, table->y + 4, col->sep_x, table->y + table->header_h - 6,
                     gui_rgb(170, 185, 205));
    }
    gui_raw_line(table->x, table->y + table->header_h - 1,
                 table->x + table->w, table->y + table->header_h - 1,
                 gui_rgb(160, 175, 195));
}

static void gui_table_view_draw_row(gui_table_view_t *table, int row, int global_index) {
    uint32_t bg;
    uint32_t border;
    int y;
    if (!table || !table->window) return;
    y = table->row_y + row * table->row_h;
    bg = (row == table->selected_row) ? gui_rgb(215, 232, 255) :
         ((row & 1) ? gui_rgb(248, 250, 252) : gui_rgb(240, 244, 248));
    border = (row == table->selected_row) ? gui_rgb(90, 130, 190) : gui_rgb(210, 218, 228);
    gui_raw_fill_rect(table->x, y, table->w, table->row_h - 1, bg);
    gui_raw_line(table->x, y + table->row_h - 1, table->x + table->w, y + table->row_h - 1, border);
    (void)global_index;
}


static void gui_table_view_draw_text_cell(gui_table_view_t *table, int col, int y, const char *text, uint32_t color) {
    if (!table || col < 0 || col >= table->column_count) return;
    gui_draw_text(table->columns[col].x, y, text, color);
}

static int fp_total_pages(void) {
    int n = fp_total_items();
    int per_page = fp_list_per_page();
    if (n <= 0) return 1;
    return (n + per_page - 1) / per_page;
}

/* fetch the Nth real entry (N=0 = first non-dot child) ---------- */
static dentry_t *fp_get_real_entry(int target) {
    int i, idx = 0;
    dentry_t *e;
    for (i = 0; ; i++) {
        e = vfs_readdir(fp_path, i);
        if (!e) return 0;
        if (fp_entry_is_dot(e)) continue;
        if (idx == target) return e;
        idx++;
    }
}

/* string compare, case-insensitive ------------------------------- */
static int fp_str_cmp_ci(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        a++; b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* vfs_time compare: returns negative/0/positive -------------------- */
static int fp_time_cmp(const vfs_time_t *a, const vfs_time_t *b) {
    if (a->year   != b->year)   return (int)a->year   - (int)b->year;
    if (a->month  != b->month)  return (int)a->month  - (int)b->month;
    if (a->day    != b->day)    return (int)a->day    - (int)b->day;
    if (a->hour   != b->hour)   return (int)a->hour   - (int)b->hour;
    if (a->minute != b->minute) return (int)a->minute - (int)b->minute;
    return (int)a->second - (int)b->second;
}

/* compare two real entries by current sort key -------------------- */
static int fp_compare_real(int ia, int ib) {
    dentry_t *ea = fp_get_real_entry(ia);
    dentry_t *eb = fp_get_real_entry(ib);
    int da, db, r = 0;
    if (!ea || !eb) return 0;
    /* directories always come before files */
    da = fp_entry_is_dir(ea) ? 1 : 0;
    db = fp_entry_is_dir(eb) ? 1 : 0;
    if (da != db) return db - da;  /* dir(1) before file(0) */
    switch (fp_sort_key) {
    case 1: /* mtime */
        if (ea->inode && eb->inode) {
            r = fp_time_cmp(&ea->inode->mtime, &eb->inode->mtime);
        }
        if (r == 0) r = fp_str_cmp_ci(ea->name, eb->name);
        break;
    case 2: /* size */
        if (ea->inode && eb->inode) {
            if (ea->inode->size < eb->inode->size) r = -1;
            else if (ea->inode->size > eb->inode->size) r = 1;
            else r = 0;
        }
        if (r == 0) r = fp_str_cmp_ci(ea->name, eb->name);
        break;
    default: /* name */
        r = fp_str_cmp_ci(ea->name, eb->name);
        break;
    }
    return fp_sort_desc ? -r : r;
}

/* build sorted index over real entries (excludes leading "..") --- */
static void fp_build_sorted_index(void) {
    int n = fp_count_entries();
    int i, j;
    if (n > GUI_FP_MAX_ENTRIES) n = GUI_FP_MAX_ENTRIES;
    for (i = 0; i < n; i++) fp_sorted_idx[i] = i;
    /* simple insertion sort, stable, fine for n <= 256 */
    for (i = 1; i < n; i++) {
        int key = fp_sorted_idx[i];
        j = i - 1;
        while (j >= 0 && fp_compare_real(fp_sorted_idx[j], key) > 0) {
            fp_sorted_idx[j + 1] = fp_sorted_idx[j];
            j--;
        }
        fp_sorted_idx[j + 1] = key;
    }
    fp_sorted_count = n;
}

/* fetch the Nth real entry under current sort order --------------- */
static dentry_t *fp_get_sorted_real_entry(int target) {
    if (target < 0 || target >= fp_sorted_count) return 0;
    return fp_get_real_entry(fp_sorted_idx[target]);
}

/* callbacks ------------------------------------------------------ */
static void fp_on_back(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    fp_mode = 0;
    fp_page = 0;
    fp_view_line_offset = 0;
    fp_view_total_lines = 0;
    gui_file_preview_rebuild();
}

static void fp_on_view_up(gui_widget_t *w, void *ud) {
    int visible = fp_view_visible_lines();
    (void)w; (void)ud;
    if (fp_view_line_offset > 0) {
        fp_view_line_offset -= visible;
        if (fp_view_line_offset < 0) fp_view_line_offset = 0;
        gui_file_preview_rebuild();
    }
}

static void fp_on_view_down(gui_widget_t *w, void *ud) {
    int visible = fp_view_visible_lines();
    (void)w; (void)ud;
    if (fp_view_line_offset + visible < fp_view_total_lines) {
        fp_view_line_offset += visible;
        gui_file_preview_rebuild();
    }
}

static void fp_on_edit_enter(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    fp_mode = 2;
    gui_file_preview_rebuild();
}

static void fp_on_edit_cancel(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    fp_mode = 1;
    gui_file_preview_rebuild();
}

static void fp_on_edit_save(gui_widget_t *w, void *ud) {
    char full[GUI_FP_MAX_PATH];
    const char *src;
    int total = 0;
    int fd;
    int written = 0;
    (void)w; (void)ud;

    src = fp_edit_textarea ? fp_edit_textarea->text : "";
    while (src[total]) total++;

    fp_path_join(fp_path, fp_view_name, full, sizeof(full));
    fd = vfs_open(full, O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        if (fp_edit_status) gui_widget_set_text(fp_edit_status, "Save failed: open");
        gui_render();
        return;
    }
    while (written < total) {
        int n = vfs_write(fd, src + written, total - written);
        if (n <= 0) break;
        written += n;
    }
    vfs_close(fd);
    if (written < total) {
        if (fp_edit_status) gui_widget_set_text(fp_edit_status, "Save failed: write");
        gui_render();
        return;
    }
    /* success: return to view mode */
    {
        char msg[GUI_NOTIF_TEXT_LEN];
        int mp = 0;
        mp = fp_str_append(msg, mp, sizeof(msg), i18n_t(I18N_KEY_STATUS_SAVED_PREFIX));
        mp = fp_str_append(msg, mp, sizeof(msg), fp_view_name);
        (void)mp;
        gui_notify(msg);
    }
    fp_mode = 1;
    fp_view_line_offset = 0;
    gui_file_preview_rebuild();
}

static void fp_clear_entry_click_state(void) {
    fp_selected = -1;
    fp_last_click_global = -1;
    fp_last_click_frame = 0;
}

static void fp_on_prev(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    if (fp_page > 0) {
        fp_page--;
        fp_clear_entry_click_state();
        gui_file_preview_rebuild();
    }
}

static void fp_on_next(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    if (fp_page + 1 < fp_total_pages()) {
        fp_page++;
        fp_clear_entry_click_state();
        gui_file_preview_rebuild();
    }
}

static void fp_open_entry_at_global(int global_index) {
    int real_index;
    dentry_t *e;

    /* item 0 in non-root is the ".." shortcut */
    if (!fp_is_root() && global_index == 0) {
        fp_path_pop();
        fp_page = 0;
        fp_clear_entry_click_state();
        gui_file_preview_rebuild();
        return;
    }

    real_index = fp_is_root() ? global_index : (global_index - 1);
    e = fp_get_sorted_real_entry(real_index);
    if (!e) return;

    if (fp_entry_is_dir(e)) {
        fp_path_push(e->name);
        fp_page = 0;
        fp_clear_entry_click_state();
        gui_file_preview_rebuild();
    } else {
        int i = 0;
        while (e->name[i] && i < GUI_FP_MAX_NAME - 1) {
            fp_view_name[i] = e->name[i];
            i++;
        }
        fp_view_name[i] = 0;
        fp_mode = 1;
        fp_view_line_offset = 0;
        fp_view_total_lines = 0;
        fp_last_click_global = -1;
        fp_last_click_frame = 0;
        gui_file_preview_rebuild();
    }
}

static void fp_on_entry(gui_widget_t *w, void *ud) {
    int slot = (int)(intptr_t)ud;
    int global_index;
    int is_double_click;
    (void)w;

    /* Single click selects only. Double click opens the selected item. */
    fp_selected = slot;
    global_index = fp_page * fp_list_per_page() + slot;
    is_double_click = (fp_last_click_global == global_index &&
                       fp_last_click_frame != 0 &&
                       (g_gui.frame_counter - fp_last_click_frame) < 18);
    fp_last_click_global = global_index;
    fp_last_click_frame = g_gui.frame_counter;

    if (is_double_click) {
        fp_open_entry_at_global(global_index);
    } else {
        gui_file_preview_rebuild();
    }
}

/* sort column click: same column -> toggle direction; new column -> reset to asc */
static void fp_on_sort(gui_widget_t *w, void *ud) {
    int key = (int)(intptr_t)ud;
    (void)w;
    if (key == fp_sort_key) {
        fp_sort_desc = !fp_sort_desc;
    } else {
        fp_sort_key = key;
        fp_sort_desc = 0;
    }
    fp_page = 0;
    gui_file_preview_rebuild();
}

/* ---- File operation helpers ---------------------------------- */
static void fp_status_set(const char *s) {
    int i;
    for (i = 0; i < 79 && s[i]; i++) fp_status[i] = s[i];
    fp_status[i] = 0;
}

static void fp_join_full(const char *name, char *out, int out_sz) {
    int i = 0, j;
    while (i < out_sz - 1 && fp_path[i]) { out[i] = fp_path[i]; i++; }
    if (i > 0 && out[i-1] != '/' && i < out_sz - 1) out[i++] = '/';
    for (j = 0; name[j] && i < out_sz - 1; j++) out[i++] = name[j];
    out[i] = 0;
}

static int fp_name_exists(const char *name) {
    dentry_t *e;
    int i = 0;
    if (!name[0]) return 1;
    while ((e = vfs_readdir(fp_path, i++)) != 0) {
        const char *n = e->name;
        int k;
        for (k = 0; n[k] && name[k] && n[k] == name[k]; k++) {}
        if (n[k] == 0 && name[k] == 0) return 1;
    }
    return 0;
}

static int fp_name_is_valid(const char *name) {
    int i;
    if (!name[0]) return 0;
    if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) return 0;
    for (i = 0; name[i]; i++) {
        char c = name[i];
        if (c == '/' || c == 0 || (unsigned char)c < 32) return 0;
    }
    return 1;
}

static void fp_prompt_open(int mode, const char *initial) {
    int i;
    fp_prompt_mode = mode;
    fp_prompt_len = 0;
    fp_prompt_buf[0] = 0;
    fp_prompt_target[0] = 0;
    if (initial) {
        for (i = 0; i < GUI_FP_MAX_NAME - 1 && initial[i]; i++) {
            fp_prompt_buf[i] = initial[i];
            fp_prompt_target[i] = initial[i];
        }
        fp_prompt_buf[i] = 0;
        fp_prompt_target[i] = 0;
        fp_prompt_len = i;
    }
    gui_file_preview_rebuild();
}

static void fp_prompt_close(void) {
    fp_prompt_mode = 0;
    fp_prompt_len = 0;
    fp_prompt_buf[0] = 0;
    fp_prompt_target[0] = 0;
    gui_file_preview_rebuild();
}

static void fp_action_new_file(const char *name) {
    char full[GUI_FP_MAX_PATH];
    int fd;
    if (!fp_name_is_valid(name)) { fp_status_set(i18n_t(I18N_KEY_STATUS_INVALID_NAME)); return; }
    if (fp_name_exists(name))    { fp_status_set(i18n_t(I18N_KEY_STATUS_ALREADY_EXISTS)); return; }
    fp_join_full(name, full, sizeof(full));
    fd = vfs_open(full, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) { fp_status_set(i18n_t(I18N_KEY_STATUS_CREATE_FAILED)); return; }
    vfs_close(fd);
    fp_status_set(i18n_t(I18N_KEY_STATUS_FILE_CREATED));
    gui_notify(i18n_t(I18N_KEY_STATUS_FILE_CREATED));
}

static void fp_action_new_dir(const char *name) {
    char full[GUI_FP_MAX_PATH];
    if (!fp_name_is_valid(name)) { fp_status_set(i18n_t(I18N_KEY_STATUS_INVALID_NAME)); return; }
    if (fp_name_exists(name))    { fp_status_set(i18n_t(I18N_KEY_STATUS_ALREADY_EXISTS)); return; }
    fp_join_full(name, full, sizeof(full));
    if (vfs_mkdir(full, 0755) < 0) { fp_status_set(i18n_t(I18N_KEY_STATUS_MKDIR_FAILED)); return; }
    fp_status_set(i18n_t(I18N_KEY_STATUS_DIR_CREATED));
    gui_notify(i18n_t(I18N_KEY_STATUS_DIR_CREATED));
}

static void fp_action_rename(const char *old_name, const char *new_name) {
    char src[GUI_FP_MAX_PATH];
    char dst[GUI_FP_MAX_PATH];
    if (!fp_name_is_valid(new_name)) { fp_status_set(i18n_t(I18N_KEY_STATUS_INVALID_NAME)); return; }
    if (fp_name_exists(new_name))    { fp_status_set(i18n_t(I18N_KEY_STATUS_TARGET_EXISTS)); return; }
    fp_join_full(old_name, src, sizeof(src));
    fp_join_full(new_name, dst, sizeof(dst));
    if (vfs_rename(src, dst) < 0) { fp_status_set(i18n_t(I18N_KEY_STATUS_RENAME_FAILED)); return; }
    fp_status_set(i18n_t(I18N_KEY_STATUS_RENAMED));
    gui_notify(i18n_t(I18N_KEY_STATUS_RENAMED));
}

static void fp_action_delete(const char *name, int is_dir) {
    char full[GUI_FP_MAX_PATH];
    int r;
    fp_join_full(name, full, sizeof(full));
    r = is_dir ? vfs_rmdir(full) : vfs_unlink(full);
    if (r < 0) { fp_status_set(is_dir ? i18n_t(I18N_KEY_STATUS_RMDIR_FAILED) : i18n_t(I18N_KEY_STATUS_DELETE_FAILED)); return; }
    fp_status_set(i18n_t(I18N_KEY_STATUS_DELETED));
    gui_notify(i18n_t(I18N_KEY_STATUS_DELETED));
}

/* Detect if fp_prompt_target refers to a directory in current dir */
static int fp_target_is_dir(void) {
    int i;
    dentry_t *e;
    for (i = 0; ; i++) {
        e = vfs_readdir(fp_path, i);
        if (!e) break;
        if (e->name[0]) {
            const char *a = e->name;
            const char *b = fp_prompt_target;
            int k;
            for (k = 0; a[k] && b[k] && a[k] == b[k]; k++) {}
            if (a[k] == 0 && b[k] == 0) {
                return (e->inode && (e->inode->mode & FS_DIR)) ? 1 : 0;
            }
        }
    }
    return 0;
}

static void fp_prompt_submit(void) {
    int mode = fp_prompt_mode;
    /* sync textbox content -> fp_prompt_buf */
    if (fp_prompt_textbox && (mode == 1 || mode == 2 || mode == 3)) {
        int i;
        for (i = 0; i < (int)sizeof(fp_prompt_buf) - 1 && fp_prompt_textbox->text[i]; i++) {
            fp_prompt_buf[i] = fp_prompt_textbox->text[i];
        }
        fp_prompt_buf[i] = 0;
        fp_prompt_len = i;
    }
    if (mode == 4) {
        /* delete confirm: target already set */
        if (fp_prompt_target[0]) fp_action_delete(fp_prompt_target, fp_target_is_dir());
        fp_prompt_close();
        return;
    }
    if (!fp_prompt_buf[0]) { fp_prompt_close(); return; }
    if      (mode == 1) fp_action_new_file(fp_prompt_buf);
    else if (mode == 2) fp_action_new_dir(fp_prompt_buf);
    else if (mode == 3) fp_action_rename(fp_prompt_target, fp_prompt_buf);
    fp_prompt_close();
}

/* Toolbar callbacks ---------------------------------------------- */
static void fp_get_selected_name(char *out, int out_sz) {
    int gidx, real_index;
    dentry_t *e;
    int i;
    out[0] = 0;
    if (fp_selected < 0) return;
    gidx = fp_page * fp_list_per_page() + fp_selected;
    if (gidx >= fp_total_items()) return;
    if (!fp_is_root() && gidx == 0) return; /* '..' not selectable */
    real_index = fp_is_root() ? gidx : (gidx - 1);
    e = fp_get_sorted_real_entry(real_index);
    if (!e || !e->name[0]) return;
    for (i = 0; e->name[i] && i < out_sz - 1; i++) out[i] = e->name[i];
    out[i] = 0;
}

static void fp_on_tb_new_file(gui_widget_t *w, void *ud) { (void)w; (void)ud; fp_prompt_open(1, ""); gui_file_preview_rebuild(); }
static void fp_on_tb_new_dir(gui_widget_t *w, void *ud)  { (void)w; (void)ud; fp_prompt_open(2, ""); gui_file_preview_rebuild(); }
static void fp_on_tb_rename(gui_widget_t *w, void *ud) {
    char name[GUI_FP_MAX_NAME];
    (void)w; (void)ud;
    fp_get_selected_name(name, sizeof(name));
    fp_prompt_open(3, name);
    if (!name[0]) fp_status_set(i18n_t(I18N_KEY_STATUS_ENTER_TARGET));
    gui_file_preview_rebuild();
}
static void fp_on_tb_delete(gui_widget_t *w, void *ud) {
    char name[GUI_FP_MAX_NAME];
    (void)w; (void)ud;
    fp_get_selected_name(name, sizeof(name));
    if (!name[0]) { fp_status_set(i18n_t(I18N_KEY_STATUS_CLICK_FILE_FIRST)); gui_file_preview_rebuild(); return; }
    fp_prompt_open(4, name);
    gui_file_preview_rebuild();
}
static void fp_on_tb_refresh(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    fp_status_set(i18n_t(I18N_KEY_STATUS_REFRESHED));
    gui_file_preview_rebuild();
}
static void fp_on_prompt_ok(gui_widget_t *w, void *ud)     { (void)w; (void)ud; fp_prompt_submit(); gui_file_preview_rebuild(); }
static void fp_on_prompt_cancel(gui_widget_t *w, void *ud) { (void)w; (void)ud; fp_prompt_close(); gui_file_preview_rebuild(); }

/* render list mode ---------------------------------------------- */
static void gui_file_preview_render_list(void) {
    char header[GUI_FP_MAX_PATH + 16];
    char pageinfo[32];
    char buf[12];
    gui_widget_t *btn;
    gui_widget_t *lbl;
    int y, slot, total_pages, total_items, base;
    int pos;
    int text_h, label_h, button_h, header_h, row_h, list_count;
    int path_y, nav_y, page_y, header_y, sep_y, list_y, toolbar_y, status_y;
    fp_list_layout_t layout;
    gui_table_view_t table;

    if (!fp_window) return;

    /* build sorted index for current directory */
    fp_build_sorted_index();

    text_h = gui_text_line_height_px();
    label_h = text_h + 2;
    button_h = text_h + 6;
    if (button_h < 20) button_h = 20;
    header_h = button_h;
    row_h = fp_row_height();
    list_count = fp_list_per_page();
    path_y = 28;
    nav_y = path_y + label_h + 4;
    page_y = gui_text_center_y(nav_y, button_h);
    header_y = nav_y + button_h + 4;
    sep_y = header_y + header_h + 1;
    list_y = sep_y + 3;
    toolbar_y = list_y + list_count * row_h + 8;
    status_y = toolbar_y + button_h + 6;
    fp_compute_list_layout(&layout);
    table.window = fp_window;
    table.x = layout.x;
    table.y = header_y;
    table.w = layout.w;
    table.header_h = header_h;
    table.row_y = list_y;
    table.row_h = row_h;
    table.rows = list_count;
    table.sort_key = fp_sort_key;
    table.sort_desc = fp_sort_desc;
    table.selected_row = fp_selected;
    table.column_count = 4;
    table.on_sort = fp_on_sort;
    table.on_row = fp_on_entry;
    table.columns[0].x = layout.x;
    table.columns[0].w = layout.sep_name - layout.x;
    table.columns[0].sep_x = layout.sep_name;
    table.columns[0].title = 0;
    table.columns[0].sort_key = 0;
    table.columns[0].sortable = 1;
    table.columns[1].x = layout.mtime_x - 4;
    table.columns[1].w = layout.sep_mtime - layout.mtime_x + 4;
    table.columns[1].sep_x = layout.sep_name;
    table.columns[1].title = 0;
    table.columns[1].sort_key = 1;
    table.columns[1].sortable = 1;
    table.columns[2].x = layout.type_x;
    table.columns[2].w = layout.type_w;
    table.columns[2].sep_x = layout.sep_mtime;
    table.columns[2].title = 0;
    table.columns[2].sort_key = -1;
    table.columns[2].sortable = 0;
    table.columns[3].x = layout.size_x - 4;
    table.columns[3].w = layout.x + layout.w - layout.size_x + 4;
    table.columns[3].sep_x = layout.sep_type;
    table.columns[3].title = 0;
    table.columns[3].sort_key = 2;
    table.columns[3].sortable = 1;

    /* path header */
    pos = 0;
    pos = fp_str_append(header, pos, sizeof(header), i18n_t(I18N_KEY_HEADER_PATH));
    pos = fp_str_append(header, pos, sizeof(header), fp_path);
    (void)pos;
    gui_add_label(fp_window, layout.x, path_y, layout.w, label_h, header);

    /* nav buttons */
    {
        int prev_w = fp_button_width_for(i18n_t(I18N_KEY_BTN_PREV), 60);
        int next_w = fp_button_width_for(i18n_t(I18N_KEY_BTN_NEXT), 60);
        int nav_gap = 8;
        btn = gui_add_button(fp_window, layout.x, nav_y, prev_w, button_h, i18n_t(I18N_KEY_BTN_PREV), fp_on_prev, 0);
        (void)btn;
        btn = gui_add_button(fp_window, layout.x + prev_w + nav_gap, nav_y, next_w, button_h, i18n_t(I18N_KEY_BTN_NEXT), fp_on_next, 0);
        (void)btn;
    }

    total_pages = fp_total_pages();
    total_items = fp_total_items();
    pos = 0;
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), i18n_t(I18N_KEY_PAGE));
    fp_itoa(fp_page + 1, buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), i18n_t(I18N_KEY_PAGE_OF));
    fp_itoa(total_pages, buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), i18n_t(I18N_KEY_PAGE_OPEN_PAREN));
    fp_itoa(total_items, buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), i18n_t(I18N_KEY_PAGE_ITEMS));
    (void)pos;
    {
        int prev_w = fp_button_width_for(i18n_t(I18N_KEY_BTN_PREV), 60);
        int next_w = fp_button_width_for(i18n_t(I18N_KEY_BTN_NEXT), 60);
        int nav_gap = 8;
        int page_x = layout.x + prev_w + next_w + nav_gap * 2;
        int page_w = layout.x + layout.w - page_x;
        if (page_w < 80) page_w = 80;
        gui_add_label(fp_window, page_x, page_y, page_w, label_h, pageinfo);
    }

    /* toolbar: New File | New Dir | Rename | Delete | Refresh */
    {
        int tx = layout.x;
        int gap = 5;
        int w_new_file = fp_button_width_for(i18n_t(I18N_KEY_BTN_NEW_FILE), 84);
        int w_new_dir = fp_button_width_for(i18n_t(I18N_KEY_BTN_NEW_DIR), 84);
        int w_rename = fp_button_width_for(i18n_t(I18N_KEY_BTN_RENAME), 72);
        int w_delete = fp_button_width_for(i18n_t(I18N_KEY_BTN_DELETE), 72);
        int w_refresh = fp_button_width_for(i18n_t(I18N_KEY_BTN_REFRESH), 84);
        btn = gui_add_button(fp_window, tx, toolbar_y, w_new_file, button_h, i18n_t(I18N_KEY_BTN_NEW_FILE), fp_on_tb_new_file, 0);
        if (btn) { btn->bg_color = gui_rgb(220, 235, 220); }
        tx += w_new_file + gap;
        btn = gui_add_button(fp_window, tx, toolbar_y, w_new_dir, button_h, i18n_t(I18N_KEY_BTN_NEW_DIR),  fp_on_tb_new_dir, 0);
        if (btn) { btn->bg_color = gui_rgb(220, 235, 220); }
        tx += w_new_dir + gap;
        btn = gui_add_button(fp_window, tx, toolbar_y, w_rename, button_h, i18n_t(I18N_KEY_BTN_RENAME),   fp_on_tb_rename, 0);
        tx += w_rename + gap;
        btn = gui_add_button(fp_window, tx, toolbar_y, w_delete, button_h, i18n_t(I18N_KEY_BTN_DELETE),   fp_on_tb_delete, 0);
        if (btn) { btn->bg_color = gui_rgb(245, 220, 220); }
        tx += w_delete + gap;
        btn = gui_add_button(fp_window, tx, toolbar_y, w_refresh, button_h, i18n_t(I18N_KEY_BTN_REFRESH),  fp_on_tb_refresh, 0);
    }

    /* status / prompt area at the very bottom */
    if (fp_prompt_mode == 0) {
        if (fp_status[0]) {
            lbl = gui_add_label(fp_window, layout.x, status_y, layout.w, label_h, fp_status);
            if (lbl) lbl->fg_color = gui_rgb(80, 80, 120);
        }
    } else {
        char promptlabel[80];
        const char *title_s = "";
        int pp = 0;
        if      (fp_prompt_mode == 1) title_s = i18n_t(I18N_KEY_PROMPT_NEW_FILE);
        else if (fp_prompt_mode == 2) title_s = i18n_t(I18N_KEY_PROMPT_NEW_DIR);
        else if (fp_prompt_mode == 3) title_s = i18n_t(I18N_KEY_PROMPT_RENAME);
        else if (fp_prompt_mode == 4) title_s = i18n_t(I18N_KEY_PROMPT_DELETE_CONFIRM);
        while (title_s[pp] && pp < 60) { promptlabel[pp] = title_s[pp]; pp++; }
        promptlabel[pp] = 0;
        {
            int ok_w = fp_button_width_for(i18n_t(I18N_KEY_BTN_OK), 56);
            int cancel_w = fp_button_width_for(i18n_t(I18N_KEY_BTN_CANCEL), 72);
            int prompt_w = gui_text_width_px(promptlabel) + 10;
            int buttons_w = ok_w + cancel_w + 8;
            int field_x;
            int field_w;
            if (prompt_w < 160) prompt_w = 160;
            if (prompt_w > layout.w / 3) prompt_w = layout.w / 3;
            field_x = layout.x + prompt_w + 8;
            field_w = layout.x + layout.w - field_x - buttons_w - 12;
            if (field_w < 120) field_w = 120;
            lbl = gui_add_label(fp_window, layout.x, status_y, prompt_w, label_h, promptlabel);
            if (lbl) lbl->fg_color = gui_rgb(40, 40, 100);
            if (fp_prompt_mode == 4) {
                /* delete confirm: show name as readonly label */
                lbl = gui_add_label(fp_window, field_x, status_y, field_w, label_h, fp_prompt_target);
                if (lbl) lbl->fg_color = gui_rgb(180, 60, 60);
            } else {
                fp_prompt_textbox = gui_add_textbox(fp_window, field_x, status_y - 2, field_w, button_h, fp_prompt_buf);
                if (fp_prompt_textbox) {
                    /* sync displayed text into buf via length */
                    fp_prompt_len = 0;
                    while (fp_prompt_buf[fp_prompt_len]) fp_prompt_len++;
                }
            }
            btn = gui_add_button(fp_window, layout.x + layout.w - buttons_w, status_y - 2, ok_w, button_h, i18n_t(I18N_KEY_BTN_OK),     fp_on_prompt_ok, 0);
            if (btn) btn->bg_color = gui_rgb(200, 230, 200);
            btn = gui_add_button(fp_window, layout.x + layout.w - cancel_w, status_y - 2, cancel_w, button_h, i18n_t(I18N_KEY_BTN_CANCEL), fp_on_prompt_cancel, 0);
            if (btn) btn->bg_color = gui_rgb(240, 220, 220);
        }
    }

    /* column header row: clickable sort buttons | separators */
    {
        char hname[32], hmod[32], htype[24], hsize[24];
        const char *arrow_no = "  ";
        const char *suf_n, *suf_m, *suf_t, *suf_s;
        uint32_t hdr_fg = gui_rgb(80, 80, 90);
        uint32_t hdr_bg = gui_rgb(232, 232, 240);
        uint32_t sel_bg = gui_rgb(208, 220, 244);
        uint32_t sep_color = gui_rgb(200, 200, 210);
        /* ascii arrows: ^ down, v up; use ASCII to avoid font issues */
        suf_n = (fp_sort_key == 0) ? (fp_sort_desc ? " v" : " ^") : arrow_no;
        suf_m = (fp_sort_key == 1) ? (fp_sort_desc ? " v" : " ^") : arrow_no;
        suf_t = arrow_no;
        suf_s = (fp_sort_key == 2) ? (fp_sort_desc ? " v" : " ^") : arrow_no;

        hname[0] = 0;
        hmod[0] = 0;
        htype[0] = 0;
        hsize[0] = 0;
        fp_str_append(hname, 0, (int)sizeof(hname), i18n_t(I18N_KEY_FILE_COL_NAME));
        fp_str_append(hmod, 0, (int)sizeof(hmod), i18n_t(I18N_KEY_FILE_COL_MODIFIED));
        fp_str_append(htype, 0, (int)sizeof(htype), i18n_t(I18N_KEY_FILE_COL_TYPE));
        fp_str_append(hsize, 0, (int)sizeof(hsize), i18n_t(I18N_KEY_FILE_COL_SIZE));
        fp_str_append(hname, (int)strlen(hname), (int)sizeof(hname), suf_n);
        fp_str_append(hmod, (int)strlen(hmod), (int)sizeof(hmod), suf_m);
        fp_str_append(htype, (int)strlen(htype), (int)sizeof(htype), suf_t);
        fp_str_append(hsize, (int)strlen(hsize), (int)sizeof(hsize), suf_s);

        table.columns[0].title = hname;
        table.columns[1].title = hmod;
        table.columns[2].title = htype;
        table.columns[3].title = hsize;
        gui_table_view_draw_header(&table);
        (void)hdr_bg;
        (void)sel_bg;
        (void)sep_color;
        (void)hdr_fg;
    }

    /* entries */
    y = list_y;
    base = fp_page * list_count;
    for (slot = 0; slot < list_count; slot++) {
        char line[80];
        char sizebuf[12];
        char mtimebuf[24];
        const char *type_str;
        const char *display_name;
        int gidx = base + slot;
        int real_index;
        dentry_t *e = 0;
        gui_icon_id_t icon;
        uint32_t fsize = 0;
        const vfs_time_t *mt = 0;
        int p = 0;
        int col;
        int selected;
        int hover;
        int is_parent = 0;

        if (gidx >= total_items) break;
        selected = (slot == fp_selected);
        hover = 0;

        if (!fp_is_root() && gidx == 0) {
            is_parent = 1;
            icon = GUI_ICON_UPDIR;
            display_name = "..";
            type_str = i18n_t(I18N_KEY_TYPE_UP);
            mtimebuf[0] = '-'; mtimebuf[1] = '-'; mtimebuf[2] = 0;
            sizebuf[0] = '-'; sizebuf[1] = '-'; sizebuf[2] = 0;
        } else {
            real_index = fp_is_root() ? gidx : (gidx - 1);
            e = fp_get_sorted_real_entry(real_index);
            if (!e) break;
            icon = fp_pick_icon(e);
            display_name = e->name;
            type_str = fp_type_label(e);
            if (e->inode) {
                fsize = e->inode->size;
                mt = &e->inode->mtime;
            }
            if (fp_entry_is_dir(e)) {
                sizebuf[0] = '-'; sizebuf[1] = '-'; sizebuf[2] = 0;
            } else {
                fp_format_size(fsize, sizebuf);
            }
            fp_format_mtime(mt, mtimebuf);
        }

        /* Keep file rows persistent across window redraws.  The previous
         * implementation drew row contents with gui_raw_* only; a later window
         * redraw cleared those pixels and left the directory body blank even
         * though the entry count was correct.  Add real widgets for the row
         * background and labels, then put a transparent hit target on top. */
        line[0] = 0;
        gui_table_view_draw_row(&table, slot, slot);
        (void)line;

        {
            gui_widget_t *name_label;
            gui_widget_t *mtime_label;
            gui_widget_t *type_label;
            gui_widget_t *size_label;
            gui_widget_t *hit;
            int label_y = y + 1;
            int label_h = row_h - 2;
            if (label_h < 1) label_h = 1;

            name_label = gui_add_label(fp_window, layout.name_x, label_y,
                                       layout.name_w, label_h, display_name);
            if (name_label) {
                name_label->fg_color = gui_rgb(24, 28, 34);
                name_label->icon = icon;
            }
            mtime_label = gui_add_label(fp_window, layout.mtime_x + 4, label_y,
                                        layout.mtime_w - 8, label_h, mtimebuf);
            if (mtime_label) mtime_label->fg_color = gui_rgb(42, 46, 54);
            type_label = gui_add_label(fp_window, layout.type_x + 4, label_y,
                                       layout.type_w - 8, label_h, type_str);
            if (type_label) type_label->fg_color = gui_rgb(42, 46, 54);
            size_label = gui_add_label(fp_window, layout.size_x + 4, label_y,
                                       layout.size_w - 8, label_h, sizebuf);
            if (size_label) size_label->fg_color = gui_rgb(42, 46, 54);

            hit = gui_add_button(fp_window, table.x, y, table.w, row_h - 1,
                                 "", fp_on_entry, (void*)(intptr_t)slot);
            if (hit) hit->button_flags |= GUI_BUTTON_FLAG_TRANSPARENT;
        }

        y += row_h;
        (void)is_parent;
        (void)p;
        (void)col;
    }
}

/* Returns 1 if name ends with .md / .MD */
static int fp_is_markdown(const char *name) {
    int n = 0;
    while (name[n]) n++;
    if (n < 3) return 0;
    if (name[n-3] == '.' &&
        (name[n-2] == 'm' || name[n-2] == 'M') &&
        (name[n-1] == 'd' || name[n-1] == 'D')) return 1;
    return 0;
}

/* Format a single raw line as markdown.
 * Inputs: src (NUL-terminated). Outputs: dst (caller buffer, dst_sz), *out_color.
 * Returns number of chars written (excluding NUL). */
static int fp_md_format_line(const char *src, char *dst, int dst_sz, uint32_t *out_color) {
    int sp = 0, dp = 0;
    int level = 0;
    int bullet = 0;
    int rule = 0;
    int code = 0;
    int quote = 0;
    int i, k, n;

    /* count leading spaces */
    while (src[sp] == ' ' && sp < 8) sp++;

    /* heading */
    if (src[sp] == '#') {
        k = sp;
        while (src[k] == '#' && level < 6) { level++; k++; }
        if (src[k] == ' ' || src[k] == 0) {
            /* skip spaces */
            while (src[k] == ' ') k++;
            /* emit prefix */
            for (i = 0; i < level && dp < dst_sz - 1; i++) dst[dp++] = '#';
            if (dp < dst_sz - 1) dst[dp++] = ' ';
            while (src[k] && dp < dst_sz - 1) dst[dp++] = src[k++];
            dst[dp] = 0;
            *out_color = (level == 1) ? gui_rgb(180, 60, 40) :
                         (level == 2) ? gui_rgb(150, 80, 40) :
                                        gui_rgb(120, 100, 40);
            return dp;
        }
    }

    /* horizontal rule: --- *** ___ */
    if ((src[sp] == '-' || src[sp] == '*' || src[sp] == '_')) {
        char ch = src[sp];
        n = 0;
        for (k = sp; src[k] == ch; k++) n++;
        if (n >= 3 && (src[k] == 0 || src[k] == ' ')) rule = 1;
    }
    if (rule) {
        for (i = 0; i < 50 && dp < dst_sz - 1; i++) dst[dp++] = '-';
        dst[dp] = 0;
        *out_color = gui_rgb(120, 120, 120);
        return dp;
    }

    /* unordered bullet: - or * followed by space */
    if ((src[sp] == '-' || src[sp] == '*' || src[sp] == '+') && src[sp+1] == ' ') {
        bullet = 1;
        k = sp + 2;
        if (dp < dst_sz - 1) dst[dp++] = ' ';
        if (dp < dst_sz - 1) dst[dp++] = ' ';
        if (dp < dst_sz - 1) dst[dp++] = (char)0x95 & 0x7f; /* fallback dot */
        dp--;
        if (dp < dst_sz - 1) dst[dp++] = '*';
        if (dp < dst_sz - 1) dst[dp++] = ' ';
        while (src[k] && dp < dst_sz - 1) dst[dp++] = src[k++];
        dst[dp] = 0;
        *out_color = gui_rgb(40, 100, 40);
        return dp;
    }

    /* blockquote: > */
    if (src[sp] == '>') {
        quote = 1;
        k = sp + 1;
        if (src[k] == ' ') k++;
        if (dp < dst_sz - 1) dst[dp++] = '|';
        if (dp < dst_sz - 1) dst[dp++] = ' ';
        while (src[k] && dp < dst_sz - 1) dst[dp++] = src[k++];
        dst[dp] = 0;
        *out_color = gui_rgb(100, 100, 140);
        return dp;
    }

    /* code block fence ``` */
    if (src[sp] == '`' && src[sp+1] == '`' && src[sp+2] == '`') {
        for (i = 0; i < 40 && dp < dst_sz - 1; i++) dst[dp++] = '=';
        dst[dp] = 0;
        *out_color = gui_rgb(80, 80, 80);
        return dp;
    }

    /* default: strip simple emphasis * and _ */
    k = 0;
    while (src[k] && dp < dst_sz - 1) {
        char c = src[k];
        if ((c == '*' || c == '_') &&
            (k == 0 || src[k-1] != '\\')) {
            k++;
            continue;
        }
        if (c == '`') { k++; continue; }
        dst[dp++] = c;
        k++;
    }
    dst[dp] = 0;
    *out_color = gui_rgb(40, 40, 40);
    (void)bullet; (void)quote; (void)code;
    return dp;
}

/* render view mode ---------------------------------------------- */
static void gui_file_preview_render_view(void) {
    char header[GUI_FP_MAX_PATH + 16];
    char status[64];
    char full[GUI_FP_MAX_PATH];
    char buf[GUI_FP_VIEW_BUF_SIZE + 1];
    char line[GUI_FP_VIEW_LINE_CHARS + 1];
    int fd, total, i, pos, lines, n;
    int line_pos;
    int x, y, content_w;
    int line_index;
    int total_lines;
    int spos;
    int margin;
    int gap;
    int label_h;
    int row_h;
    int button_h;
    int header_y;
    int nav_y;
    int body_y;
    int visible_lines;

    if (!fp_window) return;

    margin = fp_scale_i(8);
    if (margin < 8) margin = 8;
    gap = fp_panel_gap();
    label_h = fp_line_h() + fp_scale_i(4);
    row_h = fp_text_row_h();
    button_h = fp_button_h();
    visible_lines = fp_view_visible_lines();
    content_w = fp_window->rect.w - margin * 2;
    if (content_w < 120) content_w = 120;
    header_y = GUI_TITLE_HEIGHT + gap;
    nav_y = header_y + label_h + gap;
    body_y = nav_y + button_h + gap;

    /* header */
    pos = 0;
    pos = fp_str_append(header, pos, sizeof(header), i18n_t(I18N_KEY_HEADER_FILE));
    pos = fp_str_append(header, pos, sizeof(header), fp_view_name);
    (void)pos;
    gui_add_label(fp_window, margin, header_y, content_w, label_h, header);

    x = margin;
    gui_add_button(fp_window, x, nav_y, fp_scale_i(72), button_h, i18n_t(I18N_KEY_BTN_BACK), fp_on_back, 0);
    x += fp_scale_i(72) + gap;
    gui_add_button(fp_window, x, nav_y, fp_scale_i(40), button_h, "^", fp_on_view_up, 0);
    x += fp_scale_i(40) + gap;
    gui_add_button(fp_window, x, nav_y, fp_scale_i(40), button_h, "v", fp_on_view_down, 0);
    gui_add_button(fp_window, fp_window->rect.w - margin - fp_scale_i(76), nav_y,
                   fp_scale_i(76), button_h, i18n_t(I18N_KEY_BTN_EDIT), fp_on_edit_enter, 0);

    /* load file content */
    fp_path_join(fp_path, fp_view_name, full, sizeof(full));
    fd = vfs_open(full, O_RDONLY, 0);
    if (fd < 0) {
        gui_add_label(fp_window, margin, body_y, content_w, label_h,
                      "(cannot open file)");
        return;
    }

    total = 0;
    while (total < GUI_FP_VIEW_BUF_SIZE) {
        n = vfs_read(fd, buf + total, GUI_FP_VIEW_BUF_SIZE - total);
        if (n <= 0) break;
        total += n;
    }
    vfs_close(fd);
    buf[total] = 0;

    /* first pass: count total wrapped lines */
    total_lines = 0;
    {
        int cnt_pos = 0;
        int j;
        for (j = 0; j <= total; j++) {
            char c = (j < total) ? buf[j] : '\n';
            if (c == '\n') { total_lines++; cnt_pos = 0; }
            else {
                cnt_pos++;
                if (cnt_pos >= GUI_FP_VIEW_LINE_CHARS) { total_lines++; cnt_pos = 0; }
            }
        }
    }
    fp_view_total_lines = total_lines;
    if (fp_view_line_offset >= total_lines) {
        fp_view_line_offset = total_lines > visible_lines
                              ? total_lines - visible_lines : 0;
    }
    if (fp_view_line_offset < 0) fp_view_line_offset = 0;

    /* status label: "Line a-b / total" */
    {
        char nbuf[16];
        int last = fp_view_line_offset + visible_lines;
        if (last > total_lines) last = total_lines;
        spos = 0;
        spos = fp_str_append(status, spos, sizeof(status), i18n_t(I18N_KEY_LINE));
        fp_itoa(total_lines > 0 ? fp_view_line_offset + 1 : 0, nbuf);
        spos = fp_str_append(status, spos, sizeof(status), nbuf);
        spos = fp_str_append(status, spos, sizeof(status), i18n_t(I18N_KEY_LINE_DASH));
        fp_itoa(last, nbuf);
        spos = fp_str_append(status, spos, sizeof(status), nbuf);
        spos = fp_str_append(status, spos, sizeof(status), i18n_t(I18N_KEY_LINE_OF));
        fp_itoa(total_lines, nbuf);
        spos = fp_str_append(status, spos, sizeof(status), nbuf);
        (void)spos;
        gui_add_label(fp_window, margin + fp_scale_i(172), nav_y + (button_h - label_h) / 2,
                      fp_scale_i(190), label_h, status);
    }

    /* second pass: walk and emit visible lines */
    y = body_y;
    lines = 0;
    line_index = 0;
    i = 0;
    line_pos = 0;
    while (i <= total && lines < visible_lines) {
        char c = (i < total) ? buf[i] : '\n';
        int flush = 0;

        if (c == '\n') {
            flush = 1;
        } else if (line_pos >= GUI_FP_VIEW_LINE_CHARS) {
            flush = 1;
        }

        if (flush) {
            line[line_pos] = 0;
            if (line_index >= fp_view_line_offset) {
                if (fp_is_markdown(fp_view_name)) {
                    char md_line[GUI_FP_VIEW_LINE_CHARS + 16];
                    uint32_t color = gui_rgb(40, 40, 40);
                    gui_widget_t *mlbl;
                    fp_md_format_line(line, md_line, (int)sizeof(md_line), &color);
                    mlbl = gui_add_label(fp_window, margin, y, content_w, row_h, md_line);
                    if (mlbl) mlbl->fg_color = color;
                } else {
                    gui_add_label(fp_window, margin, y, content_w, row_h, line);
                }
                y += row_h;
                lines++;
            }
            line_index++;
            line_pos = 0;
            if (c == '\n') {
                i++;
                continue;
            }
            /* hard wrap: keep current char */
        }

        if (i < total) {
            unsigned char uc = (unsigned char)c;
            if (uc < 32 || uc > 126) c = '.';
            line[line_pos++] = c;
        }
        i++;
    }
}

/* clear fp_window pointer when window is closed by user (X button) */
static void fp_on_window_close(gui_window_t *win, void *ud) {
    (void)win;
    (void)ud;
    fp_window = 0;
}

/* render edit mode ---------------------------------------------- */
static void gui_file_preview_render_edit(void) {
    int margin, gap, label_h, button_h;
    int header_y, nav_y, body_y;
    int content_w;
    char full[GUI_FP_MAX_PATH];
    char header[128];
    char buf[GUI_WIDGET_TEXT_CAP];
    int fd, n, total;
    int pos;

    if (!fp_window) return;

    margin = fp_scale_i(8);
    if (margin < 8) margin = 8;
    gap = fp_panel_gap();
    label_h = fp_line_h() + fp_scale_i(4);
    button_h = fp_button_h();
    content_w = fp_window->rect.w - margin * 2;
    if (content_w < 120) content_w = 120;
    header_y = GUI_TITLE_HEIGHT + gap;
    nav_y = header_y + label_h + gap;
    body_y = nav_y + button_h + gap;

    fp_edit_textarea = 0;
    fp_edit_status = 0;

    pos = 0;
    pos = fp_str_append(header, pos, sizeof(header), i18n_t(I18N_KEY_HEADER_EDIT));
    pos = fp_str_append(header, pos, sizeof(header), fp_view_name);
    (void)pos;
    gui_add_label(fp_window, margin, header_y, content_w, label_h, header);

    gui_add_button(fp_window, margin, nav_y, fp_scale_i(72), button_h,
                   i18n_t(I18N_KEY_BTN_SAVE), fp_on_edit_save, 0);
    gui_add_button(fp_window, margin + fp_scale_i(72) + gap, nav_y,
                   fp_scale_i(82), button_h, i18n_t(I18N_KEY_BTN_CANCEL), fp_on_edit_cancel, 0);
    fp_edit_status = gui_add_label(fp_window, margin + fp_scale_i(172),
                                   nav_y + (button_h - label_h) / 2,
                                   fp_window->rect.w - margin * 2 - fp_scale_i(172), label_h, "");

    fp_path_join(fp_path, fp_view_name, full, sizeof(full));
    fd = vfs_open(full, O_RDONLY, 0);
    total = 0;
    if (fd >= 0) {
        while (total < (int)sizeof(buf) - 1) {
            n = vfs_read(fd, buf + total, (int)sizeof(buf) - 1 - total);
            if (n <= 0) break;
            total += n;
        }
        vfs_close(fd);
    }
    buf[total] = 0;

    fp_edit_textarea = gui_add_textarea(fp_window, margin, body_y,
                                        content_w,
                                        fp_window->rect.h - body_y - margin,
                                        buf);
    if (fp_edit_textarea) {
        gui_widget_set_textbox_flags(fp_edit_textarea,
            GUI_TEXTBOX_FLAG_MULTILINE | GUI_TEXTBOX_FLAG_WRAP);
        gui_widget_set_placeholder(fp_edit_textarea, i18n_t(I18N_KEY_WIN_FILE_EDITOR));
    }
}

/* rebuild and open ---------------------------------------------- */
static void gui_file_preview_rebuild(void) {
    const char *title;
    int win_x = 60;
    int win_y = 60;
    int win_w = fp_default_window_w();
    int win_h = 430;
    int had_window = 0;

    switch (fp_mode) {
        case 0:  title = i18n_t(I18N_KEY_WIN_FILES); break;
        case 1:  title = i18n_t(I18N_KEY_WIN_FILE_VIEWER); break;
        case 2:  title = i18n_t(I18N_KEY_WIN_FILE_EDITOR); break;
        default: title = i18n_t(I18N_KEY_WIN_FILES); break;
    }

    if (fp_mode == 1) win_h = fp_view_window_h();
    else if (fp_mode == 2) win_h = fp_edit_window_h();

    if (fp_window) {
        had_window = 1;
        win_x = fp_window->rect.x;
        win_y = fp_window->rect.y;
        win_w = fp_window->rect.w;
        win_h = fp_window->rect.h;

        /* avoid firing close hook (it would null fp_window prematurely) */
        gui_window_set_on_close(fp_window, 0, 0);
        gui_destroy_window(fp_window);
        fp_window = 0;
    }

    if (had_window) {
        int min_w = fp_default_window_w();
        if (win_w < min_w) win_w = min_w;
        if (win_h < 260) win_h = 260;
    }

    fp_window = gui_create_window(win_x, win_y, win_w, win_h, title);
    if (!fp_window) return;
    gui_window_set_on_close(fp_window, fp_on_window_close, 0);
    if (fp_mode == 0) {
        gui_file_preview_render_list();
    } else if (fp_mode == 1) {
        gui_file_preview_render_view();
    } else {
        gui_file_preview_render_edit();
    }
    gui_render();
}

void gui_file_preview_open_file(const char *path) {
    char parent[GUI_FP_MAX_PATH];
    char name[GUI_FP_MAX_NAME];
    uint32_t i;
    int slash = -1;
    if (!path || !path[0]) return;
    for (i = 0; path[i] && i < GUI_FP_MAX_PATH - 1; i++) {
        if (path[i] == '/') slash = (int)i;
    }
    if (slash <= 0) {
        parent[0] = '/';
        parent[1] = 0;
        gui_taskbar_search_copy(name, sizeof(name), path[0] == '/' ? path + 1 : path);
    } else {
        uint32_t n = 0;
        while (n < (uint32_t)slash && n + 1 < sizeof(parent)) {
            parent[n] = path[n];
            n++;
        }
        parent[n] = 0;
        gui_taskbar_search_copy(name, sizeof(name), path + slash + 1);
    }

    gui_taskbar_search_copy(fp_path, sizeof(fp_path), parent);
    gui_taskbar_search_copy(fp_view_name, sizeof(fp_view_name), name);
    fp_mode = 1;
    fp_selected = -1;
    fp_page = 0;
    fp_view_line_offset = 0;
    fp_view_total_lines = 0;
    fp_last_click_global = -1;
    fp_last_click_frame = 0;
    fp_clear_entry_click_state();
    gui_file_preview_rebuild();
}

void gui_file_preview_open_path(const char *path) {
    char parent[GUI_FP_MAX_PATH];
    char name[GUI_FP_MAX_NAME];
    dentry_t *entry = 0;
    uint32_t i;
    uint32_t entry_index = 0;
    int slash = -1;
    int is_dir = 0;
    if (!path || !path[0]) {
        gui_file_preview_open();
        return;
    }
    if (gui_string_equals(path, "/")) {
        fp_path_set_root();
        fp_selected = -1;
        fp_mode = 0;
        fp_page = 0;
        fp_clear_entry_click_state();
        gui_file_preview_rebuild();
        return;
    }
    for (i = 0; path[i] && i < GUI_FP_MAX_PATH - 1; i++) {
        if (path[i] == '/') slash = (int)i;
    }
    if (slash <= 0) {
        parent[0] = '/';
        parent[1] = 0;
        gui_taskbar_search_copy(name, sizeof(name), path[0] == '/' ? path + 1 : path);
    } else {
        uint32_t n = 0;
        while (n < (uint32_t)slash && n + 1 < sizeof(parent)) {
            parent[n] = path[n];
            n++;
        }
        parent[n] = 0;
        gui_taskbar_search_copy(name, sizeof(name), path + slash + 1);
    }
    for (i = 0; i < 128; i++) {
        dentry_t *e = vfs_readdir(parent, i);
        if (!e) break;
        if (gui_string_equals(e->name, name)) {
            entry = e;
            entry_index = i;
            break;
        }
    }
    if (entry && fp_entry_is_dir(entry)) is_dir = 1;
    if (is_dir) {
        gui_taskbar_search_copy(fp_path, sizeof(fp_path), path);
        fp_selected = -1;
    } else {
        int global_index;
        gui_taskbar_search_copy(fp_path, sizeof(fp_path), parent);
        global_index = (fp_is_root() ? (int)entry_index : (int)entry_index + 1);
        fp_page = global_index / fp_list_per_page();
        fp_selected = global_index - fp_page * fp_list_per_page();
    }
    fp_mode = 0;
    fp_clear_entry_click_state();
    if (!is_dir && entry) {
        int global_index = (fp_is_root() ? (int)entry_index : (int)entry_index + 1);
        fp_page = global_index / fp_list_per_page();
        fp_selected = global_index - fp_page * fp_list_per_page();
    } else {
        fp_page = 0;
    }
    gui_file_preview_rebuild();
}

void gui_file_preview_open(void) {
    if (fp_path[0] == 0) fp_path_set_root();
    fp_mode = 0;
    fp_page = 0;
    fp_clear_entry_click_state();
    gui_file_preview_rebuild();
}
