/**
 * @file i2c_selftest.c
 * @brief I2C 总线驱动自测试
 *
 * 本文件实现 I2C 总线框架的自测试，验证：
 * - 总线注册/注销
 * - 设备注册/查找
 * - 读写操作
 * - 错误处理
 *
 * @author openos
 * @date 2026
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "drivers/i2c/i2c.h"
#include "klog64.h"
#include "kernel/selftest.h"

/* M8-D.5: 使用klog替代debug.h */
#define DEBUG(fmt, ...)  klog_emit(KLOG_DEBUG, KLOG_FAC_KERNEL, "[i2c-st] debug")

/* ======================================================================
 * 模拟 I2C 总线驱动（用于自测试）
 * ====================================================================== */

#define MOCK_I2C_MAX_REGISTERS  256
#define MOCK_I2C_DEV_ADDR       0x50

typedef struct mock_i2c_bus {
    bool initialized;
    uint8_t registers[MOCK_I2C_MAX_REGISTERS];
    uint16_t current_reg;
    bool reg_addr_set;
} mock_i2c_bus_t;

static mock_i2c_bus_t g_mock_i2c;

static int mock_i2c_init(i2c_bus_t *bus)
{
    mock_i2c_bus_t *mock = &g_mock_i2c;
    
    memset(mock->registers, 0, sizeof(mock->registers));
    mock->reg_addr_set = false;
    mock->initialized = true;
    
    bus->priv = mock;
    return 0;
}

static int mock_i2c_write(i2c_bus_t *bus, uint16_t dev_addr,
                          const uint8_t *data, size_t len)
{
    mock_i2c_bus_t *mock = bus->priv;
    
    if (!mock->initialized) {
        return -I2C_ERR_NOT_INITIALIZED;
    }
    
    if (dev_addr != MOCK_I2C_DEV_ADDR) {
        return -I2C_ERR_NACK;
    }
    
    if (len < 1) {
        return -I2C_ERR_INVALID_ARG;
    }
    
    /* 第一个字节是寄存器地址 */
    mock->current_reg = data[0];
    mock->reg_addr_set = true;
    
    /* 剩余字节写入寄存器 */
    for (size_t i = 1; i < len && mock->current_reg < MOCK_I2C_MAX_REGISTERS; i++) {
        mock->registers[mock->current_reg++] = data[i];
    }
    
    return len;
}

static int mock_i2c_read(i2c_bus_t *bus, uint16_t dev_addr,
                         uint8_t *data, size_t len)
{
    mock_i2c_bus_t *mock = bus->priv;
    
    if (!mock->initialized) {
        return -I2C_ERR_NOT_INITIALIZED;
    }
    
    if (dev_addr != MOCK_I2C_DEV_ADDR) {
        return -I2C_ERR_NACK;
    }
    
    if (!mock->reg_addr_set) {
        return -I2C_ERR_INVALID_ARG;
    }
    
    /* 从当前寄存器读取 */
    for (size_t i = 0; i < len && mock->current_reg < MOCK_I2C_MAX_REGISTERS; i++) {
        data[i] = mock->registers[mock->current_reg++];
    }
    
    return len;
}

static int mock_i2c_write_read(i2c_bus_t *bus, uint16_t dev_addr,
                               const uint8_t *write_data, size_t write_len,
                               uint8_t *read_data, size_t read_len)
{
    int ret;
    
    ret = mock_i2c_write(bus, dev_addr, write_data, write_len);
    if (ret < 0) {
        return ret;
    }
    
    return mock_i2c_read(bus, dev_addr, read_data, read_len);
}

static i2c_bus_ops_t g_mock_i2c_ops = {
    .init = mock_i2c_init,
    .write = mock_i2c_write,
    .read = mock_i2c_read,
    .write_read = mock_i2c_write_read,
};

/* ======================================================================
 * 自测试用例
 * ====================================================================== */

SELFTEST_MODULE(i2c);

/**
 * @brief 测试 I2C 总线注册
 */
SELFTEST(i2c_bus_register)
{
    i2c_bus_t bus;
    int ret;
    
    memset(&bus, 0, sizeof(bus));
    bus.ops = &g_mock_i2c_ops;
    
    ret = i2c_bus_register(&bus);
    ASSERT_EQUAL(ret, 0, "I2C bus registration should succeed");
    ASSERT_NOT_EQUAL(bus.bus_id, -1, "Bus ID should be assigned");
    
    SELFTEST_PASS();
}

/**
 * @brief 测试 I2C 设备注册
 */
SELFTEST(i2c_device_register)
{
    i2c_dev_t dev;
    int ret;
    
    memset(&dev, 0, sizeof(dev));
    dev.bus_id = 0;
    dev.dev_addr = MOCK_I2C_DEV_ADDR;
    
    ret = i2c_device_register(&dev);
    ASSERT_EQUAL(ret, 0, "I2C device registration should succeed");
    
    SELFTEST_PASS();
}

/**
 * @brief 测试 I2C 设备查找
 */
SELFTEST(i2c_device_find)
{
    i2c_dev_t *dev;
    
    dev = i2c_device_find(0, MOCK_I2C_DEV_ADDR);
    ASSERT_NOT_EQUAL(dev, NULL, "Should find registered device");
    
    dev = i2c_device_find(99, MOCK_I2C_DEV_ADDR);
    ASSERT_EQUAL(dev, NULL, "Should not find device on invalid bus");
    
    dev = i2c_device_find(0, 0x7F);
    ASSERT_EQUAL(dev, NULL, "Should not find device with invalid address");
    
    SELFTEST_PASS();
}

/**
 * @brief 测试 I2C 写入操作
 */
SELFTEST(i2c_write_operation)
{
    uint8_t write_buf[4];
    int ret;
    
    write_buf[0] = 0x10;  /* 寄存器地址 */
    write_buf[1] = 0xAA;  /* 数据1 */
    write_buf[2] = 0xBB;  /* 数据2 */
    write_buf[3] = 0xCC;  /* 数据3 */
    
    ret = i2c_write(0, MOCK_I2C_DEV_ADDR, write_buf, 4);
    ASSERT_EQUAL(ret, 4, "Write should return bytes written");
    
    SELFTEST_PASS();
}

/**
 * @brief 测试 I2C 读取操作
 */
SELFTEST(i2c_read_operation)
{
    uint8_t read_buf[4];
    uint8_t reg_addr = 0x10;
    int ret;
    
    /* 先设置寄存器地址 */
    ret = i2c_write(0, MOCK_I2C_DEV_ADDR, &reg_addr, 1);
    ASSERT_EQUAL(ret, 1, "Set register address should succeed");
    
    /* 读取数据 */
    ret = i2c_read(0, MOCK_I2C_DEV_ADDR, read_buf, 3);
    ASSERT_EQUAL(ret, 3, "Read should return bytes read");
    ASSERT_EQUAL(read_buf[0], 0xAA, "First byte should match written value");
    ASSERT_EQUAL(read_buf[1], 0xBB, "Second byte should match written value");
    ASSERT_EQUAL(read_buf[2], 0xCC, "Third byte should match written value");
    
    SELFTEST_PASS();
}

/**
 * @brief 测试 I2C 组合写读操作
 */
SELFTEST(i2c_write_read_operation)
{
    uint8_t reg_addr = 0x20;
    uint8_t write_data[] = {0x20, 0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t read_buf[4];
    int ret;
    
    /* 先写入测试数据 */
    ret = i2c_write(0, MOCK_I2C_DEV_ADDR, write_data, 5);
    ASSERT_EQUAL(ret, 5, "Write test data should succeed");
    
    /* 组合写读：写地址，读数据 */
    ret = i2c_write_read(0, MOCK_I2C_DEV_ADDR, &reg_addr, 1, read_buf, 4);
    ASSERT_EQUAL(ret, 4, "Write-read should return bytes read");
    ASSERT_EQUAL(read_buf[0], 0xDE, "First byte should match");
    ASSERT_EQUAL(read_buf[1], 0xAD, "Second byte should match");
    ASSERT_EQUAL(read_buf[2], 0xBE, "Third byte should match");
    ASSERT_EQUAL(read_buf[3], 0xEF, "Fourth byte should match");
    
    SELFTEST_PASS();
}

/**
 * @brief 测试 I2C 字节读写操作
 */
SELFTEST(i2c_byte_operations)
{
    uint8_t value;
    int ret;
    
    /* 写字节 */
    ret = i2c_write_byte(0, MOCK_I2C_DEV_ADDR, 0x30, 0x55);
    ASSERT_EQUAL(ret, 0, "Write byte should succeed");
    
    /* 读字节 */
    ret = i2c_read_byte(0, MOCK_I2C_DEV_ADDR, 0x30, &value);
    ASSERT_EQUAL(ret, 0, "Read byte should succeed");
    ASSERT_EQUAL(value, 0x55, "Read value should match written value");
    
    SELFTEST_PASS();
}

/**
 * @brief 测试 I2C 16位寄存器操作
 */
SELFTEST(i2c_register_operations)
{
    uint8_t value8;
    uint16_t value16;
    int ret;
    
    /* 8位寄存器测试 */
    ret = i2c_set_reg8(0, MOCK_I2C_DEV_ADDR, 0x40, 0x12);
    ASSERT_EQUAL(ret, 0, "Set reg8 should succeed");
    
    ret = i2c_get_reg8(0, MOCK_I2C_DEV_ADDR, 0x40, &value8);
    ASSERT_EQUAL(ret, 0, "Get reg8 should succeed");
    ASSERT_EQUAL(value8, 0x12, "Reg8 value should match");
    
    /* 16位寄存器测试 */
    ret = i2c_set_reg16(0, MOCK_I2C_DEV_ADDR, 0x41, 0x3456);
    ASSERT_EQUAL(ret, 0, "Set reg16 should succeed");
    
    ret = i2c_get_reg16(0, MOCK_I2C_DEV_ADDR, 0x41, &value16);
    ASSERT_EQUAL(ret, 0, "Get reg16 should succeed");
    ASSERT_EQUAL(value16, 0x3456, "Reg16 value should match");
    
    SELFTEST_PASS();
}

/**
 * @brief 测试 I2C 位操作
 */
SELFTEST(i2c_bit_operations)
{
    uint8_t value;
    int ret;
    
    /* 设置位 */
    ret = i2c_set_reg8(0, MOCK_I2C_DEV_ADDR, 0x50, 0x00);
    ASSERT_EQUAL(ret, 0, "Initialize register should succeed");
    
    ret = i2c_set_bits(0, MOCK_I2C_DEV_ADDR, 0x50, 0x0F);
    ASSERT_EQUAL(ret, 0, "Set bits should succeed");
    
    ret = i2c_get_reg8(0, MOCK_I2C_DEV_ADDR, 0x50, &value);
    ASSERT_EQUAL(value, 0x0F, "Bits should be set");
    
    /* 清除位 */
    ret = i2c_clear_bits(0, MOCK_I2C_DEV_ADDR, 0x50, 0x03);
    ASSERT_EQUAL(ret, 0, "Clear bits should succeed");
    
    ret = i2c_get_reg8(0, MOCK_I2C_DEV_ADDR, 0x50, &value);
    ASSERT_EQUAL(value, 0x0C, "Bits should be cleared");
    
    /* 测试位 */
    bool bit_set = i2c_test_bit(0, MOCK_I2C_DEV_ADDR, 0x50, 3);
    ASSERT_EQUAL(bit_set, true, "Bit 3 should be set");
    
    bit_set = i2c_test_bit(0, MOCK_I2C_DEV_ADDR, 0x50, 1);
    ASSERT_EQUAL(bit_set, false, "Bit 1 should be cleared");
    
    SELFTEST_PASS();
}

/**
 * @brief 测试 I2C 错误处理
 */
SELFTEST(i2c_error_handling)
{
    uint8_t buf[16];
    int ret;
    
    /* 无效总线 */
    ret = i2c_write(99, MOCK_I2C_DEV_ADDR, buf, 1);
    ASSERT_EQUAL(ret, -I2C_ERR_BUS_NOT_FOUND, "Should return bus not found error");
    
    /* 无效设备地址 */
    ret = i2c_write(0, 0x7F, buf, 1);
    ASSERT_EQUAL(ret, -I2C_ERR_NACK, "Should return NACK for invalid device");
    
    /* NULL 缓冲区 */
    ret = i2c_write(0, MOCK_I2C_DEV_ADDR, NULL, 1);
    ASSERT_EQUAL(ret, -I2C_ERR_INVALID_ARG, "Should return invalid arg error");
    
    /* 零长度 */
    ret = i2c_write(0, MOCK_I2C_DEV_ADDR, buf, 0);
    ASSERT_EQUAL(ret, -I2C_ERR_INVALID_ARG, "Should return invalid arg for zero length");
    
    /* 错误码字符串测试 */
    const char *err_str = i2c_error_string(-I2C_ERR_NACK);
    ASSERT_NOT_EQUAL(strstr(err_str, "NACK"), NULL, "Error string should contain NACK");
    
    SELFTEST_PASS();
}

/**
 * @brief 运行所有 I2C 自测试
 */
int i2c_run_selftests(void)
{
    int failed = 0;
    int passed = 0;
    
    DEBUG("\\n=== Running I2C Framework Selftests ===\\n");
    
    /* 初始化模拟总线 */
    memset(&g_mock_i2c, 0, sizeof(g_mock_i2c));
    
    /* 运行测试 */
    failed += RUN_SELFTEST(i2c_bus_register);
    failed += RUN_SELFTEST(i2c_device_register);
    failed += RUN_SELFTEST(i2c_device_find);
    failed += RUN_SELFTEST(i2c_write_operation);
    failed += RUN_SELFTEST(i2c_read_operation);
    failed += RUN_SELFTEST(i2c_write_read_operation);
    failed += RUN_SELFTEST(i2c_byte_operations);
    failed += RUN_SELFTEST(i2c_register_operations);
    failed += RUN_SELFTEST(i2c_bit_operations);
    failed += RUN_SELFTEST(i2c_error_handling);
    
    passed = 10 - failed;
    
    DEBUG("=== I2C Selftests: %d passed, %d failed ===\\n\\n", passed, failed);
    
    return failed;
}
