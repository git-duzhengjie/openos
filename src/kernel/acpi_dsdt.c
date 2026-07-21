/*
 * acpi_dsdt.c - ACPI DSDT 解析器，用于枚举 I²C HID 设备 (PNP0C50)
 *
 * 实现功能：
 *   - 解析 ACPI 命名空间，查找 _HID = "PNP0C50" 的设备
 *   - 提取 HID Descriptor Address
 *   - 提取 I²C 总线地址和设备地址
 */
#include "include/acpi_dsdt.h"
#include "arch/x86_64/include/acpi64.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* 解析结果全局存储 */
static acpi_dsdt_result_t g_dsdt_result;
static bool g_dsdt_initialized = false;

/* ACPI 表头 */
typedef struct {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    char     creator_id[4];
    uint32_t creator_revision;
} __attribute__((packed)) acpi_table_header_t;

/* 验证 ACPI 表校验和 */
static bool acpi_validate_checksum(const void* table, uint32_t length)
{
    const uint8_t* data = (const uint8_t*)table;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++) {
        sum += data[i];
    }
    return (sum == 0);
}

/* 查找 ACPI 表 (通过签名) */
static acpi_table_header_t* acpi_find_table(const char* signature)
{
    const arch_x86_64_acpi_info_t* acpi_info = arch_x86_64_acpi_info();
    if (!acpi_info || !acpi_info->valid) {
        return NULL;
    }

    /* 使用 XSDT 或 RSDT */
    if (acpi_info->xsdt_phys) {
        /* TODO: 从 XSDT 查找 */
        return NULL;
    } else if (acpi_info->rsdt_phys) {
        /* TODO: 从 RSDT 查找 */
        return NULL;
    }

    return NULL;
}

/* 初始化 DSDT 解析器 */
int acpi_dsdt_init(void)
{
    memset(&g_dsdt_result, 0, sizeof(g_dsdt_result));
    g_dsdt_initialized = false;

    /* 验证 ACPI 已初始化 */
    const arch_x86_64_acpi_info_t* acpi_info = arch_x86_64_acpi_info();
    if (!acpi_info || !acpi_info->valid) {
        return -1;
    }

    /* TODO: 完整 DSDT 解析 */
    g_dsdt_initialized = true;
    return 0;
}

/* 获取解析结果 */
const acpi_dsdt_result_t* acpi_dsdt_get_result(void)
{
    if (!g_dsdt_initialized) {
        return NULL;
    }
    return &g_dsdt_result;
}

/* 获取 I²C HID 设备数量 */
uint32_t acpi_dsdt_get_i2c_hid_count(void)
{
    if (!g_dsdt_initialized) {
        return 0;
    }
    return g_dsdt_result.device_count;
}

/* 根据索引获取 I²C HID 设备信息 */
const acpi_i2c_hid_device_t* acpi_dsdt_get_i2c_hid_device(uint32_t index)
{
    if (!g_dsdt_initialized || index >= g_dsdt_result.device_count) {
        return NULL;
    }
    return &g_dsdt_result.devices[index];
}
