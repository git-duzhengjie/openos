#include "openos.h"

int main(int argc, char **argv)
{
    openos_font_query_t q;
    const char *sample = "OpenOS 字体";
    (void)argc;
    (void)argv;

    memset(&q, 0, sizeof(q));
    q.codepoint = 0x5B57u; /* 字 */
    strncpy(q.text, sample, sizeof(q.text) - 1);

    if (openos_font_query(&q) < 0) {
        printf("fontprobe: SYS_FONT_QUERY failed\n");
        return 1;
    }

    printf("fontprobe: ascii=%ux%u unicode=%ux%u line=%u scale=%u%% size=%u\n",
           q.ascii_width, q.ascii_height,
           q.unicode_width, q.unicode_height,
           q.line_height, q.scale_percent, q.font_size);
    printf("fontprobe: cjk loaded=%u glyphs=%u cell=%ux%u cp U+%x width=%u\n",
           q.cjk_loaded, q.cjk_glyph_count, q.cjk_width, q.cjk_height,
           q.codepoint, q.codepoint_width);
    printf("fontprobe: text '%s' metrics=%ux%u lines=%u\n",
           q.text, q.text_width, q.text_height, q.text_lines);
    return 0;
}
