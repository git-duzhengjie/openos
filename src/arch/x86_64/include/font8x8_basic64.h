#ifndef OPENOS_ARCH_X86_64_FONT8X8_BASIC64_H
#define OPENOS_ARCH_X86_64_FONT8X8_BASIC64_H

#include <stdint.h>

/*
 * dhepper/font8x8_basic — Public Domain 8x8 bitmap font.
 * Covers ASCII 0x20..0x7F (96 chars). Row-major, LSB = leftmost pixel.
 */
#define OPENOS_FONT8X8_FIRST_GLYPH 0x20u
#define OPENOS_FONT8X8_LAST_GLYPH  0x7Fu
#define OPENOS_FONT8X8_GLYPH_COUNT (OPENOS_FONT8X8_LAST_GLYPH - OPENOS_FONT8X8_FIRST_GLYPH + 1u)
#define OPENOS_FONT8X8_WIDTH       8u
#define OPENOS_FONT8X8_HEIGHT      8u

extern const uint8_t openos_font8x8_basic[OPENOS_FONT8X8_GLYPH_COUNT][OPENOS_FONT8X8_HEIGHT];

#endif /* OPENOS_ARCH_X86_64_FONT8X8_BASIC64_H */
