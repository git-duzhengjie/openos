#ifndef OPENOS_ARCH_AARCH64_I2C_BUS_H
#define OPENOS_ARCH_AARCH64_I2C_BUS_H

#include <stdint.h>
#include <stddef.h>

/*
 * Minimal I2C bus abstraction for M11-D.
 *
 * QEMU virt has no real GT911, so we ship a stub controller ops table
 * that a test harness can drive.  On real ARM SoCs a proper controller
 * driver (mtk-i2c / rk3399-i2c / sun6i-i2c) plugs in via aarch64_i2c_bus_ops_t.
 *
 * Zero heap: single global bus, callers pass buffers.
 */

typedef struct aarch64_i2c_msg {
    uint16_t addr;      /* 7-bit slave address */
    uint16_t flags;     /* bit 0: 1=read, 0=write */
    uint16_t len;
    uint8_t *buf;
} aarch64_i2c_msg_t;

#define AARCH64_I2C_M_READ  0x0001u

typedef struct aarch64_i2c_bus_ops {
    int (*transfer)(void *ctx, aarch64_i2c_msg_t *msgs, uint32_t n);
    int (*probe)(void *ctx, uint16_t addr);
} aarch64_i2c_bus_ops_t;

typedef struct aarch64_i2c_bus {
    const char             *name;
    const aarch64_i2c_bus_ops_t *ops;
    void                   *ctx;
    uint32_t                bus_id;
    uint32_t                xfer_count;
    uint32_t                err_count;
    uint8_t                 ready;
} aarch64_i2c_bus_t;

int  aarch64_i2c_bus_register(aarch64_i2c_bus_t *bus,
                              uint32_t bus_id,
                              const char *name,
                              const aarch64_i2c_bus_ops_t *ops,
                              void *ctx);

aarch64_i2c_bus_t *aarch64_i2c_bus_get(uint32_t bus_id);

int  aarch64_i2c_transfer(aarch64_i2c_bus_t *bus,
                          aarch64_i2c_msg_t *msgs,
                          uint32_t n);

int  aarch64_i2c_write_reg16(aarch64_i2c_bus_t *bus,
                             uint16_t addr,
                             uint16_t reg,
                             const uint8_t *data,
                             uint16_t len);

int  aarch64_i2c_read_reg16(aarch64_i2c_bus_t *bus,
                            uint16_t addr,
                            uint16_t reg,
                            uint8_t *data,
                            uint16_t len);

/* --- stub controller (QEMU / selftest) --------------------------------- */

typedef struct aarch64_i2c_stub_dev {
    uint16_t addr;
    uint16_t base_addr;  /* device-side base: reg_ptr - base_addr = index into regs[] */
    uint8_t  regs[512];
    uint16_t reg_size;   /* bytes total (must be <= 512) */
    uint16_t reg_ptr;    /* current 16-bit register pointer (raw, set by write) */
} aarch64_i2c_stub_dev_t;

typedef struct aarch64_i2c_stub_ctx {
    aarch64_i2c_stub_dev_t *devices;
    uint32_t                device_count;
} aarch64_i2c_stub_ctx_t;

int aarch64_i2c_stub_init(aarch64_i2c_bus_t *bus,
                          uint32_t bus_id,
                          aarch64_i2c_stub_ctx_t *ctx,
                          aarch64_i2c_stub_dev_t *devices,
                          uint32_t device_count);

#endif /* OPENOS_ARCH_AARCH64_I2C_BUS_H */
