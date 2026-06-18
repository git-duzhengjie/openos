/* ============================================================
 * openos - Bitmap Font Rendering Engine
 * ============================================================ */

#ifndef OPENOS_FONT_H
#define OPENOS_FONT_H

#include "types.h"

#define FONT_DEFAULT_WIDTH       8
#define FONT_DEFAULT_HEIGHT      8
#define FONT_UNICODE_WIDTH       16
#define FONT_UNICODE_HEIGHT      16
#define FONT_DEFAULT_LINE_GAP    2
#define FONT_DEFAULT_TAB_SPACES  4

typedef enum font_size {
    FONT_SIZE_SMALL = 0,
    FONT_SIZE_MEDIUM = 1,
    FONT_SIZE_LARGE = 2
} font_size_t;

typedef struct font_renderer font_renderer_t;

typedef uint8_t (*font_get_glyph_row_fn)(const font_renderer_t *renderer, char ch, int row);
typedef void (*font_put_pixel_fn)(void *ctx, int x, int y, uint32_t color);

typedef struct font_renderer_ops {
    font_get_glyph_row_fn get_glyph_row;
} font_renderer_ops_t;

struct font_renderer {
    const font_renderer_ops_t *ops;
    uint8_t width;
    uint8_t height;
    const char *name;
};

typedef struct font_rect {
    int x;
    int y;
    int w;
    int h;
} font_rect_t;

typedef struct font_text_metrics {
    uint32_t width;
    uint32_t height;
    uint32_t lines;
} font_text_metrics_t;

const font_renderer_t *font_get_default(void);
void font_set_size(font_size_t size);
font_size_t font_get_size(void);
uint32_t font_get_scale_percent(void);
uint32_t font_scale_value(uint32_t value);
uint32_t font_get_ascii_width(const font_renderer_t *renderer);
uint32_t font_get_ascii_height(const font_renderer_t *renderer);
uint32_t font_get_unicode_width(void);
uint32_t font_get_unicode_height(void);
uint8_t font_get_glyph_row(const font_renderer_t *renderer, char ch, int row);
uint32_t font_get_line_height(const font_renderer_t *renderer);
int font_decode_utf8(const char **text, uint32_t *codepoint);
uint32_t font_measure_codepoint_width(const font_renderer_t *renderer, uint32_t codepoint);
uint32_t font_measure_text_width(const font_renderer_t *renderer, const char *text);
uint32_t font_measure_text_height(const font_renderer_t *renderer, const char *text);
void font_measure_text(const font_renderer_t *renderer, const char *text, font_text_metrics_t *out_metrics);
void font_draw_char(const font_renderer_t *renderer, font_put_pixel_fn put_pixel, void *ctx,
                    int x, int y, char ch, uint32_t color);
void font_draw_char_clipped(const font_renderer_t *renderer, font_put_pixel_fn put_pixel, void *ctx,
                            const font_rect_t *clip, int x, int y, char ch, uint32_t color);
void font_draw_codepoint_clipped(const font_renderer_t *renderer, font_put_pixel_fn put_pixel, void *ctx,
                                 const font_rect_t *clip, int x, int y, uint32_t codepoint, uint32_t color);
void font_draw_text(const font_renderer_t *renderer, font_put_pixel_fn put_pixel, void *ctx,
                    int x, int y, const char *text, uint32_t color);
void font_draw_text_clipped(const font_renderer_t *renderer, font_put_pixel_fn put_pixel, void *ctx,
                            const font_rect_t *clip, int x, int y, const char *text, uint32_t color);

#endif /* OPENOS_FONT_H */
