/* ============================================================
 * openos - Minimal Bitmap Font Renderer
 * ============================================================ */

#include "font.h"

#define FONT_ROW8(r0, r1, r2, r3, r4, r5, r6, r7) \
    do { \
        if (row == 0) return (uint8_t)(r0); \
        if (row == 1) return (uint8_t)(r1); \
        if (row == 2) return (uint8_t)(r2); \
        if (row == 3) return (uint8_t)(r3); \
        if (row == 4) return (uint8_t)(r4); \
        if (row == 5) return (uint8_t)(r5); \
        if (row == 6) return (uint8_t)(r6); \
        if (row == 7) return (uint8_t)(r7); \
        return 0x00; \
    } while (0)

#define FONT_GLYPH(ch, r0, r1, r2, r3, r4, r5, r6, r7) \
    do { \
        if (c == (uint8_t)(ch)) { \
            FONT_ROW8(r0, r1, r2, r3, r4, r5, r6, r7); \
        } \
    } while (0)

#include "generated/cjk_font.h"

static int font_codepoint_is_cjk(uint32_t codepoint) {
    return (codepoint >= 0x3400u && codepoint <= 0x4DBFu) ||
           (codepoint >= 0x4E00u && codepoint <= 0x9FFFu) ||
           (codepoint >= 0xF900u && codepoint <= 0xFAFFu) ||
           (codepoint >= 0x20000u && codepoint <= 0x2FA1Fu);
}

static const uint16_t *font_find_cjk_rows(uint32_t codepoint) {
    uint32_t left = 0;
    uint32_t right = g_generated_cjk_glyph_count;
    while (left < right) {
        uint32_t mid = left + ((right - left) / 2u);
        uint32_t mid_codepoint = g_generated_cjk_glyphs[mid].codepoint;
        if (mid_codepoint == codepoint) return g_generated_cjk_glyphs[mid].rows;
        if (mid_codepoint < codepoint) {
            left = mid + 1u;
        } else {
            right = mid;
        }
    }
    return 0;
}

static uint16_t font_missing_cjk_row(uint32_t codepoint, int row) {
    uint16_t bits;
    if (row < 0 || row >= FONT_UNICODE_HEIGHT) return 0;
    if (row == 0 || row == FONT_UNICODE_HEIGHT - 1) return 0x7ffeu;
    if (row == 1 || row == FONT_UNICODE_HEIGHT - 2) return 0x4002u;
    bits = 0x4002u;
    if ((row & 1) == 0) bits |= 0x3ffcu;
    if (((codepoint >> (row & 7)) & 1u) != 0) bits |= 0x0810u;
    if (((codepoint >> ((row + 3) & 7)) & 1u) != 0) bits |= 0x0420u;
    if (((codepoint >> ((row + 6) & 7)) & 1u) != 0) bits |= 0x0240u;
    return bits;
}

static uint16_t font_unicode_fallback_row(uint32_t codepoint, int row) {
    uint16_t bits = 0;
    int col;
    if (row < 0 || row >= FONT_UNICODE_HEIGHT) return 0;
    for (col = 0; col < FONT_UNICODE_WIDTH; col++) {
        uint32_t bit = ((uint32_t)(row * 13 + col * 7) ^ codepoint ^ (codepoint >> 8)) & 7u;
        int border = (row == 0 || row == FONT_UNICODE_HEIGHT - 1 || col == 0 || col == FONT_UNICODE_WIDTH - 1);
        int diagonal = (col == row || col == FONT_UNICODE_WIDTH - 1 - row);
        if (border || diagonal || bit == 0) bits |= (uint16_t)(0x8000u >> col);
    }
    return bits;
}

__attribute__((optimize("no-jump-tables")))
static uint8_t font8x8_get_glyph_row_direct(char ch, int row) {
    uint8_t c = (uint8_t)ch;

    if (row < 0 || row >= FONT_DEFAULT_HEIGHT) return 0x00;

    FONT_GLYPH('0', 0x3c,0x66,0x6e,0x76,0x66,0x66,0x3c,0x00);
    FONT_GLYPH('1', 0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0x00);
    FONT_GLYPH('2', 0x3c,0x66,0x06,0x1c,0x30,0x66,0x7e,0x00);
    FONT_GLYPH('3', 0x3c,0x66,0x06,0x1c,0x06,0x66,0x3c,0x00);
    FONT_GLYPH('4', 0x0c,0x1c,0x3c,0x6c,0x7e,0x0c,0x0c,0x00);
    FONT_GLYPH('5', 0x7e,0x60,0x7c,0x06,0x06,0x66,0x3c,0x00);
    FONT_GLYPH('6', 0x1c,0x30,0x60,0x7c,0x66,0x66,0x3c,0x00);
    FONT_GLYPH('7', 0x7e,0x66,0x06,0x0c,0x18,0x18,0x18,0x00);
    FONT_GLYPH('8', 0x3c,0x66,0x66,0x3c,0x66,0x66,0x3c,0x00);
    FONT_GLYPH('9', 0x3c,0x66,0x66,0x3e,0x06,0x0c,0x38,0x00);

    FONT_GLYPH('A', 0x18,0x3c,0x66,0x66,0x7e,0x66,0x66,0x00);
    FONT_GLYPH('B', 0x7c,0x66,0x66,0x7c,0x66,0x66,0x7c,0x00);
    FONT_GLYPH('C', 0x3c,0x66,0x60,0x60,0x60,0x66,0x3c,0x00);
    FONT_GLYPH('D', 0x78,0x6c,0x66,0x66,0x66,0x6c,0x78,0x00);
    FONT_GLYPH('E', 0x7e,0x60,0x60,0x7c,0x60,0x60,0x7e,0x00);
    FONT_GLYPH('F', 0x7e,0x60,0x60,0x7c,0x60,0x60,0x60,0x00);
    FONT_GLYPH('G', 0x3c,0x66,0x60,0x6e,0x66,0x66,0x3c,0x00);
    FONT_GLYPH('H', 0x66,0x66,0x66,0x7e,0x66,0x66,0x66,0x00);
    FONT_GLYPH('I', 0x7e,0x18,0x18,0x18,0x18,0x18,0x7e,0x00);
    FONT_GLYPH('J', 0x1e,0x0c,0x0c,0x0c,0x0c,0x6c,0x38,0x00);
    FONT_GLYPH('K', 0x66,0x6c,0x78,0x70,0x78,0x6c,0x66,0x00);
    FONT_GLYPH('L', 0x60,0x60,0x60,0x60,0x60,0x60,0x7e,0x00);
    FONT_GLYPH('M', 0x63,0x77,0x7f,0x6b,0x63,0x63,0x63,0x00);
    FONT_GLYPH('N', 0x66,0x76,0x7e,0x7e,0x6e,0x66,0x66,0x00);
    FONT_GLYPH('O', 0x3c,0x66,0x66,0x66,0x66,0x66,0x3c,0x00);
    FONT_GLYPH('P', 0x7c,0x66,0x66,0x7c,0x60,0x60,0x60,0x00);
    FONT_GLYPH('Q', 0x3c,0x66,0x66,0x66,0x6e,0x6c,0x36,0x00);
    FONT_GLYPH('R', 0x7c,0x66,0x66,0x7c,0x78,0x6c,0x66,0x00);
    FONT_GLYPH('S', 0x3c,0x66,0x60,0x3c,0x06,0x66,0x3c,0x00);
    FONT_GLYPH('T', 0x7e,0x5a,0x18,0x18,0x18,0x18,0x3c,0x00);
    FONT_GLYPH('U', 0x66,0x66,0x66,0x66,0x66,0x66,0x3c,0x00);
    FONT_GLYPH('V', 0x66,0x66,0x66,0x66,0x66,0x3c,0x18,0x00);
    FONT_GLYPH('W', 0x63,0x63,0x63,0x6b,0x7f,0x77,0x63,0x00);
    FONT_GLYPH('X', 0x66,0x66,0x3c,0x18,0x3c,0x66,0x66,0x00);
    FONT_GLYPH('Y', 0x66,0x66,0x66,0x3c,0x18,0x18,0x3c,0x00);
    FONT_GLYPH('Z', 0x7e,0x06,0x0c,0x18,0x30,0x60,0x7e,0x00);

    FONT_GLYPH('a', 0x00,0x00,0x3c,0x06,0x3e,0x66,0x3e,0x00);
    FONT_GLYPH('b', 0x60,0x60,0x7c,0x66,0x66,0x66,0x7c,0x00);
    FONT_GLYPH('c', 0x00,0x00,0x3c,0x66,0x60,0x66,0x3c,0x00);
    FONT_GLYPH('d', 0x06,0x06,0x3e,0x66,0x66,0x66,0x3e,0x00);
    FONT_GLYPH('e', 0x00,0x00,0x3c,0x66,0x7e,0x60,0x3c,0x00);
    FONT_GLYPH('f', 0x1c,0x36,0x30,0x7c,0x30,0x30,0x30,0x00);
    FONT_GLYPH('g', 0x00,0x00,0x3e,0x66,0x66,0x3e,0x06,0x7c);
    FONT_GLYPH('h', 0x60,0x60,0x7c,0x66,0x66,0x66,0x66,0x00);
    FONT_GLYPH('i', 0x18,0x00,0x38,0x18,0x18,0x18,0x3c,0x00);
    FONT_GLYPH('j', 0x0c,0x00,0x1c,0x0c,0x0c,0x6c,0x6c,0x38);
    FONT_GLYPH('k', 0x60,0x60,0x66,0x6c,0x78,0x6c,0x66,0x00);
    FONT_GLYPH('l', 0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0x00);
    FONT_GLYPH('m', 0x00,0x00,0x6c,0x7e,0x7e,0x6b,0x63,0x00);
    FONT_GLYPH('n', 0x00,0x00,0x7c,0x66,0x66,0x66,0x66,0x00);
    FONT_GLYPH('o', 0x00,0x00,0x3c,0x66,0x66,0x66,0x3c,0x00);
    FONT_GLYPH('p', 0x00,0x00,0x7c,0x66,0x66,0x7c,0x60,0x60);
    FONT_GLYPH('q', 0x00,0x00,0x3e,0x66,0x66,0x3e,0x06,0x06);
    FONT_GLYPH('r', 0x00,0x00,0x6c,0x76,0x60,0x60,0x60,0x00);
    FONT_GLYPH('s', 0x00,0x00,0x3e,0x60,0x3c,0x06,0x7c,0x00);
    FONT_GLYPH('t', 0x30,0x30,0x7c,0x30,0x30,0x36,0x1c,0x00);
    FONT_GLYPH('u', 0x00,0x00,0x66,0x66,0x66,0x66,0x3e,0x00);
    FONT_GLYPH('v', 0x00,0x00,0x66,0x66,0x66,0x3c,0x18,0x00);
    FONT_GLYPH('w', 0x00,0x00,0x63,0x6b,0x7f,0x7f,0x36,0x00);
    FONT_GLYPH('x', 0x00,0x00,0x66,0x3c,0x18,0x3c,0x66,0x00);
    FONT_GLYPH('y', 0x00,0x00,0x66,0x66,0x66,0x3e,0x06,0x7c);
    FONT_GLYPH('z', 0x00,0x00,0x7e,0x0c,0x18,0x30,0x7e,0x00);

    FONT_GLYPH('!', 0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00);
    FONT_GLYPH('"', 0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00);
    FONT_GLYPH('#', 0x36,0x36,0x7f,0x36,0x7f,0x36,0x36,0x00);
    FONT_GLYPH('$', 0x18,0x3e,0x60,0x3c,0x06,0x7c,0x18,0x00);
    FONT_GLYPH('%', 0x62,0x66,0x0c,0x18,0x30,0x66,0x46,0x00);
    FONT_GLYPH('&', 0x38,0x6c,0x38,0x76,0xdc,0xcc,0x76,0x00);
    FONT_GLYPH('\'',0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00);
    FONT_GLYPH('(', 0x0c,0x18,0x30,0x30,0x30,0x18,0x0c,0x00);
    FONT_GLYPH(')', 0x30,0x18,0x0c,0x0c,0x0c,0x18,0x30,0x00);
    FONT_GLYPH('*', 0x00,0x66,0x3c,0xff,0x3c,0x66,0x00,0x00);
    FONT_GLYPH('+', 0x00,0x18,0x18,0x7e,0x18,0x18,0x00,0x00);
    FONT_GLYPH(',', 0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30);
    FONT_GLYPH('-', 0x00,0x00,0x00,0x7e,0x00,0x00,0x00,0x00);
    FONT_GLYPH('.', 0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00);
    FONT_GLYPH('/', 0x02,0x06,0x0c,0x18,0x30,0x60,0x40,0x00);
    FONT_GLYPH(':', 0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00);
    FONT_GLYPH(';', 0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30);
    FONT_GLYPH('<', 0x0e,0x18,0x30,0x60,0x30,0x18,0x0e,0x00);
    FONT_GLYPH('=', 0x00,0x00,0x7e,0x00,0x7e,0x00,0x00,0x00);
    FONT_GLYPH('>', 0x70,0x18,0x0c,0x06,0x0c,0x18,0x70,0x00);
    FONT_GLYPH('?', 0x3c,0x66,0x06,0x0c,0x18,0x00,0x18,0x00);
    FONT_GLYPH('@', 0x3c,0x66,0x6e,0x6a,0x6e,0x60,0x3e,0x00);
    FONT_GLYPH('[', 0x3c,0x30,0x30,0x30,0x30,0x30,0x3c,0x00);
    FONT_GLYPH('\\',0x40,0x60,0x30,0x18,0x0c,0x06,0x02,0x00);
    FONT_GLYPH(']', 0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0x00);
    FONT_GLYPH('^', 0x18,0x3c,0x66,0x00,0x00,0x00,0x00,0x00);
    FONT_GLYPH('_', 0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x00);
    FONT_GLYPH('`', 0x30,0x18,0x0c,0x00,0x00,0x00,0x00,0x00);
    FONT_GLYPH('{', 0x0e,0x18,0x18,0x70,0x18,0x18,0x0e,0x00);
    FONT_GLYPH('|', 0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00);
    FONT_GLYPH('}', 0x70,0x18,0x18,0x0e,0x18,0x18,0x70,0x00);
    FONT_GLYPH('~', 0x00,0x00,0x76,0xdc,0x00,0x00,0x00,0x00);

    if (c == (uint8_t)' ') return 0x00;
    if (row == 0 || row == 7) return 0x7e;
    return 0x42;
}

static uint8_t font8x8_get_glyph_row(const font_renderer_t *renderer, char ch, int row) {
    (void)renderer;
    return font8x8_get_glyph_row_direct(ch, row);
}

static font_renderer_ops_t g_font8x8_ops;
static font_renderer_t g_font8x8;
static int g_font8x8_ready = 0;
static font_size_t g_font_size = FONT_SIZE_MEDIUM;
static int g_font_size_ready = 0;

static uint32_t font_scale_percent_for_size(font_size_t size) {
    switch (size) {
        case FONT_SIZE_SMALL: return 75;
        case FONT_SIZE_LARGE: return 150;
        case FONT_SIZE_MEDIUM:
        default: return 100;
    }
}

static uint32_t font_scale_dimension(uint32_t value, uint32_t percent) {
    uint32_t scaled;
    if (value == 0) return 0;
    scaled = (value * percent + 50u) / 100u;
    return scaled ? scaled : 1u;
}

static void font_init_size_once(void) {
    if (g_font_size_ready) return;
    g_font_size = FONT_SIZE_MEDIUM;
    g_font_size_ready = 1;
}

static const font_renderer_t *font_resolve_renderer(const font_renderer_t *renderer);

static void font8x8_refresh(void) {
    g_font8x8_ops.get_glyph_row = font8x8_get_glyph_row;
    g_font8x8.ops = &g_font8x8_ops;
    g_font8x8.width = FONT_DEFAULT_WIDTH;
    g_font8x8.height = FONT_DEFAULT_HEIGHT;
    g_font8x8.name = 0;
    g_font8x8_ready = 1;
}

const font_renderer_t *font_get_default(void) {
    font_init_size_once();
    if (!g_font8x8_ready || !g_font8x8.ops || !g_font8x8.ops->get_glyph_row ||
        g_font8x8.width != FONT_DEFAULT_WIDTH || g_font8x8.height != FONT_DEFAULT_HEIGHT) {
        font8x8_refresh();
    }
    return &g_font8x8;
}

void font_set_size(font_size_t size) {
    if (size < FONT_SIZE_SMALL || size > FONT_SIZE_LARGE) {
        size = FONT_SIZE_MEDIUM;
    }
    g_font_size = size;
    g_font_size_ready = 1;
}

font_size_t font_get_size(void) {
    font_init_size_once();
    return g_font_size;
}

uint32_t font_get_scale_percent(void) {
    return font_scale_percent_for_size(font_get_size());
}

uint32_t font_scale_value(uint32_t value) {
    return font_scale_dimension(value, font_get_scale_percent());
}

uint32_t font_get_ascii_width(const font_renderer_t *renderer) {
    const font_renderer_t *r = font_resolve_renderer(renderer);
    if (!r) return 0;
    return font_scale_value(r->width);
}

uint32_t font_get_ascii_height(const font_renderer_t *renderer) {
    const font_renderer_t *r = font_resolve_renderer(renderer);
    if (!r) return 0;
    return font_scale_value(r->height);
}

uint32_t font_get_unicode_width(void) {
    return font_scale_value(FONT_UNICODE_WIDTH);
}

uint32_t font_get_unicode_height(void) {
    return font_scale_value(FONT_UNICODE_HEIGHT);
}

static const font_renderer_t *font_resolve_renderer(const font_renderer_t *renderer) {
    const font_renderer_t *r = renderer ? renderer : font_get_default();
    if (!r || !r->ops || !r->ops->get_glyph_row || r->width == 0 || r->height == 0) {
        return font_get_default();
    }
    return r;
}

static int font_rect_contains(const font_rect_t *rect, int x, int y) {
    if (!rect) return 1;
    return x >= rect->x && y >= rect->y && x < rect->x + rect->w && y < rect->y + rect->h;
}

int font_decode_utf8(const char **text, uint32_t *codepoint) {
    const uint8_t *s;
    uint32_t cp;
    if (!text || !*text || !codepoint) return 0;
    s = (const uint8_t *)(*text);
    if (s[0] == 0) return 0;
    if (s[0] < 0x80u) {
        *codepoint = s[0];
        *text += 1;
        return 1;
    }
    if ((s[0] & 0xE0u) == 0xC0u && (s[1] & 0xC0u) == 0x80u) {
        cp = ((uint32_t)(s[0] & 0x1Fu) << 6) | (uint32_t)(s[1] & 0x3Fu);
        if (cp >= 0x80u) {
            *codepoint = cp;
            *text += 2;
            return 1;
        }
    } else if ((s[0] & 0xF0u) == 0xE0u &&
               (s[1] & 0xC0u) == 0x80u && (s[2] & 0xC0u) == 0x80u) {
        cp = ((uint32_t)(s[0] & 0x0Fu) << 12) |
             ((uint32_t)(s[1] & 0x3Fu) << 6) | (uint32_t)(s[2] & 0x3Fu);
        if (cp >= 0x800u && !(cp >= 0xD800u && cp <= 0xDFFFu)) {
            *codepoint = cp;
            *text += 3;
            return 1;
        }
    } else if ((s[0] & 0xF8u) == 0xF0u &&
               (s[1] & 0xC0u) == 0x80u && (s[2] & 0xC0u) == 0x80u &&
               (s[3] & 0xC0u) == 0x80u) {
        cp = ((uint32_t)(s[0] & 0x07u) << 18) |
             ((uint32_t)(s[1] & 0x3Fu) << 12) |
             ((uint32_t)(s[2] & 0x3Fu) << 6) | (uint32_t)(s[3] & 0x3Fu);
        if (cp >= 0x10000u && cp <= 0x10FFFFu) {
            *codepoint = cp;
            *text += 4;
            return 1;
        }
    }
    *codepoint = 0xFFFDu;
    *text += 1;
    return 1;
}

uint32_t font_measure_codepoint_width(const font_renderer_t *renderer, uint32_t codepoint) {
    const font_renderer_t *r = font_resolve_renderer(renderer);
    if (!r) return 0;
    if (codepoint == '\t') return font_get_ascii_width(r) * FONT_DEFAULT_TAB_SPACES;
    if (codepoint < 0x80u) return font_get_ascii_width(r);
    return font_get_unicode_width();
}

uint8_t font_get_glyph_row(const font_renderer_t *renderer, char ch, int row) {
    const font_renderer_t *r = font_resolve_renderer(renderer);
    if (!r || row < 0 || row >= (int)r->height) return 0;
    return r->ops->get_glyph_row(r, ch, row);
}

uint32_t font_get_line_height(const font_renderer_t *renderer) {
    const font_renderer_t *r = font_resolve_renderer(renderer);
    uint32_t glyph_height;
    uint32_t ascii_height;
    uint32_t unicode_height;
    if (!r) return 0;
    ascii_height = font_get_ascii_height(r);
    unicode_height = font_get_unicode_height();
    glyph_height = (ascii_height < unicode_height) ? unicode_height : ascii_height;
    return glyph_height + font_scale_value(FONT_DEFAULT_LINE_GAP);
}

uint32_t font_measure_text_width(const font_renderer_t *renderer, const char *text) {
    const font_renderer_t *r = font_resolve_renderer(renderer);
    uint32_t max_width = 0;
    uint32_t line_width = 0;
    if (!r || !text) return 0;
    while (*text) {
        uint32_t cp;
        if (!font_decode_utf8(&text, &cp)) break;
        if (cp == '\n') {
            if (line_width > max_width) max_width = line_width;
            line_width = 0;
        } else {
            line_width += font_measure_codepoint_width(r, cp);
        }
    }
    if (line_width > max_width) max_width = line_width;
    return max_width;
}

uint32_t font_measure_text_height(const font_renderer_t *renderer, const char *text) {
    const font_renderer_t *r = font_resolve_renderer(renderer);
    uint32_t lines = 1;
    if (!r || !text) return 0;
    while (*text) {
        if (*text == '\n') lines++;
        text++;
    }
    return lines * font_get_line_height(r);
}

void font_measure_text(const font_renderer_t *renderer, const char *text, font_text_metrics_t *out_metrics) {
    const font_renderer_t *r = font_resolve_renderer(renderer);
    uint32_t lines = 1;
    if (!out_metrics) return;
    out_metrics->width = 0;
    out_metrics->height = 0;
    out_metrics->lines = 0;
    if (!r || !text) return;
    out_metrics->width = font_measure_text_width(r, text);
    while (*text) {
        if (*text == '\n') lines++;
        text++;
    }
    out_metrics->lines = lines;
    out_metrics->height = lines * font_get_line_height(r);
}

void font_draw_char_clipped(const font_renderer_t *renderer, font_put_pixel_fn put_pixel, void *ctx,
                            const font_rect_t *clip, int x, int y, char ch, uint32_t color) {
    const font_renderer_t *r = font_resolve_renderer(renderer);
    int row;
    int col;
    if (!r || !put_pixel) return;
    if ((uint8_t)ch < 32 || ch == ' ' || ch == '\t' || ch == '\n') return;

    {
        uint32_t scaled_w = font_get_ascii_width(r);
        uint32_t scaled_h = font_get_ascii_height(r);
        if (!scaled_w || !scaled_h) return;
        for (row = 0; row < (int)scaled_h; row++) {
            int src_row = (int)((uint32_t)row * r->height / scaled_h);
            uint8_t bits = font_get_glyph_row(r, ch, src_row);
            for (col = 0; col < (int)scaled_w; col++) {
                int src_col = (int)((uint32_t)col * r->width / scaled_w);
                int px;
                int py;
                if (src_col >= 8) continue;
                if ((bits & (uint8_t)(0x80u >> src_col)) == 0) continue;
                px = x + col;
                py = y + row;
                if (font_rect_contains(clip, px, py)) put_pixel(ctx, px, py, color);
            }
        }
    }
}

void font_draw_char(const font_renderer_t *renderer, font_put_pixel_fn put_pixel, void *ctx,
                    int x, int y, char ch, uint32_t color) {
    font_draw_char_clipped(renderer, put_pixel, ctx, 0, x, y, ch, color);
}

static void font_draw_codepoint_at_y_clipped(const font_renderer_t *renderer, font_put_pixel_fn put_pixel, void *ctx,
                                             const font_rect_t *clip, int x, int y, uint32_t codepoint,
                                             uint32_t color, int ascii_baseline_shift) {
    const font_renderer_t *r = font_resolve_renderer(renderer);
    const uint16_t *cjk_rows;
    int row, col;
    if (!r || !put_pixel) return;
    if (codepoint < 0x80u) {
        font_draw_char_clipped(r, put_pixel, ctx, clip, x, y + ascii_baseline_shift, (char)codepoint, color);
        return;
    }

    cjk_rows = font_find_cjk_rows(codepoint);
    {
        uint32_t scaled_w = font_get_unicode_width();
        uint32_t scaled_h = font_get_unicode_height();
        if (!scaled_w || !scaled_h) return;
        for (row = 0; row < (int)scaled_h; row++) {
            int src_row = (int)((uint32_t)row * FONT_UNICODE_HEIGHT / scaled_h);
            uint16_t bits;
            if (cjk_rows) {
                bits = cjk_rows[src_row];
            } else if (font_codepoint_is_cjk(codepoint)) {
                bits = font_missing_cjk_row(codepoint, src_row);
            } else {
                bits = font_unicode_fallback_row(codepoint, src_row);
            }
            for (col = 0; col < (int)scaled_w; col++) {
                int src_col = (int)((uint32_t)col * FONT_UNICODE_WIDTH / scaled_w);
                int px;
                int py;
                if (src_col >= FONT_UNICODE_WIDTH) continue;
                if ((bits & (uint16_t)(0x8000u >> src_col)) == 0) continue;
                px = x + col;
                py = y + row;
                if (font_rect_contains(clip, px, py)) put_pixel(ctx, px, py, color);
            }
        }
    }
}

void font_draw_codepoint_clipped(const font_renderer_t *renderer, font_put_pixel_fn put_pixel, void *ctx,
                                 const font_rect_t *clip, int x, int y, uint32_t codepoint, uint32_t color) {
    font_draw_codepoint_at_y_clipped(renderer, put_pixel, ctx, clip, x, y, codepoint, color, 0);
}

static int font_line_contains_wide_glyph(const char *text) {
    const char *scan = text;
    if (!scan) return 0;
    while (*scan && *scan != '\n') {
        uint32_t cp;
        if (!font_decode_utf8(&scan, &cp)) break;
        if (cp >= 0x80u) return 1;
    }
    return 0;
}

static int font_ascii_baseline_shift_for_line(const font_renderer_t *renderer, const char *line) {
    const font_renderer_t *r = font_resolve_renderer(renderer);
    uint32_t ascii_h;
    uint32_t unicode_h;
    if (!r || !font_line_contains_wide_glyph(line)) return 0;
    ascii_h = font_get_ascii_height(r);
    unicode_h = font_get_unicode_height();
    if (ascii_h >= unicode_h) return 0;
    return (int)((unicode_h - ascii_h) / 2u);
}

void font_draw_text_clipped(const font_renderer_t *renderer, font_put_pixel_fn put_pixel, void *ctx,
                            const font_rect_t *clip, int x, int y, const char *text, uint32_t color) {
    const font_renderer_t *r = font_resolve_renderer(renderer);
    int cx = x;
    int cy = y;
    int ascii_shift;
    if (!r || !put_pixel || !text) return;
    ascii_shift = font_ascii_baseline_shift_for_line(r, text);

    while (*text) {
        uint32_t cp;
        if (!font_decode_utf8(&text, &cp)) break;
        if (cp == '\n') {
            cy += (int)font_get_line_height(r);
            cx = x;
            ascii_shift = font_ascii_baseline_shift_for_line(r, text);
        } else if (cp == '\t') {
            cx += (int)font_measure_codepoint_width(r, cp);
        } else {
            int cp_shift = (cp < 0x80u) ? ascii_shift : 0;
            font_draw_codepoint_at_y_clipped(r, put_pixel, ctx, clip, cx, cy, cp, color, cp_shift);
            cx += (int)font_measure_codepoint_width(r, cp);
        }
    }
}

void font_draw_text(const font_renderer_t *renderer, font_put_pixel_fn put_pixel, void *ctx,
                    int x, int y, const char *text, uint32_t color) {
    font_draw_text_clipped(renderer, put_pixel, ctx, 0, x, y, text, color);
}

#undef FONT_GLYPH
#undef FONT_ROW8
