/* ============================================================================
 * osk.c — On-Screen Keyboard 实现（M8-D.1）
 *
 * 布局：底部占屏 40% 高度全宽面板，5 行 x 10 逻辑列。
 * 层次：LOWER / UPPER / SYMBOL，通过 Shift / ?123 键切换。
 * 命中：osk_handle_tap(x,y) → 内部状态机 / gui_post_key(_code)。
 * 渲染：osk_render() 输出到 present buffer；present 由 GUI 主循环负责。
 * ============================================================================ */
#include "osk.h"
#include "gui.h"

/* 避免 osk_get_state() 返回结构时 uint8_t 未定义 */
#include <stdint.h>

/* ============================ 布局定义 ============================ */

#define OSK_ROWS 5
#define OSK_COLS 10

/* 每行必须使 col_span 之和 == 10 */

static const osk_layout_t g_layout_lower = {
    .row_count = OSK_ROWS, .col_count = OSK_COLS,
    .rows = {
        {10,{{'1',"1",1},{'2',"2",1},{'3',"3",1},{'4',"4",1},{'5',"5",1},
             {'6',"6",1},{'7',"7",1},{'8',"8",1},{'9',"9",1},{'0',"0",1}}},
        {10,{{'q',"q",1},{'w',"w",1},{'e',"e",1},{'r',"r",1},{'t',"t",1},
             {'y',"y",1},{'u',"u",1},{'i',"i",1},{'o',"o",1},{'p',"p",1}}},
        {10,{{'a',"a",1},{'s',"s",1},{'d',"d",1},{'f',"f",1},{'g',"g",1},
             {'h',"h",1},{'j',"j",1},{'k',"k",1},{'l',"l",1},{OSK_KEY_BACKSPACE,"<-",1}}},
        {8, {{OSK_KEY_SHIFT,"Shf",2},{'z',"z",1},{'x',"x",1},{'c',"c",1},
             {'v',"v",1},{'b',"b",1},{'n',"n",1},{'m',"m",1}}},
        {5, {{OSK_KEY_SYMBOL,"?123",2},{',',",",1},{OSK_KEY_SPACE,"space",4},
             {'.',".",1},{OSK_KEY_ENTER,"Enter",2}}}
    }
};

static const osk_layout_t g_layout_upper = {
    .row_count = OSK_ROWS, .col_count = OSK_COLS,
    .rows = {
        {10,{{'1',"1",1},{'2',"2",1},{'3',"3",1},{'4',"4",1},{'5',"5",1},
             {'6',"6",1},{'7',"7",1},{'8',"8",1},{'9',"9",1},{'0',"0",1}}},
        {10,{{'Q',"Q",1},{'W',"W",1},{'E',"E",1},{'R',"R",1},{'T',"T",1},
             {'Y',"Y",1},{'U',"U",1},{'I',"I",1},{'O',"O",1},{'P',"P",1}}},
        {10,{{'A',"A",1},{'S',"S",1},{'D',"D",1},{'F',"F",1},{'G',"G",1},
             {'H',"H",1},{'J',"J",1},{'K',"K",1},{'L',"L",1},{OSK_KEY_BACKSPACE,"<-",1}}},
        {8, {{OSK_KEY_SHIFT,"SHF",2},{'Z',"Z",1},{'X',"X",1},{'C',"C",1},
             {'V',"V",1},{'B',"B",1},{'N',"N",1},{'M',"M",1}}},
        {5, {{OSK_KEY_SYMBOL,"?123",2},{',',",",1},{OSK_KEY_SPACE,"space",4},
             {'.',".",1},{OSK_KEY_ENTER,"Enter",2}}}
    }
};

static const osk_layout_t g_layout_symbol = {
    .row_count = OSK_ROWS, .col_count = OSK_COLS,
    .rows = {
        {10,{{'`',"`",1},{'~',"~",1},{'-',"-",1},{'_',"_",1},{'=',"=",1},
             {'+',"+",1},{'[',"[",1},{']',"]",1},{'{',"{",1},{'}',"}",1}}},
        {10,{{';',";",1},{':',":",1},{'\'',"'",1},{'"',"\"",1},{',',",",1},
             {'.',".",1},{'/',"/",1},{'\\',"\\",1},{'?',"?",1},{'|',"|",1}}},
        {10,{{'!',"!",1},{'@',"@",1},{'#',"#",1},{'$',"$",1},{'%',"%",1},
             {'^',"^",1},{'&',"&",1},{'*',"*",1},{'(',"(",1},{')',")",1}}},
        {8, {{'<',"<",1},{'>',">",1},{'/',"/",1},{'\\',"\\",1},
             {'?',"?",1},{'|',"|",1},{'~',"~",1},{OSK_KEY_BACKSPACE,"<-",1}}},
        {5, {{OSK_KEY_SYMBOL,"abc",2},{',',",",1},{OSK_KEY_SPACE,"space",4},
             {'.',".",1},{OSK_KEY_ENTER,"Enter",2}}}
    }
};

static const osk_layout_t *osk_layout_ptr_(osk_layer_t l) {
    switch (l) {
        case OSK_LAYER_UPPER:  return &g_layout_upper;
        case OSK_LAYER_SYMBOL: return &g_layout_symbol;
        default:               return &g_layout_lower;
    }
}

/* ============================ 运行时状态 ============================ */

static uint32_t osk_strlen_helper_(const char *s);

static osk_state_t g_osk;

const osk_state_t *osk_get_state(void) { return &g_osk; }
const osk_layout_t *osk_get_layout(osk_layer_t layer) { return osk_layout_ptr_(layer); }

static void osk_recompute_geometry_(void) {
    /* 面板占底部 40% 高度、全宽 */
    g_osk.panel_w = g_osk.screen_w;
    g_osk.panel_h = (g_osk.screen_h * 40) / 100;
    if (g_osk.panel_h < 120) g_osk.panel_h = 120;
    if (g_osk.panel_h > g_osk.screen_h) g_osk.panel_h = g_osk.screen_h;
    g_osk.panel_x = 0;
    g_osk.panel_y = g_osk.screen_h - g_osk.panel_h;
    g_osk.key_w   = g_osk.panel_w / OSK_COLS;
    g_osk.key_h   = g_osk.panel_h / OSK_ROWS;
}

void osk_init(int screen_w, int screen_h) {
    for (uint32_t i = 0; i < sizeof(g_osk); i++) ((uint8_t *)&g_osk)[i] = 0;
    g_osk.screen_w = (screen_w > 0) ? screen_w : 640;
    g_osk.screen_h = (screen_h > 0) ? screen_h : 480;
    g_osk.layer    = OSK_LAYER_LOWER;
    g_osk.visible  = 0;
    osk_recompute_geometry_();
}

void osk_show(void)   { g_osk.visible = 1; }
void osk_hide(void)   { g_osk.visible = 0; }
void osk_toggle(void) { g_osk.visible = g_osk.visible ? 0 : 1; }
int  osk_is_visible(void) { return g_osk.visible; }

/* ============================ 命中测试 ============================ */

/* 返回指针到指定 (row,col) 处的 osk_key_t，未命中返回 NULL。
 * col_span > 1 的键会横跨多个逻辑列。 */
static const osk_key_t *osk_hit_key_(int row, int logical_col) {
    const osk_layout_t *lay = osk_layout_ptr_(g_osk.layer);
    if (row < 0 || row >= (int)lay->row_count) return 0;
    if (logical_col < 0 || logical_col >= (int)lay->col_count) return 0;
    const osk_row_t *r = &lay->rows[row];
    int c0 = 0;
    for (uint8_t i = 0; i < r->key_count; i++) {
        int span = r->keys[i].col_span ? r->keys[i].col_span : 1;
        if (logical_col >= c0 && logical_col < c0 + span) return &r->keys[i];
        c0 += span;
    }
    return 0;
}

static void osk_dispatch_key_(const osk_key_t *k) {
    if (!k) return;
    g_osk.stat_keys_pressed++;
    g_osk.last_key_code = k->code;
    g_osk.last_text_len = 0;
    g_osk.last_text[0]  = 0;

    if (k->code > 0) {
        char ch = (char)k->code;
        g_osk.last_text[0] = ch;
        g_osk.last_text[1] = 0;
        g_osk.last_text_len = 1;
        g_osk.stat_chars_emitted++;
        gui_post_key(ch);
        /* Shift 非锁定：大写层按字母后回落小写 */
        if (g_osk.layer == OSK_LAYER_UPPER && ((ch >= 'A' && ch <= 'Z'))) {
            g_osk.layer = OSK_LAYER_LOWER;
        }
        return;
    }
    switch (k->code) {
        case OSK_KEY_SHIFT:
            g_osk.layer = (g_osk.layer == OSK_LAYER_UPPER) ? OSK_LAYER_LOWER : OSK_LAYER_UPPER;
            break;
        case OSK_KEY_SYMBOL:
            g_osk.layer = (g_osk.layer == OSK_LAYER_SYMBOL) ? OSK_LAYER_LOWER : OSK_LAYER_SYMBOL;
            break;
        case OSK_KEY_BACKSPACE:
            g_osk.stat_backspaces++;
            gui_post_key_code(GUI_KEY_BACKSPACE);
            break;
        case OSK_KEY_ENTER:
            g_osk.stat_enters++;
            gui_post_key_code(GUI_KEY_ENTER);
            break;
        case OSK_KEY_SPACE:
            g_osk.last_text[0] = ' ';
            g_osk.last_text[1] = 0;
            g_osk.last_text_len = 1;
            g_osk.stat_chars_emitted++;
            gui_post_key(' ');
            break;
        case OSK_KEY_HIDE:
            osk_hide();
            break;
        default: break;
    }
}

int osk_handle_tap(int x, int y) {
    if (!g_osk.visible) return 0;
    if (x < g_osk.panel_x || x >= g_osk.panel_x + g_osk.panel_w) return 0;
    if (y < g_osk.panel_y || y >= g_osk.panel_y + g_osk.panel_h) return 0;
    int col = (x - g_osk.panel_x) / (g_osk.key_w > 0 ? g_osk.key_w : 1);
    int row = (y - g_osk.panel_y) / (g_osk.key_h > 0 ? g_osk.key_h : 1);
    if (col >= OSK_COLS) col = OSK_COLS - 1;
    if (row >= OSK_ROWS) row = OSK_ROWS - 1;
    const osk_key_t *k = osk_hit_key_(row, col);
    osk_dispatch_key_(k);
    return 1;
}

/* ============================ 渲染 ============================ */

#define OSK_COLOR_PANEL_BG    0xFF202028u
#define OSK_COLOR_KEY_BG      0xFF3A3A48u
#define OSK_COLOR_KEY_SPECIAL 0xFF4A4A60u
#define OSK_COLOR_KEY_BORDER  0xFF606078u
#define OSK_COLOR_KEY_TEXT    0xFFFFFFFFu

void osk_render(void) {
    if (!g_osk.visible) return;
    gui_screen_fill_rect(g_osk.panel_x, g_osk.panel_y, g_osk.panel_w, g_osk.panel_h,
                         OSK_COLOR_PANEL_BG);
    const osk_layout_t *lay = osk_layout_ptr_(g_osk.layer);
    int gap = 2;
    for (uint8_t ri = 0; ri < lay->row_count; ri++) {
        const osk_row_t *r = &lay->rows[ri];
        int c0 = 0;
        int ky = g_osk.panel_y + ri * g_osk.key_h;
        for (uint8_t ki = 0; ki < r->key_count; ki++) {
            const osk_key_t *k = &r->keys[ki];
            int span = k->col_span ? k->col_span : 1;
            int kx = g_osk.panel_x + c0 * g_osk.key_w;
            int kw = g_osk.key_w * span;
            int kh = g_osk.key_h;
            uint32_t bg = (k->code <= 0) ? OSK_COLOR_KEY_SPECIAL : OSK_COLOR_KEY_BG;
            gui_screen_fill_rect(kx + gap, ky + gap, kw - 2*gap, kh - 2*gap, bg);
            gui_screen_draw_border(kx + gap, ky + gap, kw - 2*gap, kh - 2*gap, 1,
                                   OSK_COLOR_KEY_BORDER);
            uint32_t label_len = osk_strlen_helper_(k->label);
            int text_w = (int)label_len * 8;
            int tx = kx + (kw - text_w) / 2;
            int ty = ky + (kh - 12) / 2;
            gui_draw_text(tx, ty, k->label, OSK_COLOR_KEY_TEXT);
            c0 += span;
        }
    }
}

static uint32_t osk_strlen_helper_(const char *s) {
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

/* selftest 实现位于 src/arch/x86_64/kernel/osk_selftest64.c，这里提供内部辅助 */

/* 获取 (row, key_index_in_row) 对应的屏幕中心坐标——selftest 内部使用 */
void osk_center_of_(osk_layer_t layer, int row, int key_index, int *px, int *py) {
    const osk_layout_t *lay = osk_layout_ptr_(layer);
    const osk_row_t *r = &lay->rows[row];
    int c0 = 0;
    for (int i = 0; i < key_index; i++) c0 += r->keys[i].col_span ? r->keys[i].col_span : 1;
    int span = r->keys[key_index].col_span ? r->keys[key_index].col_span : 1;
    int kx = g_osk.panel_x + c0 * g_osk.key_w + (span * g_osk.key_w) / 2;
    int ky = g_osk.panel_y + row * g_osk.key_h + g_osk.key_h / 2;
    *px = kx;
    *py = ky;
}
