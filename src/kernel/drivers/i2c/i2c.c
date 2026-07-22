/**
 * @file i2c.c
 * @brief I2C 总线核心实现
 *
 * 本文件实现 I2C 总线框架的核心功能：总线注册、设备管理、
 * 通用传输 API 等，为上层驱动提供统一的硬件抽象接口。
 *
 * @author openos
 * @date 2026
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "drivers/i2c/i2c.h"

/* M8-D.5: 使用klog替代debug.h，定义简化日志宏 */
#include "klog64.h"
#define I2C_LOG(s)      klog_emit(KLOG_INFO, KLOG_FAC_KERNEL, "[i2c] " s)
#define I2C_LOG_NN(s)   klog_emit(KLOG_INFO, KLOG_FAC_KERNEL, "[i2c] " s)

/* DEBUG宏替换：简化为klog_emit调用 */
#define DEBUG(fmt, ...)  klog_emit(KLOG_DEBUG, KLOG_FAC_KERNEL, "[i2c] debug")

/* ======================================================================
 * 全局数据
 * ====================================================================== */

#define I2C_MAX_BUSES      8

static i2c_bus_t g_i2c_buses[I2C_MAX_BUSES];
static i2c_device_t g_i2c_devices[I2C_MAX_DEVICES];
static int g_i2c_bus_count = 0;
static int g_i2c_device_count = 0;

/* ======================================================================
 * 总线管理
 * ====================================================================== */

i2c_bus_t *i2c_alloc_bus(void)
{
    int i;

    for (i = 0; i < I2C_MAX_BUSES; i++) {
        if (!g_i2c_buses[i].registered) {
            memset(&g_i2c_buses[i], 0, sizeof(i2c_bus_t));
            return &g_i2c_buses[i];
        }
    }

    DEBUG("I2C: No free bus slot available\n");
    return NULL;
}

int i2c_register_bus(i2c_bus_t *bus)
{
    if (!bus) {
        return -I2C_ERR_INVALID;
    }

    if (bus->registered) {
        DEBUG("I2C: Bus %d already registered\n", bus->bus_id);
        return -I2C_ERR_INVALID;
    }

    /* 验证必要的回调 */
    if (!bus->master_write || !bus->master_read) {
        DEBUG("I2C: Bus %d missing required callbacks\n", bus->bus_id);
        return -I2C_ERR_INVALID;
    }

    bus->registered = true;
    g_i2c_bus_count++;

    DEBUG("I2C: Bus %d registered\n", bus->bus_id);
    return 0;
}

i2c_bus_t *i2c_get_bus(int bus_id)
{
    int i;

    for (i = 0; i < I2C_MAX_BUSES; i++) {
        if (g_i2c_buses[i].registered && g_i2c_buses[i].bus_id == bus_id) {
            return &g_i2c_buses[i];
        }
    }

    return NULL;
}

int i2c_get_bus_count(void)
{
    return g_i2c_bus_count;
}

/* ======================================================================
 * 设备管理
 * ====================================================================== */

i2c_device_t *i2c_alloc_device(void)
{
    int i;

    for (i = 0; i < I2C_MAX_DEVICES; i++) {
        if (!g_i2c_devices[i].registered) {
            memset(&g_i2c_devices[i], 0, sizeof(i2c_device_t));
            return &g_i2c_devices[i];
        }
    }

    DEBUG("I2C: No free device slot available\n");
    return NULL;
}

int i2c_register_device(i2c_device_t *dev)
{
    if (!dev) {
        return -I2C_ERR_INVALID;
    }

    if (dev->registered) {
        DEBUG("I2C: Device 0x%02X already registered\n", dev->addr);
        return -I2C_ERR_INVALID;
    }

    /* 验证设备地址 */
    if (dev->addr == 0 || dev->addr > 0x7F) {
        DEBUG("I2C: Invalid device address 0x%02X\n", dev->addr);
        return -I2C_ERR_INVALID;
    }

    /* 检查总线 */
    if (!dev->bus || !dev->bus->registered) {
        DEBUG("I2C: Device 0x%02X has invalid bus\n", dev->addr);
        return -I2C_ERR_INVALID;
    }

    dev->registered = true;
    g_i2c_device_count++;

    DEBUG("I2C: Device 0x%02X registered on bus %d\n",
          dev->addr, dev->bus->bus_id);
    return 0;
}

i2c_device_t *i2c_get_device(int bus_id, uint16_t addr)
{
    int i;

    for (i = 0; i < I2C_MAX_DEVICES; i++) {
        if (g_i2c_devices[i].registered &&
            g_i2c_devices[i].bus->bus_id == bus_id &&
            g_i2c_devices[i].addr == addr) {
            return &g_i2c_devices[i];
        }
    }

    return NULL;
}

int i2c_get_device_count(void)
{
    return g_i2c_device_count;
}

/* ======================================================================
 * 通用传输 API
 * ====================================================================== */

int i2c_write(i2c_bus_t *bus, uint16_t addr, const uint8_t *buf, uint16_t len)
{
    if (!bus || !bus->registered) {
        return -I2C_ERR_UNINIT;
    }

    if (!buf || len == 0) {
        return -I2C_ERR_INVALID;
    }

    return bus->master_write(bus, addr, buf, len);
}

int i2c_read(i2c_bus_t *bus, uint16_t addr, uint8_t *buf, uint16_t len)
{
    if (!bus || !bus->registered) {
        return -I2C_ERR_UNINIT;
    }

    if (!buf || len == 0) {
        return -I2C_ERR_INVALID;
    }

    return bus->master_read(bus, addr, buf, len);
}

int i2c_write_read(i2c_bus_t *bus, uint16_t addr,
                   const uint8_t *write_buf, uint16_t write_len,
                   uint8_t *read_buf, uint16_t read_len)
{
    if (!bus || !bus->registered) {
        return -I2C_ERR_UNINIT;
    }

    if (!write_buf || !read_buf || write_len == 0 || read_len == 0) {
        return -I2C_ERR_INVALID;
    }

    /* 如果总线支持组合写读，直接使用 */
    if (bus->master_write_read) {
        return bus->master_write_read(bus, addr, write_buf, write_len,
                                      read_buf, read_len);
    }

    /* 否则使用通用实现 */
    int ret = bus->master_write(bus, addr, write_buf, write_len);
    if (ret < 0) {
        return ret;
    }

    return bus->master_read(bus, addr, read_buf, read_len);
}

/* ======================================================================
 * 便捷 API：单字节和寄存器操作
 * ====================================================================== */

int i2c_write_byte(i2c_bus_t *bus, uint16_t addr, uint8_t data)
{
    return i2c_write(bus, addr, &data, 1);
}

int i2c_read_byte(i2c_bus_t *bus, uint16_t addr, uint8_t *data)
{
    return i2c_read(bus, addr, data, 1);
}

int i2c_write_reg8(i2c_bus_t *bus, uint16_t addr, uint8_t reg, uint8_t data)
{
    uint8_t buf[2];

    buf[0] = reg;
    buf[1] = data;

    return i2c_write(bus, addr, buf, 2);
}

int i2c_read_reg8(i2c_bus_t *bus, uint16_t addr, uint8_t reg, uint8_t *data)
{
    return i2c_write_read(bus, addr, &reg, 1, data, 1);
}

int i2c_write_reg16(i2c_bus_t *bus, uint16_t addr, uint8_t reg, uint16_t data)
{
    uint8_t buf[3];

    buf[0] = reg;
    buf[1] = (data >> 8) & 0xFF;
    buf[2] = data & 0xFF;

    return i2c_write(bus, addr, buf, 3);
}

int i2c_read_reg16(i2c_bus_t *bus, uint16_t addr, uint8_t reg, uint16_t *data)
{
    uint8_t buf[2];
    int ret;

    ret = i2c_write_read(bus, addr, &reg, 1, buf, 2);
    if (ret < 0) {
        return ret;
    }

    *data = ((uint16_t)buf[0] << 8) | buf[1];
    return 0;
}

/* ======================================================================
 * 错误码转字符串
 * ====================================================================== */

const char *i2c_strerror(int err)
{
    switch (err) {
    case I2C_OK:
        return "Success";
    case -I2C_ERR_TIMEOUT:
        return "Timeout";
    case -I2C_ERR_NACK:
        return "NACK received";
    case -I2C_ERR_ARBITRATION:
        return "Arbitration lost";
    case -I2C_ERR_BUS:
        return "Bus error";
    case -I2C_ERR_INVALID:
        return "Invalid argument";
    case -I2C_ERR_UNINIT:
        return "Uninitialized";
    case -I2C_ERR_UNSUPPORTED:
        return "Unsupported operation";
    default:
        return "Unknown error";
    }
}

/* ======================================================================
 * 总线扫描
 * ====================================================================== */

int i2c_scan_bus(i2c_bus_t *bus, uint8_t *device_list, int max_devices)
{
    uint8_t dummy;
    int count = 0;
    uint16_t addr;
    int ret;

    if (!bus || !bus->registered) {
        return -I2C_ERR_UNINIT;
    }

    if (!device_list || max_devices <= 0) {
        return -I2C_ERR_INVALID;
    }

    DEBUG("I2C: Scanning bus %d...\n", bus->bus_id);

    /* 扫描标准 7-bit 地址范围 (0x08-0x77) */
    for (addr = 0x08; addr <= 0x77; addr++) {
        /* 尝试读取一个字节来检测设备 */
        ret = i2c_read_byte(bus, addr, &dummy);
        if (ret >= 0) {
            if (count < max_devices) {
                device_list[count] = (uint8_t)addr;
            }
            count++;
            DEBUG("I2C: Found device at 0x%02X\n", addr);
        }
    }

    DEBUG("I2C: Scan complete, found %d devices\n", count);
    return count;
}

/* ======================================================================
 * 版本信息
 * ====================================================================== */

const char *i2c_core_version(void)
{
    return "I2C Core Framework v1.0 (openos)";
}

/* ========================================================================
 * Default adapter accessor
 * ======================================================================== */

i2c_bus_t *i2c_get_default_adapter(void)
{
    int i;
    for (i = 0; i < I2C_MAX_BUSES; i++) {
        if (g_i2c_buses[i].registered) {
            return &g_i2c_buses[i];
        }
    }
    return NULL;
}
