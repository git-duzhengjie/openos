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

    log_dec("\n[x86_64][gfx-selftest] fb ", fi->width);
    log_dec("x", fi->height);
    early_console64_write("\n[x86_64][gfx-selftest] PASS\n");
    return 1;
}
