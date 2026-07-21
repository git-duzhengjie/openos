/**
 * @file i2c.h
 * @brief I²C 总线通用驱动接口
 * 
 * 支持 Intel LPSS (Low Power Subsystem) I²C 控制器
 * 兼容 Designware I²C 控制器标准
 */

#ifndef OPENOS_KERNEL_DRIVERS_I2C_I2C_H
#define OPENOS_KERNEL_DRIVERS_I2C_I2C_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ==========================================================================
 * I²C 总线通用定义
 * ========================================================================== */

/** I²C 错误码 */
#define I2C_OK              0
#define I2C_ERR_TIMEOUT     1
#define I2C_ERR_NACK        2
#define I2C_ERR_ARBITRATION 3
#define I2C_ERR_BUS         4
#define I2C_ERR_INVALID     5
#define I2C_ERR_UNINIT      6
#define I2C_ERR_UNSUPPORTED 7

/** I²C 总线速度模式 */
typedef enum {
    I2C_SPEED_STANDARD = 100000,    /* 标准模式: 100 kHz */
    I2C_SPEED_FAST     = 400000,    /* 快速模式: 400 kHz */
    I2C_SPEED_FAST_PLUS = 1000000,  /* 快速模式+: 1 MHz */
    I2C_SPEED_HIGH     = 3400000,   /* 高速模式: 3.4 MHz */
} i2c_speed_t;

/** I²C 消息标志 */
#define I2C_M_WR          0x0000    /* 写消息 */
#define I2C_M_RD          0x0001    /* 读消息 */
#define I2C_M_TEN         0x0010    /* 10位地址 */
#define I2C_M_RECV_LEN    0x0400    /* 接收长度可变 */
#define I2C_M_NO_RD_ACK   0x0800    /* 读不发送 ACK */
#define I2C_M_IGNORE_NAK  0x1000    /* 忽略 NAK */
#define I2C_M_REV_DIR_ADDR 0x2000   /* 反转读写位 */
#define I2C_M_NOSTART     0x4000    /* 不发送 START */
#define I2C_M_STOP        0x8000    /* 消息后发送 STOP */

/** I²C 消息 */
typedef struct {
    uint16_t addr;                   /* 从设备地址 */
    uint16_t flags;                  /* 消息标志 */
    uint16_t len;                    /* 数据长度 */
    uint8_t *buf;                    /* 数据缓冲区 */
} i2c_msg_t;

/* ==========================================================================
 * Intel LPSS I²C 控制器寄存器定义
 * ========================================================================== */

/** Intel LPSS I²C 寄存器偏移 */
#define I2C_LPSS_CON             0x00    /* 控制寄存器 */
#define I2C_LPSS_TAR             0x04    /* 目标地址寄存器 */
#define I2C_LPSS_DATA_CMD        0x10    /* 数据命令寄存器 */
#define I2C_LPSS_SS_SCL_HCNT    0x14    /* 标准模式 SCL 高计数 */
#define I2C_LPSS_SS_SCL_LCNT    0x18    /* 标准模式 SCL 低计数 */
#define I2C_LPSS_FS_SCL_HCNT    0x1C    /* 快速模式 SCL 高计数 */
#define I2C_LPSS_FS_SCL_LCNT    0x20    /* 快速模式 SCL 低计数 */
#define I2C_LPSS_HS_SCL_HCNT    0x24    /* 高速模式 SCL 高计数 */
#define I2C_LPSS_HS_SCL_LCNT    0x28    /* 高速模式 SCL 低计数 */
#define I2C_LPSS_INTR_STAT      0x2C    /* 中断状态 */
#define I2C_LPSS_INTR_MASK       0x30    /* 中断屏蔽 */
#define I2C_LPSS_RAW_INTR_STAT  0x34    /* 原始中断状态 */
#define I2C_LPSS_RX_TL           0x38    /* 接收 FIFO 阈值 */
#define I2C_LPSS_TX_TL           0x3C    /* 发送 FIFO 阈值 */
#define I2C_LPSS_CLR_INTR        0x40    /* 清除中断 */
#define I2C_LPSS_CLR_RX_UNDER    0x44    /* 清除接收不足 */
#define I2C_LPSS_CLR_RX_OVER     0x48    /* 清除接收溢出 */
#define I2C_LPSS_CLR_TX_OVER     0x4C    /* 清除发送溢出 */
#define I2C_LPSS_CLR_RD_REQ      0x50    /* 清除读请求 */
#define I2C_LPSS_CLR_TX_ABRT     0x54    /* 清除发送中止 */
#define I2C_LPSS_CLR_RX_DONE     0x58    /* 清除接收完成 */
#define I2C_LPSS_CLR_ACTIVITY    0x5C    /* 清除活动 */
#define I2C_LPSS_CLR_STOP_DET    0x60    /* 清除停止检测 */
#define I2C_LPSS_CLR_START_DET   0x64    /* 清除开始检测 */
#define I2C_LPSS_CLR_GEN_CALL    0x68    /* 清除广播呼叫 */
#define I2C_LPSS_ENABLE          0x6C    /* 使能寄存器 */
#define I2C_LPSS_STATUS          0x70    /* 状态寄存器 */
#define I2C_LPSS_TXFLR           0x74    /* 发送 FIFO 等级 */
#define I2C_LPSS_RXFLR           0x78    /* 接收 FIFO 等级 */
#define I2C_LPSS_SDA_HOLD        0x7C    /* SDA 保持时间 */
#define I2C_LPSS_TX_ABRT_SOURCE  0x80    /* 发送中止源 */
#define I2C_LPSS_SLV_DATA_NACK_ONLY 0x84 /* 仅从数据 NAK */
#define I2C_LPSS_DMA_CR          0x88    /* DMA 控制 */
#define I2C_LPSS_DMA_TDLR        0x8C    /* DMA 发送等级 */
#define I2C_LPSS_DMA_RDLR        0x90    /*  DMA 接收等级 */
#define I2C_LPSS_SDA_SETUP       0x94    /* SDA 建立时间 */
#define I2C_LPSS_ACK_GENERAL_CALL 0x98   /* ACK 广播呼叫 */
#define I2C_LPSS_ENABLE_STATUS   0x9C    /* 使能状态 */
#define I2C_LPSS_FS_SPKLEN       0xA0    /* 快速模式尖峰长度 */
#define I2C_LPSS_HS_SPKLEN       0xA4    /* 高速模式尖峰长度 */
#define I2C_LPSS_CLR_RESTART_DET 0xA8    /* 清除重启检测 */
#define I2C_LPSS_COMP_PARAM_1    0xF4    /* 组件参数 1 */
#define I2C_LPSS_COMP_VERSION    0xF8    /* 组件版本 */
#define I2C_LPSS_COMP_TYPE       0xFC    /* 组件类型 */

/** CON 控制寄存器位 */
#define I2C_CON_MASTER_MODE      (1 << 0)    /* 主模式 */
#define I2C_CON_SPEED_STD        (1 << 1)    /* 标准速度 */
#define I2C_CON_SPEED_FAST       (2 << 1)    /* 快速速度 */
#define I2C_CON_SPEED_HIGH       (3 << 1)    /* 高速速度 */
#define I2C_CON_10BITADDR_SLAVE  (1 << 3)    /* 10位从地址 */
#define I2C_CON_10BITADDR_MASTER (1 << 4)    /* 10位主地址 */
#define I2C_CON_RESTART_EN       (1 << 5)    /* 重启使能 */
#define I2C_CON_SLAVE_DISABLE    (1 << 6)    /* 从模式禁用 */
#define I2C_CON_STOP_DET_IFADDRESSED (1 << 7) /* 仅寻址时停止检测 */
#define I2C_CON_TX_EMPTY_CTRL    (1 << 8)    /* 发送空控制 */

/** DATA_CMD 寄存器位 */
#define I2C_DATA_CMD_READ        (1 << 8)    /* 读命令 */
#define I2C_DATA_CMD_WRITE       (0 << 8)    /* 写命令 */
#define I2C_DATA_CMD_STOP        (1 << 9)    /* 发送 STOP */
#define I2C_DATA_CMD_RESTART     (1 << 10)   /* 发送 RESTART */
#define I2C_DATA_CMD_FIRST_DATA_BYTE (1 << 11) /* 首数据字节 */

/** ENABLE 寄存器位 */
#define I2C_ENABLE_CTRL          (1 << 0)    /* 控制器使能 */
#define I2C_ENABLE_ABORT         (1 << 1)    /* 中止传输 */

/** STATUS 寄存器位 */
#define I2C_STATUS_ACTIVITY      (1 << 0)    /* 活动状态 */
#define I2C_STATUS_TFNF          (1 << 1)    /* 发送 FIFO 非满 */
#define I2C_STATUS_TFE           (1 << 2)    /* 发送 FIFO 空 */
#define I2C_STATUS_RFNE          (1 << 3)    /* 接收 FIFO 非空 */
#define I2C_STATUS_RFF           (1 << 4)    /* 接收 FIFO 满 */
#define I2C_STATUS_MST_ACTIVITY  (1 << 5)    /* 主活动 */
#define I2C_STATUS_SLV_ACTIVITY  (1 << 6)    /* 从活动 */

/** 中断位 */
#define I2C_INTR_RX_UNDER        (1 << 0)    /* 接收不足 */
#define I2C_INTR_RX_OVER         (1 << 1)    /* 接收溢出 */
#define I2C_INTR_RX_FULL         (1 << 2)    /* 接收满 */
#define I2C_INTR_TX_OVER         (1 << 3)    /* 发送溢出 */
#define I2C_INTR_TX_EMPTY        (1 << 4)    /* 发送空 */
#define I2C_INTR_RD_REQ          (1 << 5)    /* 读请求 */
#define I2C_INTR_TX_ABRT         (1 << 6)    /* 发送中止 */
#define I2C_INTR_RX_DONE         (1 << 7)    /* 接收完成 */
#define I2C_INTR_ACTIVITY        (1 << 8)    /* 活动 */
#define I2C_INTR_STOP_DET        (1 << 9)    /* 停止检测 */
#define I2C_INTR_START_DET       (1 << 10)   /* 开始检测 */
#define I2C_INTR_GEN_CALL        (1 << 11)   /* 广播呼叫 */
#define I2C_INTR_RESTART_DET     (1 << 12)   /* 重启检测 */

/* ==========================================================================
 * I²C 适配器定义
 * ========================================================================== */

/** I²C 适配器操作 */
typedef struct i2c_adapter_ops i2c_adapter_ops_t;

/** I²C 适配器 */
typedef struct i2c_adapter {
    const char *name;                    /* 适配器名称 */
    uintptr_t base;                      /* 寄存器基地址 */
    uint32_t irq;                        /* 中断号 */
    i2c_speed_t speed;                   /* 总线速度 */
    bool initialized;                    /* 是否已初始化 */
    const i2c_adapter_ops_t *ops;        /* 操作函数表 */
    void *priv;                          /* 私有数据 */
} i2c_adapter_t;

/** I²C 适配器操作函数表 */
struct i2c_adapter_ops {
    /** 初始化适配器 */
    int (*init)(i2c_adapter_t *adap);
    
    /** 发送 I²C 消息 */
    int (*xfer)(i2c_adapter_t *adap, i2c_msg_t *msgs, int num);
    
    /** 设置总线速度 */
    int (*set_speed)(i2c_adapter_t *adap, i2c_speed_t speed);
    
    /** 探测从设备 */
    bool (*probe)(i2c_adapter_t *adap, uint16_t addr);
};

/* ==========================================================================
 * I²C 总线管理（总线-外设分层架构）
 * ========================================================================== */

/** I²C 总线最大数量 */
#ifndef I2C_MAX_BUSES
#define I2C_MAX_BUSES   8
#endif

/** I²C 总线结构体 */
typedef struct i2c_bus {
    int bus_id;                          /* 总线 ID */
    i2c_adapter_t *adapter;              /* 绑定的适配器 */
    int initialized;                     /* 是否已初始化 */
    int registered;                      /* 是否已注册 */
    /* 直接回调函数（简化模式，可选） */
    int (*master_write)(struct i2c_bus *bus, uint16_t addr, const uint8_t *buf, uint16_t len);
    int (*master_read)(struct i2c_bus *bus, uint16_t addr, uint8_t *buf, uint16_t len);
    int (*master_write_read)(struct i2c_bus *bus, uint16_t addr,
                             const uint8_t *wbuf, uint16_t wlen,
                             uint8_t *rbuf, uint16_t rlen);
    void *priv;                          /* 控制器私有数据 */
} i2c_bus_t;

/* 总线管理 API */
i2c_bus_t *i2c_alloc_bus(void);
int i2c_register_bus(i2c_bus_t *bus);
i2c_bus_t *i2c_get_bus(int bus_id);

/** I²C 设备最大数量 */
#ifndef I2C_MAX_DEVICES
#define I2C_MAX_DEVICES  16
#endif

/** I²C 设备结构体 */
typedef struct i2c_device {
    int bus_id;                          /* 所在总线 ID */
    uint16_t addr;                       /* 设备地址 */
    const char *name;                    /* 设备名称 */
    int used;                            /* 是否已使用 */
    int registered;                      /* 是否已注册 */
    i2c_bus_t *bus;                      /* 指向所在总线 */
} i2c_device_t;

/* 设备管理 API */
i2c_device_t *i2c_alloc_device(void);
int i2c_register_device(i2c_device_t *dev);
i2c_device_t *i2c_get_device(int bus_id, uint16_t addr);

/* 标准化传输 API */
int i2c_write(i2c_bus_t *bus, uint16_t addr, const uint8_t *buf, uint16_t len);
int i2c_read(i2c_bus_t *bus, uint16_t addr, uint8_t *buf, uint16_t len);
int i2c_write_read(i2c_bus_t *bus, uint16_t addr,
                   const uint8_t *wbuf, uint16_t wlen,
                   uint8_t *rbuf, uint16_t rlen);
int i2c_write_byte(i2c_bus_t *bus, uint16_t addr, uint8_t data);
int i2c_read_byte(i2c_bus_t *bus, uint16_t addr, uint8_t *data);
int i2c_write_reg8(i2c_bus_t *bus, uint16_t addr, uint8_t reg, uint8_t data);
int i2c_read_reg8(i2c_bus_t *bus, uint16_t addr, uint8_t reg, uint8_t *data);
int i2c_write_reg16(i2c_bus_t *bus, uint16_t addr, uint8_t reg, uint16_t data);
int i2c_read_reg16(i2c_bus_t *bus, uint16_t addr, uint8_t reg, uint16_t *data);

/* 辅助 API */
const char *i2c_strerror(int err);
int i2c_scan_bus(i2c_bus_t *bus, uint8_t *device_list, int max_devices);

/* ==========================================================================
 * 全局 API
 * ========================================================================== */

/**
 * @brief 初始化 Intel LPSS I²C 控制器
 * 
 * @param adap 适配器结构体
 * @param base 寄存器基地址
 * @param speed 总线速度
 * @return int 0 成功，<0 错误
 */
int i2c_lpss_init(i2c_adapter_t *adap, uintptr_t base, i2c_speed_t speed);

/**
 * @brief I²C 总线传输
 * 
 * @param adap 适配器
 * @param msgs 消息数组
 * @param num 消息数量
 * @return int 成功传输的消息数，<0 错误
 */
int i2c_transfer(i2c_adapter_t *adap, i2c_msg_t *msgs, int num);

/**
 * @brief 简化的 I²C 读操作
 * 
 * @param adap 适配器
 * @param addr 从设备地址
 * @param reg 寄存器地址
 * @param buf 数据缓冲区
 * @param len 数据长度
 * @return int 0 成功，<0 错误
 */
int i2c_smbus_read_byte_data(i2c_adapter_t *adap, uint16_t addr, uint8_t reg, uint8_t *buf);

/**
 * @brief 简化的 I²C 写操作
 * 
 * @param adap 适配器
 * @param addr 从设备地址
 * @param reg 寄存器地址
 * @param value 要写入的值
 * @return int 0 成功，<0 错误
 */
int i2c_smbus_write_byte_data(i2c_adapter_t *adap, uint16_t addr, uint8_t reg, uint8_t value);

/**
 * @brief I²C 块读操作
 * 
 * @param adap 适配器
 * @param addr 从设备地址
 * @param reg 寄存器地址
 * @param buf 数据缓冲区
 * @param len 数据长度
 * @return int 0 成功，<0 错误
 */
int i2c_smbus_read_i2c_block_data(i2c_adapter_t *adap, uint16_t addr, 
                                   uint8_t reg, uint8_t *buf, uint8_t len);

/**
 * @brief I²C 块写操作
 * 
 * @param adap 适配器
 * @param addr 从设备地址
 * @param reg 寄存器地址
 * @param buf 数据缓冲区
 * @param len 数据长度
 * @return int 0 成功，<0 错误
 */
int i2c_smbus_write_i2c_block_data(i2c_adapter_t *adap, uint16_t addr, 
                                    uint8_t reg, const uint8_t *buf, uint8_t len);

/**
 * @brief 探测 I²C 从设备是否存在
 * 
 * @param adap 适配器
 * @param addr 从设备地址
 * @return 1 设备存在，0 不存在
 */
int i2c_probe_device(i2c_adapter_t *adap, uint16_t addr);

/**
 * @brief 扫描 I²C 总线
 * 
 * @param adap 适配器
 * @param found_addrs 输出找到的设备地址数组
 * @param max_addrs 数组最大容量
 * @return int 找到的设备数量
 */
/* 旧定义已废弃，使用 i2c_scan_bus(i2c_bus_t*, uint8_t*, int) 统一接口 */

/* ==========================================================================
 * 总线管理
 * ========================================================================== */

/** 获取默认 I²C 适配器 */
i2c_adapter_t *i2c_get_default_adapter(void);

/** 检查 I²C 总线是否就绪 */
int i2c_bus_ready(void);

/** 设置默认适配器（内部接口） */
void i2c_set_default_adapter(i2c_adapter_t *adapter);

/**
 * @brief Intel LPSS I²C 驱动全局初始化 (PCI 探测 + 适配器初始化)
 * 
 * 调用链路: kernel64.c -> i2c_lpss_init_all() -> PCI 探测 -> 适配器初始化
 * 同时兼容 QEMU 模拟环境（无真实硬件时返回成功）
 */
void i2c_lpss_init_all(void);

/**
 * @brief I²C 核心 selftest
 * 
 * 测试 I²C 读写、总线扫描、超时处理等基础功能
 * 
 * @return int 0 通过，<0 失败
 */
int i2c_core_selftest(void);

#endif /* OPENOS_KERNEL_DRIVERS_I2C_I2C_H */
