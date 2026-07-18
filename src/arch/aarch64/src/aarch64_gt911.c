#include "aarch64_gt911.h"

/* Cross-arch input core hook: only wire when building the full kernel.
 * For the standalone aarch64 target selftest, we still get the symbols
 * because input_core.c is compiled into the kernel image. */
#include "../../../kernel/include/input_core.h"

#define GT911_REG_PRODUCT_ID 0x8140u
#define GT911_REG_STATUS     0x814Eu
#define GT911_REG_POINT0     0x814Fu
#define GT911_POINT_STRIDE   8u

/* Device id assigned by input_core; lazily initialised. */
static uint16_t g_gt911_input_dev_id = 0;

static uint16_t gt911_input_device(void)
{
    if (g_gt911_input_dev_id == 0) {
        g_gt911_input_dev_id = input_device_register(INPUT_DEV_TOUCH_I2C, "gt911");
    }
    return g_gt911_input_dev_id;
}

int aarch64_gt911_init(aarch64_gt911_device_t *dev,
                       aarch64_i2c_bus_t *bus,
                       uint16_t addr)
{
    if (!dev || !bus) {
        return -1;
    }
    dev->bus = bus;
    dev->addr = addr ? addr : AARCH64_GT911_DEFAULT_ADDR;
    dev->probed = 0;
    dev->poll_count = 0;
    dev->report_count = 0;
    dev->error_count = 0;
    for (int i = 0; i < 8; i++) dev->product_id[i] = 0;
    return 0;
}

int aarch64_gt911_probe(aarch64_gt911_device_t *dev)
{
    if (!dev || !dev->bus) {
        return -1;
    }
    uint8_t pid[4];
    if (aarch64_i2c_read_reg16(dev->bus, dev->addr, GT911_REG_PRODUCT_ID, pid, 4) != 0) {
        dev->error_count++;
        return -1;
    }
    dev->product_id[0] = (char)pid[0];
    dev->product_id[1] = (char)pid[1];
    dev->product_id[2] = (char)pid[2];
    dev->product_id[3] = (char)pid[3];
    dev->product_id[4] = 0;
    /* Accept any non-zero product id (real GT911 returns "911\0", but
     * different vendor rebadges use similar ASCII). */
    if (pid[0] == 0 && pid[1] == 0 && pid[2] == 0) {
        dev->error_count++;
        return -1;
    }
    dev->probed = 1;
    return 0;
}

int aarch64_gt911_poll(aarch64_gt911_device_t *dev,
                       aarch64_gt911_frame_t *frame)
{
    if (!dev || !dev->bus || !dev->probed || !frame) {
        return -1;
    }
    dev->poll_count++;
    uint8_t status = 0;
    if (aarch64_i2c_read_reg16(dev->bus, dev->addr, GT911_REG_STATUS, &status, 1) != 0) {
        dev->error_count++;
        return -1;
    }
    frame->touch_count = 0;
    for (uint32_t i = 0; i < AARCH64_GT911_MAX_TOUCHES; i++) {
        frame->points[i].active = 0;
    }
    if ((status & 0x80u) == 0) {
        /* No buffer_ready → no fresh data. */
        return 0;
    }
    uint32_t count = status & 0x0fu;
    if (count > AARCH64_GT911_MAX_TOUCHES) {
        count = AARCH64_GT911_MAX_TOUCHES;
    }
    if (count > 0) {
        uint8_t buf[AARCH64_GT911_MAX_TOUCHES * GT911_POINT_STRIDE];
        if (aarch64_i2c_read_reg16(dev->bus, dev->addr, GT911_REG_POINT0,
                                   buf, (uint16_t)(count * GT911_POINT_STRIDE)) != 0) {
            dev->error_count++;
            return -1;
        }
        for (uint32_t i = 0; i < count; i++) {
            uint8_t *p = &buf[i * GT911_POINT_STRIDE];
            frame->points[i].track_id = p[0];
            frame->points[i].x        = (uint16_t)(((uint16_t)p[2] << 8) | p[1]);
            frame->points[i].y        = (uint16_t)(((uint16_t)p[4] << 8) | p[3]);
            frame->points[i].size     = (uint16_t)(((uint16_t)p[6] << 8) | p[5]);
            frame->points[i].active   = 1;
        }
    }
    frame->touch_count = count;
    /* Clear the buffer_ready flag by writing 0 back to status. */
    uint8_t zero = 0;
    (void)aarch64_i2c_write_reg16(dev->bus, dev->addr, GT911_REG_STATUS, &zero, 1);
    dev->report_count++;
    return (int)count;
}

int aarch64_gt911_post_events(const aarch64_gt911_frame_t *frame)
{
    if (!frame) return 0;
    uint16_t dev_id = gt911_input_device();
    if (!dev_id) return 0;

    int posted = 0;
    for (uint32_t i = 0; i < frame->touch_count && i < AARCH64_GT911_MAX_TOUCHES; i++) {
        const aarch64_gt911_touch_t *t = &frame->points[i];
        if (!t->active) continue;
        input_event_t ev;
        ev.timestamp_ms = 0;
        ev.dev_id       = dev_id;
        ev.type         = INPUT_EV_ABS;
        ev.code         = INPUT_TOUCH_CONTACT;
        ev.value        = (int32_t)(((uint32_t)t->track_id << 24) | (1u << 16));
        ev.x            = (int32_t)t->x;
        ev.y            = (int32_t)t->y;
        input_report(&ev);
        posted++;
    }
    /* Frame-end marker (M8-C convention). */
    input_event_t syn;
    syn.timestamp_ms = 0;
    syn.dev_id       = dev_id;
    syn.type         = INPUT_EV_SYN;
    syn.code         = INPUT_TOUCH_FRAME_END;
    syn.value        = (int32_t)frame->touch_count;
    syn.x = 0;
    syn.y = 0;
    input_report(&syn);
    return posted;
}

/* ---------------- Stub preload helpers (selftest) ---------------- */

#define GT911_STUB_BASE 0x8140u

void aarch64_gt911_stub_preload(aarch64_i2c_stub_dev_t *dev)
{
    if (!dev) return;
    dev->base_addr = GT911_STUB_BASE;
    dev->reg_size = 512;
    for (int i = 0; i < 512; i++) dev->regs[i] = 0;
    /* Product ID @ 0x8140 → "911\0" */
    dev->regs[0] = '9';
    dev->regs[1] = '1';
    dev->regs[2] = '1';
    dev->regs[3] = 0;
}

void aarch64_gt911_stub_inject_frame(aarch64_i2c_stub_dev_t *dev,
                                     const aarch64_gt911_frame_t *frame)
{
    if (!dev || !frame) return;
    uint32_t count = frame->touch_count;
    if (count > AARCH64_GT911_MAX_TOUCHES) count = AARCH64_GT911_MAX_TOUCHES;
    /* Status @ 0x814E → offset 0x0E from base 0x8140. */
    uint32_t status_off = GT911_REG_STATUS - GT911_STUB_BASE;
    dev->regs[status_off] = (uint8_t)(0x80u | (count & 0x0fu));
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = (GT911_REG_POINT0 - GT911_STUB_BASE) + i * GT911_POINT_STRIDE;
        uint8_t *p = &dev->regs[off];
        p[0] = frame->points[i].track_id;
        p[1] = (uint8_t)(frame->points[i].x & 0xff);
        p[2] = (uint8_t)((frame->points[i].x >> 8) & 0xff);
        p[3] = (uint8_t)(frame->points[i].y & 0xff);
        p[4] = (uint8_t)((frame->points[i].y >> 8) & 0xff);
        p[5] = (uint8_t)(frame->points[i].size & 0xff);
        p[6] = (uint8_t)((frame->points[i].size >> 8) & 0xff);
        p[7] = 0;
    }
}
