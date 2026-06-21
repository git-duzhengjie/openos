#include "openos.h"

#define BLINK_VIEW_W 300
#define BLINK_VIEW_H 190

typedef struct css_box {
    const char *tag;
    const char *text;
    unsigned int bg;
    unsigned int fg;
    int margin;
    int padding;
    int height;
    int x;
    int y;
    int w;
    int h;
} css_box_t;

static void layout_block(css_box_t *box, int viewport_w, int *cursor_y)
{
    box->x = box->margin;
    box->y = *cursor_y + box->margin;
    box->w = viewport_w - box->margin * 2;
    box->h = box->height + box->padding * 2;
    *cursor_y = box->y + box->h + box->margin;
}

static void draw_box(int win, const css_box_t *box)
{
    int text_x = box->x + box->padding;
    int text_y = box->y + box->padding + 6;
    openos_gui_fill_rect(win, box->x, box->y, box->w, box->h, box->bg);
    openos_gui_draw_text(win, text_x, text_y, box->text, box->fg);
}

int main(void)
{
    int win;
    int y = 8;
    int i;
    css_box_t boxes[] = {
        {"h1", "OpenOS Blink layout smoke", 0xff1a73e8u, 0xffffffffu, 8, 10, 22, 0, 0, 0, 0},
        {"p", "HTML block flow + CSS margin/padding", 0xffffffffu, 0xff202124u, 8, 8, 18, 0, 0, 0, 0},
        {"button", "button: navigate", 0xff34a853u, 0xffffffffu, 8, 8, 18, 0, 0, 0, 0},
        {"img", "image placeholder 64x32", 0xffffc107u, 0xff202124u, 8, 8, 32, 0, 0, 0, 0}
    };
    int count = (int)(sizeof(boxes) / sizeof(boxes[0]));

    win = openos_gui_create_window("Blink Smoke", 120, 90, BLINK_VIEW_W, BLINK_VIEW_H);
    if (win < 0) {
        printf("blink_smoke: failed to create window\n");
        return 1;
    }

    openos_gui_fill_rect(win, 0, 0, BLINK_VIEW_W, BLINK_VIEW_H, 0xfff1f3f4u);
    for (i = 0; i < count; ++i) {
        layout_block(&boxes[i], BLINK_VIEW_W, &y);
        draw_box(win, &boxes[i]);
    }

    openos_gui_draw_text(win, 12, BLINK_VIEW_H - 18,
                         "DOM: 4 nodes, style: 4 rules, layout: block-flow", 0xff5f6368u);
    openos_gui_present(win);

    printf("blink_smoke: html/css layout nodes=%d viewport=%dx%d\n", count, BLINK_VIEW_W, BLINK_VIEW_H);
    return 0;
}
