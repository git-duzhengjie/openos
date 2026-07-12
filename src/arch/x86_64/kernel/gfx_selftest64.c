#include "../include/gfx_selftest64.h"
#include "../include/early_console64.h"
#include "framebuffer.h"

#include <stdint.h>

/* M6.3 — verify the framebuffer_blit_row acceleration primitive.
 * Writes a few probe rows into VRAM, reads them back via framebuffer_get_pixel,
 * validates horizontal clipping / NULL-safety, then restores the pixels it
 * touched so the boot splash is left untouched. */

static void log_dec(const char *key, uint32_t val)
{
    char buf[12];
    int i = 0;
    early_console64_write(key);
    if (val == 0) {
        early_console64_write("0");
        return;
    }
    char tmp[12];
    int n = 0;
    while (val > 0 && n < 11) {
        tmp[n++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (n > 0) buf[i++] = tmp[--n];
    buf[i] = '\0';
    early_console64_write(buf);
}

int arch_x86_64_gfx_selftest_run(void)
{
    early_console64_write("\n[x86_64][gfx-selftest] begin");

    const framebuffer_info_t *fi = framebuffer_get_info();
    const framebuffer_caps_t *fc = framebuffer_get_caps();
    if (fi == 0 || fc == 0 || fi->width == 0 || fi->height == 0) {
        early_console64_write("\n[x86_64][gfx-selftest] FAIL no framebuffer\n");
        return 0;
    }

    /* 1. ROW_BLIT capability must be advertised (32bpp linear). */
    if (!(fc->flags & FRAMEBUFFER_CAP_ROW_BLIT)) {
        early_console64_write("\n[x86_64][gfx-selftest] FAIL ROW_BLIT cap missing\n");
        return 0;
    }

    /* 2. NULL source must be rejected (return 0, no fault). */
    if (framebuffer_blit_row(0, 0, 0, 8) != 0) {
        early_console64_write("\n[x86_64][gfx-selftest] FAIL null src not rejected\n");
        return 0;
    }

    /* 3. Fully out-of-bounds origin must be rejected. */
    uint32_t probe[16];
    for (int i = 0; i < 16; i++) probe[i] = 0x00112233u + (uint32_t)i;
    if (framebuffer_blit_row(fi->width, 0, probe, 4) != 0 ||
        framebuffer_blit_row(0, fi->height, probe, 4) != 0) {
        early_console64_write("\n[x86_64][gfx-selftest] FAIL oob origin not rejected\n");
        return 0;
    }

    /* 4. Horizontal clipping: request more than remaining width -> clipped. */
    uint32_t near_edge = fi->width - 3;
    uint32_t written = framebuffer_blit_row(near_edge, 0, probe, 16);
    if (written != 3) {
        log_dec("\n[x86_64][gfx-selftest] FAIL clip written=", written);
        early_console64_write("\n");
        return 0;
    }

    /* 5. Write-then-readback correctness on a probe row (row 0). Save first. */
    uint32_t saved[16];
    uint32_t test_w = 8;
    for (uint32_t i = 0; i < test_w; i++) {
        saved[i] = framebuffer_get_pixel(i, 0);
    }
    uint32_t w2 = framebuffer_blit_row(0, 0, probe, test_w);
    int ok = (w2 == test_w);
    for (uint32_t i = 0; i < test_w && ok; i++) {
        uint32_t got = framebuffer_get_pixel(i, 0);
        /* Compare only the 24-bit RGB payload; alpha byte may be masked. */
        if ((got & 0x00FFFFFFu) != (probe[i] & 0x00FFFFFFu)) {
            ok = 0;
        }
    }
    /* Restore original pixels regardless of result. */
    framebuffer_blit_row(0, 0, saved, test_w);
    /* Also restore the near-edge probe from check 4. */
    for (uint32_t i = 0; i < 3; i++) {
        framebuffer_put_pixel(near_edge + i, 0, framebuffer_get_pixel(near_edge + i, 0));
    }
    if (!ok) {
        early_console64_write("\n[x86_64][gfx-selftest] FAIL readback mismatch\n");
        return 0;
    }

    /* 6. M6.3d RECT_BLIT capability must be advertised. */
    if (!(fc->flags & FRAMEBUFFER_CAP_RECT_BLIT)) {
        early_console64_write("\n[x86_64][gfx-selftest] FAIL RECT_BLIT cap missing\n");
        return 0;
    }

    /* 7. RECT_BLIT NULL / degenerate / oob rejection. */
    if (framebuffer_blit_rect(0, 0, 0, 4, 2, 2) != 0 ||
        framebuffer_blit_rect(0, 0, probe, 4, 0, 2) != 0 ||
        framebuffer_blit_rect(0, 0, probe, 4, 2, 0) != 0 ||
        framebuffer_blit_rect(fi->width, 0, probe, 4, 2, 2) != 0 ||
        framebuffer_blit_rect(0, fi->height, probe, 4, 2, 2) != 0) {
        early_console64_write("\n[x86_64][gfx-selftest] FAIL rect reject\n");
        return 0;
    }

    /* 8. RECT_BLIT vertical clipping: request more rows than remaining -> clipped. */
    uint32_t rh = framebuffer_blit_rect(0, fi->height - 2, probe, 4, 2, 16);
    if (rh != 2) {
        log_dec("\n[x86_64][gfx-selftest] FAIL rect vclip rows=", rh);
        early_console64_write("\n");
        return 0;
    }
    /* Restore the 2x2 region touched by check 8. */
    for (uint32_t ry = 0; ry < 2; ry++) {
        for (uint32_t rx = 0; rx < 2; rx++) {
            uint32_t px = rx;
            uint32_t py = fi->height - 2 + ry;
            framebuffer_put_pixel(px, py, framebuffer_get_pixel(px, py));
        }
    }

    /* 9. RECT_BLIT write-then-readback correctness (2x3 block at row 0, x=0). */
    uint32_t src2[6] = { 0x00A1B2C3u, 0x00C3B2A1u, 0x00445566u,
                         0x00665544u, 0x00778899u, 0x00998877u };
    uint32_t rsaved[6];
    for (uint32_t ry = 0; ry < 3; ry++) {
        for (uint32_t rx = 0; rx < 2; rx++) {
            rsaved[ry * 2 + rx] = framebuffer_get_pixel(rx, ry);
        }
    }
    uint32_t rrows = framebuffer_blit_rect(0, 0, src2, 2, 2, 3);
    int rok = (rrows == 3);
    for (uint32_t ry = 0; ry < 3 && rok; ry++) {
        for (uint32_t rx = 0; rx < 2 && rok; rx++) {
            uint32_t got = framebuffer_get_pixel(rx, ry);
            if ((got & 0x00FFFFFFu) != (src2[ry * 2 + rx] & 0x00FFFFFFu)) {
                rok = 0;
            }
        }
    }
    /* Restore original pixels of the 2x3 block. */
    framebuffer_blit_rect(0, 0, rsaved, 2, 2, 3);
    if (!rok) {
        early_console64_write("\n[x86_64][gfx-selftest] FAIL rect readback mismatch\n");
        return 0;
    }

    log_dec("\n[x86_64][gfx-selftest] fb ", fi->width);
    log_dec("x", fi->height);
    early_console64_write("\n[x86_64][gfx-selftest] PASS\n");
    return 1;
}
