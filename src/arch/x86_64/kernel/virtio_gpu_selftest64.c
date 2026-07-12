#include "../include/virtio_gpu_selftest64.h"
#include "../include/early_console64.h"
#include "virtio_gpu.h"

#include <stdint.h>

/* M6.4 — verify the virtio-gpu 2D driver public surface + present path.
 * If no virtio-gpu device is attached the driver reports device_count==0 and
 * this test passes as a no-op (graceful degradation on GOP-only boots). */

static void log_dec(const char *key, uint32_t val)
{
    char buf[12];
    int i = 0;
    early_console64_write(key);
    if (val == 0) { early_console64_write("0"); return; }
    char tmp[12];
    int n = 0;
    while (val > 0 && n < 11) { tmp[n++] = (char)('0' + (val % 10)); val /= 10; }
    while (n > 0) buf[i++] = tmp[--n];
    buf[i] = '\0';
    early_console64_write(buf);
}

int arch_x86_64_virtio_gpu_selftest_run(void)
{
    early_console64_write("\n[x86_64][virtio-gpu-selftest] begin");

    uint32_t count = virtio_gpu_device_count();
    if (count == 0) {
        early_console64_write(
            "\n[x86_64][virtio-gpu-selftest] PASS (device absent, no-op)\n");
        return 1;
    }

    /* 1. geometry must be sane */
    uint32_t w = virtio_gpu_width();
    uint32_t h = virtio_gpu_height();
    log_dec("\n[x86_64][virtio-gpu-selftest] scanout w=", w);
    log_dec(" h=", h);
    if (w == 0 || h == 0 || w > 16384 || h > 16384) {
        early_console64_write(
            "\n[x86_64][virtio-gpu-selftest] FAIL bad geometry\n");
        return 1;
    }

    /* 2. backing store must be non-NULL */
    uint8_t *fb = (uint8_t *)virtio_gpu_framebuffer();
    if (fb == 0) {
        early_console64_write(
            "\n[x86_64][virtio-gpu-selftest] FAIL null backing store\n");
        return 1;
    }

    /* 3. draw a probe pattern into a small top-left rect (BGRA) */
    uint32_t rw = (w < 64) ? w : 64;
    uint32_t rh = (h < 64) ? h : 64;
    uint32_t *px = (uint32_t *)fb;
    for (uint32_t y = 0; y < rh; y++) {
        for (uint32_t x = 0; x < rw; x++) {
            /* gradient: B=x, G=y, R=0x40, A=0xff */
            px[y * w + x] = 0xFF400000u | ((y & 0xFF) << 8) | (x & 0xFF);
        }
    }

    /* 4. flush the probe rect to the host scanout */
    int rc = virtio_gpu_flush_rect(0, 0, rw, rh);
    log_dec("\n[x86_64][virtio-gpu-selftest] flush rc=", (uint32_t)(rc & 0xFF));
    if (rc != 0) {
        early_console64_write(
            "\n[x86_64][virtio-gpu-selftest] FAIL flush failed\n");
        return 1;
    }

    /* 5. fully out-of-range flush origin must be a clipped no-op (rc==0) */
    if (virtio_gpu_flush_rect(w, 0, 8, 8) != 0 ||
        virtio_gpu_flush_rect(0, h, 8, 8) != 0) {
        early_console64_write(
            "\n[x86_64][virtio-gpu-selftest] FAIL oob flush not clipped\n");
        return 1;
    }

    /* 6. oversized rect must be clipped and still succeed */
    if (virtio_gpu_flush_rect(0, 0, w + 100, h + 100) != 0) {
        early_console64_write(
            "\n[x86_64][virtio-gpu-selftest] FAIL oversized flush not clipped\n");
        return 1;
    }

    early_console64_write("\n[x86_64][virtio-gpu-selftest] PASS\n");
    return 1;
}
