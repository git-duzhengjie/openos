/**
 * @file i2c_hid.h
 * @brief HID over I²C v1.0 驱动头文件
 */

#ifndef OPENOS_KERNEL_DRIVERS_I2C_HID_I2C_HID_H
#define OPENOS_KERNEL_DRIVERS_I2C_HID_I2C_HID_H

#include <stdint.h>
#include "../../../kernel/include/types.h"
#include <stddef.h>

#include "../i2c/i2c.h"

/* ==========================================================================
 * HID over I²C 寄存器定义
 * ========================================================================== */

#define I2C_HID_REG_HID_DESC         0x0001
#define I2C_HID_REG_REPORT_DESC      0x0002
#define I2C_HID_REG_INPUT            0x0003
#define I2C_HID_REG_OUTPUT           0x0004
#define I2C_HID_REG_COMMAND          0x0005
#define I2C_HID_REG_DATA             0x0006

/* 命令 */
#define I2C_HID_RESET                0x0001
#define I2C_HID_GET_REPORT           0x0002
#define I2C_HID_SET_REPORT           0x0003
#define I2C_HID_SET_IDLE             0x0004
#define I2C_HID_SET_PROTOCOL         0x0005

/* 电源状态 */
#define I2C_HID_POWER_ON             0x0000
#define I2C_HID_POWER_SLEEP          0x0001
#define I2C_HID_POWER_OFF            0x0002

/* HID 描述符长度 */
#define I2C_HID_DESC_LENGTH          21

/* ==========================================================================
 * HID 描述符结构
 * ========================================================================== */

typedef struct {
    uint16_t wDescLength;
    uint16_t bcdVersion;
    uint16_t wReportDescLength;
    uint16_t wReportDescRegister;
    uint16_t wInputRegister;
    uint16_t wMaxInputLength;
    uint16_t wOutputRegister;
    uint16_t wMaxOutputLength;
    uint16_t wControlRegister;
    uint16_t wVendorId;
    uint16_t wProductId;
    uint16_t wVersionId;
    uint8_t  bReserved;
} __attribute__((packed)) i2c_hid_descriptor_t;

/* ==========================================================================
 * I²C HID 设备
 * ========================================================================== */

typedef struct i2c_adapter i2c_adapter_t;

typedef struct {
    i2c_adapter_t *i2c_adap;          /* I²C 适配器 */
    uint16_t i2c_addr;                 /* I²C 从设备地址 */
    
    i2c_hid_descriptor_t hid_desc;     /* HID 描述符 */
    uint8_t *report_desc;              /* 报告描述符 */
    uint16_t report_desc_len;          /* 报告描述符长度 */
    uint8_t report_desc_buf[512];      /* 报告描述符缓冲区 */
    
    uint8_t input_buf[512];            /* 输入报告缓冲区 */
    
    /* I2C 总线连接 */
    int bus_id;                        /* I2C 总线 ID */
    uint16_t dev_addr;                 /* I2C 设备地址 */
    
    /* 触摸设备参数 */
    uint8_t max_contacts;              /* 最大触点数 */
    uint16_t max_x;                    /* 最大 X 坐标 (物理) */
    uint16_t max_y;                    /* 最大 Y 坐标 (物理) */
    uint16_t width_px;                 /* 屏幕宽度 (像素) */
    uint16_t height_px;                /* 屏幕高度 (像素) */
    
    /* input 子系统集成 */
    int input_dev_id;                  /* input 设备 ID (<0 = 未注册) */
    
    /* 中断支持 */
    int irq_vector;                    /* 中断向量号 (<0 = 未使用中断) */
    bool irq_enabled;                  /* 中断是否已启用 */
    bool use_interrupt;                /* 是否使用中断模式 (false=轮询) */
    
    bool initialized;                  /* 是否已初始化 */
} i2c_hid_device_t;

/* ==========================================================================
 * API
 * ========================================================================== */

/**
 * @brief 初始化 I²C HID 设备（通过通用 I2C 总线）
 * 
 * @param bus_id I2C 总线 ID
 * @param dev_addr I2C 从设备地址
 * @return int 0 成功，<0 错误
 */
int i2c_hid_init(int bus_id, uint16_t dev_addr);

/**
 * @brief 轮询读取输入报告
 * 
 * @param dev I²C HID 设备
 * @return int 0 成功，<0 错误
 */
int i2c_hid_poll(i2c_hid_device_t *dev);

/**
 * @brief 中断处理函数 (从 IRQ handler 调用)
 *
 * 在中断模式下，当设备触发中断时调用此函数。
 * 读取输入报告并上报到 input 子系统。
 *
 * @param dev I²C HID 设备
 * @return int 0 成功，<0 错误
 */
int i2c_hid_irq_handler(i2c_hid_device_t *dev);

/**
 * @brief 启用中断模式
 *
 * 注册 IRQ handler，切换从中轮询模式到中断模式。
 * 需要 GPIO 或 ACPI GPE 中断已配置。
 *
 * @param dev I²C HID 设备
 * @param irq_vector 中断向量号
 * @return int 0 成功，<0 错误
 */
int i2c_hid_enable_interrupt(i2c_hid_device_t *dev, int irq_vector);

/**
 * @brief 禁用中断模式，回退到轮询
 *
 * @param dev I²C HID 设备
 * @return int 0 成功，<0 错误
 */
int i2c_hid_disable_interrupt(i2c_hid_device_t *dev);

/**
 * @brief 检查 I²C HID 设备是否存在 (ACPI PNP0C50)
 * 
 * @return true 设备存在
 * @return false 设备不存在
 */
bool i2c_hid_present(void);

/**
 * @brief 获取全局 I²C HID 设备实例
 * 
 * @return i2c_hid_device_t* 设备指针
 */
i2c_hid_device_t *i2c_hid_get_device(void);

/**
 * @brief I²C HID 触屏驱动全局初始化
 * 
 * 初始化 I²C HID 设备，解析 HID 报告描述符，
 * 注册到 input_core 输入子系统
 * 
 * @return int 0 成功，<0 错误
 */
int i2c_hid_global_init(void);

/**
 * @brief I²C HID 驱动自检
 * 
 * 执行 HID 描述符读取、报告解析、多点触控事件注入等测试
 * 
 * @return int 0 全部通过，<0 失败
 */
int i2c_hid_selftest(void);

/* ==========================================================================
 * ACPI 设备枚举接口
 * ========================================================================== */

/**
 * @brief 枚举 ACPI DSDT 中所有 PNP0C50 设备
 * 
 * 解析 ACPI 命名空间，查找 HID over I²C 兼容设备（PNP0C50），
 * 提取 I²C 总线地址、设备地址和 HID 描述符地址
 * 
 * @return int 发现的设备数量，<0 错误
 */
int i2c_hid_enumerate_acpi(void);

/**
 * @brief 通过 ACPI 索引初始化 I²C HID 设备
 * 
 * @param acpi_index ACPI 枚举中的设备索引（从 0 开始）
 * @return int 0 成功，<0 错误
 */
int i2c_hid_init_from_acpi(uint32_t acpi_index);

/**
 * @brief 获取 ACPI 枚举的设备总数
 * 
 * @return uint32_t 设备总数
 */
uint32_t i2c_hid_get_acpi_device_count(void);

#endif /* OPENOS_KERNEL_DRIVERS_I2C_HID_I2C_HID_H */
