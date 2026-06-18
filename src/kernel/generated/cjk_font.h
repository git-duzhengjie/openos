/* generated/cjk_font.h - generated CJK bitmap glyph table ABI */
#ifndef OPENOS_GENERATED_CJK_FONT_H
#define OPENOS_GENERATED_CJK_FONT_H

#include <stdint.h>
#include "font.h"

typedef struct font_cjk_glyph {
    uint32_t codepoint;
    uint16_t rows[FONT_UNICODE_HEIGHT];
} font_cjk_glyph_t;

extern const font_cjk_glyph_t g_generated_cjk_glyphs[];
extern const uint32_t g_generated_cjk_glyph_count;
extern const char g_generated_cjk_font_source[];

#endif /* OPENOS_GENERATED_CJK_FONT_H */
