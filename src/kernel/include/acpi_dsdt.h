/*
 * acpi_dsdt.h - ACPI DSDT 解析器，用于枚举 I²C HID 设备 (PNP0C50)
 *
 * 实现功能：
 *   - 从 XSDT/RSDT 中定位 DSDT 表
 *   - 解析 ACPI 命名空间，查找 _HID = "PNP0C50" 的设备
 *   - 提取 HID Descriptor Address (通过 _DSM 或 _CRS)
 *   - 提取 I²C 总线地址和设备地址
 */
#ifndef OPENOS_KERNEL_ACPI_DSDT_H
#define OPENOS_KERNEL_ACPI_DSDT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* I²C HID 设备信息 */
typedef struct {
    char     hid_id[8];         /* 硬件ID: "PNP0C50" */
    char     uid[16];           /* 唯一ID (可选) */
    uint16_t i2c_bus_number;    /* I²C 总线号 */
    uint16_t i2c_device_addr;   /* I²C 设备地址 (7位) */
    uint32_t hid_descriptor_address; /* HID 描述符地址 */
    uint32_t hid_descriptor_length;  /* HID 描述符长度 */
    bool     has_interrupt;      /* 是否有中断 */
    uint32_t interrupt_gsi;      /* 中断 GSI 号 */
} acpi_i2c_hid_device_t;

/* 最大支持的 I²C HID 设备数量 */
#define ACPI_MAX_I2C_HID_DEVICES  8

/* DSDT 解析结果 */
typedef struct {
    uint32_t device_count;  /* 发现的 I²C HID 设备数量 */
    acpi_i2c_hid_device_t devices[ACPI_MAX_I2C_HID_DEVICES];
} acpi_dsdt_result_t;

/* 初始化 DSDT 解析器 */
int acpi_dsdt_init(void);

/* 获取解析结果 */
const acpi_dsdt_result_t* acpi_dsdt_get_result(void);

/* 获取 I²C HID 设备数量 */
uint32_t acpi_dsdt_get_i2c_hid_count(void);

/* 根据索引获取 I²C HID 设备信息 */
const acpi_i2c_hid_device_t* acpi_dsdt_get_i2c_hid_device(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_KERNEL_ACPI_DSDT_H */
