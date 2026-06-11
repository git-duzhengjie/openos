/* ============================================================
 * openos - Minimal Bitmap Font Renderer
 * ============================================================ */

#ifndef OPENOS_FONT_H
#define OPENOS_FONT_H

#include "types.h"

#define FONT_DEFAULT_WIDTH   8
#define FONT_DEFAULT_HEIGHT  8

typedef struct font_renderer font_renderer_t;

typedef uint8_t (*font_get_glyph_row_fn)(const font_renderer_t *renderer, char ch, int row);

typedef struct font_renderer_ops {
    font_get_glyph_row_fn get_glyph_row;
} font_renderer_ops_t;

struct font_renderer {
    const font_renderer_ops_t *ops;
    uint8_t width;
    uint8_t height;
    const char *name;
};

const font_renderer_t *font_get_default(void);
uint8_t font_get_glyph_row(const font_renderer_t *renderer, char ch, int row);
uint32_t font_measure_text_width(const font_renderer_t *renderer, const char *text);
uint32_t font_measure_text_height(const font_renderer_t *renderer, const char *text);

#endif /* OPENOS_FONT_H */
