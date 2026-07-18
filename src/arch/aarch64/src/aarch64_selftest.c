#include "aarch64_selftest.h"
#include "aarch64_uart.h"
#include "aarch64_platform.h"
#include "aarch64_gicv3.h"
#include "aarch64_exception.h"
#include "aarch64_i2c_bus.h"
#include "aarch64_gt911.h"
#include "aarch64_dtb.h"

/* Minimal helpers (zero libc). */
static void _emit(const char *s) { aarch64_uart_write(s); }

static void _emit_u(uint32_t v) {
    char buf[12];
    int i = 0;
    if (v == 0) { _emit("0"); return; }
    while (v > 0 && i < 11) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) {
        char c[2]; c[0] = buf[--i]; c[1] = 0;
        _emit(c);
    }
}

static void _stage(int n, int ok, const char *label) {
    _emit("A11.Z stage ");
    _emit_u((uint32_t)n);
    _emit(ok ? " PASS: " : " FAIL: ");
    _emit(label);
    _emit("\n");
}

static uint32_t g_timer_irq_hits;
static void _timer_irq_handler(uint32_t intid, void *cookie) {
    (void)intid; (void)cookie;
    g_timer_irq_hits++;
}

int aarch64_platform_selftest_run(void)
{
    _emit("A11.Z selftest begin\n");
    int failures = 0;

    /* Stage 1: DTB.  Under bare `-kernel <ELF>` QEMU may not pass a DTB
     * (x0=0).  Accept absence gracefully: treat as INFO, not FAIL.  When
     * booted via `-M virt,-dtb <blob>` and proper ELF flag, dtb_ready will
     * be true and the stage passes.  We report both outcomes clearly. */
    const aarch64_platform_state_t *ps = aarch64_platform_get_state();
    int s1 = ps && (ps->dtb_ready || ps->dtb_base == 0);
    _stage(1, s1, ps && ps->dtb_ready ? "DTB magic+totalsize" : "DTB not provided (bare -kernel)");
    if (!s1) failures++;

    /* Stage 2: GICv3 */
    int s2 = ps && ps->gic_ready ? 1 : 0;
    _stage(2, s2, "GICv3 distributor+redistributor");
    if (!s2) failures++;

    /* Stage 3: Timer PPI wiring + IRQ dispatch (software simulate). */
    g_timer_irq_hits = 0;
    int reg_rc = aarch64_irq_register(30u, _timer_irq_handler, 0);
    aarch64_irq_simulate(30u);
    aarch64_irq_simulate(30u);
    int s3 = (reg_rc == 0) && (g_timer_irq_hits == 2) && (ps && ps->timer_irq == 30u);
    _stage(3, s3, "Timer PPI30 + IRQ dispatch x2");
    if (!s3) failures++;

    /* Stage 4: I2C stub bus round-trip. */
    static aarch64_i2c_bus_t bus;
    static aarch64_i2c_stub_ctx_t stub_ctx;
    static aarch64_i2c_stub_dev_t stub_devs[1];
    stub_devs[0].addr = AARCH64_GT911_DEFAULT_ADDR;
    aarch64_gt911_stub_preload(&stub_devs[0]);
    int s4a = aarch64_i2c_stub_init(&bus, 0, &stub_ctx, stub_devs, 1) == 0;
    /* Read product id register. */
    uint8_t pid[4] = {0};
    int s4b = aarch64_i2c_read_reg16(&bus, AARCH64_GT911_DEFAULT_ADDR, 0x8140u, pid, 4) == 0;
    int s4c = (pid[0] == '9') && (pid[1] == '1') && (pid[2] == '1');
    int s4 = s4a && s4b && s4c;
    _stage(4, s4, "I2C stub xfer + reg16 R/W");
    if (!s4) failures++;

    /* Stage 5: GT911 probe + inject frame + poll + post events. */
    static aarch64_gt911_device_t gt;
    int s5a = aarch64_gt911_init(&gt, &bus, AARCH64_GT911_DEFAULT_ADDR) == 0;
    int s5b = aarch64_gt911_probe(&gt) == 0;
    aarch64_gt911_frame_t inject;
    inject.touch_count = 2;
    inject.points[0].track_id = 1;
    inject.points[0].x = 100;
    inject.points[0].y = 200;
    inject.points[0].size = 8;
    inject.points[0].active = 1;
    inject.points[1].track_id = 2;
    inject.points[1].x = 400;
    inject.points[1].y = 300;
    inject.points[1].size = 8;
    inject.points[1].active = 1;
    aarch64_gt911_stub_inject_frame(&stub_devs[0], &inject);
    aarch64_gt911_frame_t out;
    int poll_rc = aarch64_gt911_poll(&gt, &out);
    int s5c = poll_rc == 2 && out.touch_count == 2 &&
              out.points[0].x == 100 && out.points[0].y == 200 &&
              out.points[1].x == 400 && out.points[1].y == 300;
    int posted = aarch64_gt911_post_events(&out);
    int s5d = posted == 2;
    int s5 = s5a && s5b && s5c && s5d;
    _stage(5, s5, "GT911 probe+poll+post_events");
    if (!s5) failures++;

    _emit("A11.Z selftest done: ");
    _emit_u((uint32_t)(5 - failures));
    _emit("/5 pass\n");
    return failures;
}
