#include "aarch64_i2c_bus.h"

#define AARCH64_I2C_MAX_BUSES 4u

static aarch64_i2c_bus_t *g_i2c_buses[AARCH64_I2C_MAX_BUSES];

int aarch64_i2c_bus_register(aarch64_i2c_bus_t *bus,
                             uint32_t bus_id,
                             const char *name,
                             const aarch64_i2c_bus_ops_t *ops,
                             void *ctx)
{
    if (!bus || !ops || bus_id >= AARCH64_I2C_MAX_BUSES) {
        return -1;
    }
    bus->name = name;
    bus->ops = ops;
    bus->ctx = ctx;
    bus->bus_id = bus_id;
    bus->xfer_count = 0;
    bus->err_count = 0;
    bus->ready = 1;
    g_i2c_buses[bus_id] = bus;
    return 0;
}

aarch64_i2c_bus_t *aarch64_i2c_bus_get(uint32_t bus_id)
{
    if (bus_id >= AARCH64_I2C_MAX_BUSES) {
        return 0;
    }
    return g_i2c_buses[bus_id];
}

int aarch64_i2c_transfer(aarch64_i2c_bus_t *bus,
                         aarch64_i2c_msg_t *msgs,
                         uint32_t n)
{
    if (!bus || !bus->ready || !bus->ops || !bus->ops->transfer) {
        return -1;
    }
    int rc = bus->ops->transfer(bus->ctx, msgs, n);
    if (rc == 0) {
        bus->xfer_count++;
    } else {
        bus->err_count++;
    }
    return rc;
}

int aarch64_i2c_write_reg16(aarch64_i2c_bus_t *bus,
                            uint16_t addr,
                            uint16_t reg,
                            const uint8_t *data,
                            uint16_t len)
{
    /* Compose single write message: [reg_hi, reg_lo, data...]. Copy into
     * a static scratch buffer to keep the API zero-heap. */
    static uint8_t scratch[128];
    if (!bus || !data || (uint32_t)len + 2u > sizeof(scratch)) {
        return -1;
    }
    scratch[0] = (uint8_t)((reg >> 8) & 0xff);
    scratch[1] = (uint8_t)(reg & 0xff);
    for (uint16_t i = 0; i < len; i++) {
        scratch[2 + i] = data[i];
    }
    aarch64_i2c_msg_t msg;
    msg.addr = addr;
    msg.flags = 0;
    msg.len = (uint16_t)(len + 2u);
    msg.buf = scratch;
    return aarch64_i2c_transfer(bus, &msg, 1);
}

int aarch64_i2c_read_reg16(aarch64_i2c_bus_t *bus,
                           uint16_t addr,
                           uint16_t reg,
                           uint8_t *data,
                           uint16_t len)
{
    if (!bus || !data) {
        return -1;
    }
    uint8_t reg_buf[2];
    reg_buf[0] = (uint8_t)((reg >> 8) & 0xff);
    reg_buf[1] = (uint8_t)(reg & 0xff);

    aarch64_i2c_msg_t msgs[2];
    msgs[0].addr = addr;
    msgs[0].flags = 0;
    msgs[0].len = 2;
    msgs[0].buf = reg_buf;
    msgs[1].addr = addr;
    msgs[1].flags = AARCH64_I2C_M_READ;
    msgs[1].len = len;
    msgs[1].buf = data;
    return aarch64_i2c_transfer(bus, msgs, 2);
}

/* ============================================================
 *   Stub controller: memory-backed device array for selftest.
 * ============================================================ */

static aarch64_i2c_stub_dev_t *stub_find(aarch64_i2c_stub_ctx_t *sc,
                                         uint16_t addr)
{
    for (uint32_t i = 0; i < sc->device_count; i++) {
        if (sc->devices[i].addr == addr) {
            return &sc->devices[i];
        }
    }
    return 0;
}

static int stub_transfer(void *ctx, aarch64_i2c_msg_t *msgs, uint32_t n)
{
    aarch64_i2c_stub_ctx_t *sc = (aarch64_i2c_stub_ctx_t *)ctx;
    if (!sc || !msgs || n == 0) {
        return -1;
    }
    for (uint32_t i = 0; i < n; i++) {
        aarch64_i2c_msg_t *m = &msgs[i];
        aarch64_i2c_stub_dev_t *dev = stub_find(sc, m->addr);
        if (!dev) {
            return -1;
        }
        if ((m->flags & AARCH64_I2C_M_READ) == 0) {
            /* Write: first two bytes = 16-bit register pointer, rest = data */
            if (m->len < 2) {
                return -1;
            }
            uint16_t reg = (uint16_t)(((uint16_t)m->buf[0] << 8) | m->buf[1]);
            dev->reg_ptr = reg;
            uint16_t payload_len = (uint16_t)(m->len - 2u);
            for (uint16_t k = 0; k < payload_len; k++) {
                uint32_t off = (uint32_t)dev->reg_ptr + k - dev->base_addr;
                if (dev->reg_ptr + k < dev->base_addr || off >= dev->reg_size) {
                    return -1;
                }
                dev->regs[off] = m->buf[2 + k];
            }
            dev->reg_ptr = (uint16_t)(dev->reg_ptr + payload_len);
        } else {
            for (uint16_t k = 0; k < m->len; k++) {
                uint32_t off = (uint32_t)dev->reg_ptr + k - dev->base_addr;
                if (dev->reg_ptr + k < dev->base_addr || off >= dev->reg_size) {
                    return -1;
                }
                m->buf[k] = dev->regs[off];
            }
            dev->reg_ptr = (uint16_t)(dev->reg_ptr + m->len);
        }
    }
    return 0;
}

static int stub_probe(void *ctx, uint16_t addr)
{
    aarch64_i2c_stub_ctx_t *sc = (aarch64_i2c_stub_ctx_t *)ctx;
    return sc && stub_find(sc, addr) ? 0 : -1;
}

static const aarch64_i2c_bus_ops_t g_stub_ops = {
    .transfer = stub_transfer,
    .probe    = stub_probe,
};

int aarch64_i2c_stub_init(aarch64_i2c_bus_t *bus,
                          uint32_t bus_id,
                          aarch64_i2c_stub_ctx_t *ctx,
                          aarch64_i2c_stub_dev_t *devices,
                          uint32_t device_count)
{
    if (!bus || !ctx || !devices) {
        return -1;
    }
    ctx->devices = devices;
    ctx->device_count = device_count;
    return aarch64_i2c_bus_register(bus, bus_id, "i2c-stub", &g_stub_ops, ctx);
}
