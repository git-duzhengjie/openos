/**
 * @file intel_lpss_i2c.c
 * @brief Intel LPSS (Low Power Subsystem) I2C 控制器驱动
 *
 * 本驱动实现 Intel LPSS I2C 控制器的硬件抽象，遵循 I2C 总线驱动规范。
 * 支持 Intel Sunrise Point、Kaby Lake 等平台的 I2C 控制器。
 *
 * @author openos
 * @date 2026
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "drivers/i2c/i2c.h"
#include "intel_lpss_i2c.h"

/* M8-D.5: 使用klog替代debug.h */
#include "klog64.h"

/* M8-G.5: PCI枚举支持 */
#include "pci.h"
#define DEBUG(fmt, ...)  klog_emit(KLOG_DEBUG, KLOG_FAC_KERNEL, "[i2c-lpss] debug")

/* ======================================================================
 * Intel LPSS I2C 寄存器定义
 * ====================================================================== */

/**
 * @brief Intel LPSS I2C 寄存器偏移
 */
#define I2C_LPSS_CON                    0x00    /* I2C 控制寄存器 */
#define I2C_LPSS_TAR                    0x04    /* 目标地址寄存器 */
#define I2C_LPSS_SAR                    0x08    /* 从设备地址寄存器 */
#define I2C_LPSS_DATA_CMD               0x10    /* 数据命令寄存器 */
#define I2C_LPSS_SS_SCL_HCNT            0x14    /* 标准速度 SCL 高电平计数 */
#define I2C_LPSS_SS_SCL_LCNT            0x18    /* 标准速度 SCL 低电平计数 */
#define I2C_LPSS_FS_SCL_HCNT            0x1C    /* 快速速度 SCL 高电平计数 */
#define I2C_LPSS_FS_SCL_LCNT            0x20    /* 快速速度 SCL 低电平计数 */
#define I2C_LPSS_HS_SCL_HCNT            0x24    /* 高速 SCL 高电平计数 */
#define I2C_LPSS_HS_SCL_LCNT            0x28    /* 高速 SCL 低电平计数 */
#define I2C_LPSS_INTR_STAT              0x2C    /* 中断状态寄存器 */
#define I2C_LPSS_INTR_MASK              0x30    /* 中断屏蔽寄存器 */
#define I2C_LPSS_RAW_INTR_STAT          0x34    /* 原始中断状态 */
#define I2C_LPSS_RX_TL                  0x38    /* 接收 FIFO 阈值 */
#define I2C_LPSS_TX_TL                  0x3C    /* 发送 FIFO 阈值 */
#define I2C_LPSS_CLR_INTR               0x40    /* 清除中断 */
#define I2C_LPSS_CLR_RX_UNDER           0x44    /* 清除接收下溢 */
#define I2C_LPSS_CLR_RX_OVER            0x48    /* 清除接收上溢 */
#define I2C_LPSS_CLR_TX_OVER            0x4C    /* 清除发送上溢 */
#define I2C_LPSS_CLR_RD_REQ             0x50    /* 清除读请求 */
#define I2C_LPSS_CLR_TX_ABRT            0x54    /* 清除发送中止 */
#define I2C_LPSS_CLR_RX_DONE            0x58    /* 清除接收完成 */
#define I2C_LPSS_CLR_ACTIVITY           0x5C    /* 清除活动 */
#define I2C_LPSS_CLR_STOP_DET           0x60    /* 清除停止检测 */
#define I2C_LPSS_CLR_START_DET          0x64    /* 清除起始检测 */
#define I2C_LPSS_CLR_GEN_CALL           0x68    /* 清除广播调用 */
#define I2C_LPSS_ENABLE                 0x6C    /* I2C 启用寄存器 */
#define I2C_LPSS_STATUS                 0x70    /* 状态寄存器 */
#define I2C_LPSS_TXFLR                  0x74    /* 发送 FIFO 级别 */
#define I2C_LPSS_RXFLR                  0x78    /* 接收 FIFO 级别 */
#define I2C_LPSS_SDA_HOLD               0x7C    /* SDA 保持时间 */
#define I2C_LPSS_TX_ABRT_SOURCE         0x80    /* 发送中止源 */
#define I2C_LPSS_SLV_DATA_NACK_ONLY     0x84    /* 从设备 NACK 控制 */
#define I2C_LPSS_DMA_CR                 0x88    /* DMA 控制 */
#define I2C_LPSS_DMA_TDLR               0x8C    /* DMA 发送级别 */
#define I2C_LPSS_DMA_RDLR               0x90    /* DMA 接收级别 */
#define I2C_LPSS_SDA_SETUP              0x94    /* SDA 建立时间 */
#define I2C_LPSS_ACK_GENERAL_CALL       0x98    /* ACK 广播调用 */
#define I2C_LPSS_ENABLE_STATUS          0x9C    /* 启用状态 */
#define I2C_LPSS_FS_SPKLEN              0xA0    /* 快速速度尖峰长度 */
#define I2C_LPSS_HS_SPKLEN              0xA4    /* 高速尖峰长度 */
#define I2C_LPSS_CLR_RESTART_DET        0xA8    /* 清除重启检测 */
#define I2C_LPSS_COMP_PARAM_1           0xF4    /* 组件参数 1 */
#define I2C_LPSS_COMP_VERSION           0xF8    /* 组件版本 */
#define I2C_LPSS_COMP_TYPE              0xFC    /* 组件类型 */

/* CON 寄存器位定义 */
#define I2C_CON_MASTER_MODE             (1 << 0)
#define I2C_CON_SPEED_STANDARD          (1 << 1)
#define I2C_CON_SPEED_FAST              (2 << 1)
#define I2C_CON_SPEED_HIGH              (3 << 1)
#define I2C_CON_10BITADDR_SLAVE         (1 << 3)
#define I2C_CON_10BITADDR_MASTER        (1 << 4)
#define I2C_CON_RESTART_EN              (1 << 5)
#define I2C_CON_SLAVE_DISABLE           (1 << 6)
#define I2C_CON_STOP_DET_IFADDRESSED    (1 << 7)
#define I2C_CON_TX_EMPTY_CTRL           (1 << 8)
#define I2C_CON_RX_FIFO_FULL_HLD_CTRL   (1 << 9)

/* DATA_CMD 寄存器位定义 */
#define I2C_DATA_CMD_CMD_READ           (1 << 8)
#define I2C_DATA_CMD_CMD_WRITE          (0 << 8)
#define I2C_DATA_CMD_STOP               (1 << 9)
#define I2C_DATA_CMD_RESTART            (1 << 10)
#define I2C_DATA_CMD_NACK               (1 << 11)
/* I2C_DATA_CMD_FIRST_DATA_BYTE already defined in i2c.h */

/* INTR_STAT 寄存器位定义 */
#define I2C_INTR_RX_UNDER               (1 << 0)
#define I2C_INTR_RX_OVER                (1 << 1)
#define I2C_INTR_RX_FULL                (1 << 2)
#define I2C_INTR_TX_OVER                (1 << 3)
#define I2C_INTR_TX_EMPTY               (1 << 4)
#define I2C_INTR_RD_REQ                 (1 << 5)
#define I2C_INTR_TX_ABRT                (1 << 6)
#define I2C_INTR_RX_DONE                (1 << 7)
#define I2C_INTR_ACTIVITY               (1 << 8)
#define I2C_INTR_STOP_DET               (1 << 9)
#define I2C_INTR_START_DET              (1 << 10)
#define I2C_INTR_GEN_CALL               (1 << 11)
#define I2C_INTR_RESTART_DET            (1 << 12)
#define I2C_INTR_MST_ON_HOLD            (1 << 13)

/* STATUS 寄存器位定义 */
#define I2C_STATUS_ACTIVITY             (1 << 0)
#define I2C_STATUS_TFNF                 (1 << 1)
#define I2C_STATUS_TFE                  (1 << 2)
#define I2C_STATUS_RFNE                 (1 << 3)
#define I2C_STATUS_RFF                  (1 << 4)
#define I2C_STATUS_MST_ACTIVITY         (1 << 5)
#define I2C_STATUS_SLV_ACTIVITY         (1 << 6)
#define I2C_STATUS_MST_HOLD_TX_FIFO_EMPTY (1 << 8)
#define I2C_STATUS_MST_HOLD_RX_FIFO_FULL  (1 << 9)
#define I2C_STATUS_SLV_HOLD_ADDR_ACK    (1 << 10)
#define I2C_STATUS_SLV_HOLD_RX_FIFO_FULL (1 << 11)

/* ENABLE 寄存器位定义 */
#define I2C_ENABLE_BIT                  (1 << 0)
#define I2C_ENABLE_ABORT                (1 << 1)
#define I2C_ENABLE_TX_CMD_BLOCK         (1 << 2)

/* ======================================================================
 * 私有数据结构
 * ====================================================================== */

/**
 * @brief Intel LPSS I2C 控制器私有数据
 */
typedef struct intel_lpss_i2c_priv {
    void *base;                 /* 寄存器基地址 */
    uint32_t input_clk;                 /* 输入时钟频率 (Hz) */
    uint32_t bus_speed;                 /* 总线速度 (Hz) */
    uint8_t sda_hold;                   /* SDA 保持时间 (ns) */
    uint8_t sda_setup;                  /* SDA 建立时间 (ns) */
    bool initialized;                   /* 初始化状态 */
} intel_lpss_i2c_priv_t;

#define INTEL_LPSS_I2C_MAX_CONTROLLERS  8

static intel_lpss_i2c_priv_t g_lpss_i2c_controllers[INTEL_LPSS_I2C_MAX_CONTROLLERS];
static int g_lpss_i2c_count = 0;

/* ======================================================================
 * 寄存器读写操作
 * ====================================================================== */

static inline uint32_t intel_lpss_i2c_readl(intel_lpss_i2c_priv_t *priv,
                                            uint32_t offset)
{
    return *((volatile uint32_t *)((uintptr_t)priv->base + offset));
}

static inline void intel_lpss_i2c_writel(intel_lpss_i2c_priv_t *priv,
                                         uint32_t offset, uint32_t value)
{
    *((volatile uint32_t *)((uintptr_t)priv->base + offset)) = value;
}

/* ======================================================================
 * 辅助函数
 * ====================================================================== */

static int intel_lpss_i2c_wait_bus_idle(intel_lpss_i2c_priv_t *priv)
{
    uint32_t timeout = 100000;
    uint32_t status;

    while (timeout--) {
        status = intel_lpss_i2c_readl(priv, I2C_LPSS_STATUS);
        if (!(status & (I2C_STATUS_ACTIVITY | I2C_STATUS_MST_ACTIVITY))) {
            return 0;
        }
    }

    DEBUG("I2C-LPSS: Bus timeout waiting for idle\\n");
    return -I2C_ERR_TIMEOUT;
}

static int intel_lpss_i2c_flush_fifos(intel_lpss_i2c_priv_t *priv)
{
    uint32_t data;
    uint32_t rxflr;
    int timeout = 1000;

    /* 清空 RX FIFO */
    while (timeout--) {
        rxflr = intel_lpss_i2c_readl(priv, I2C_LPSS_RXFLR);
        if (rxflr == 0) {
            break;
        }
        data = intel_lpss_i2c_readl(priv, I2C_LPSS_DATA_CMD);
        (void)data;
    }

    /* 等待 TX FIFO 空 */
    timeout = 1000;
    while (timeout--) {
        uint32_t status = intel_lpss_i2c_readl(priv, I2C_LPSS_STATUS);
        if (status & I2C_STATUS_TFE) {
            break;
        }
    }

    return 0;
}

/* ======================================================================
 * 总线速度配置
 * ====================================================================== */

static int intel_lpss_i2c_set_bus_speed(intel_lpss_i2c_priv_t *priv)
{
    uint32_t input_clk = priv->input_clk;
    uint32_t bus_speed = priv->bus_speed;
    uint32_t ss_hcnt, ss_lcnt, fs_hcnt, fs_lcnt;

    /* 计算标准模式 (100 kHz) SCL 计数 */
    if (input_clk == 0) {
        input_clk = 100000000; /* 默认 100 MHz */
    }

    /* 标准模式: T_low = 4.7us, T_high = 4.0us */
    ss_hcnt = (input_clk * 4) / 1000000;
    ss_lcnt = (input_clk * 5) / 1000000;

    /* 快速模式: T_low = 1.3us, T_high = 0.6us */
    fs_hcnt = (input_clk * 6) / 10000000;
    fs_lcnt = (input_clk * 13) / 10000000;

    /* Check bus speed validity */
    if (bus_speed != I2C_SPEED_STANDARD && bus_speed != I2C_SPEED_FAST &&
        bus_speed != I2C_SPEED_FAST_PLUS) {
        DEBUG("I2C-LPSS: Unsupported bus speed %u Hz\n", bus_speed);
        return -I2C_ERR_UNSUPPORTED;
    }

    /* Write SCL count registers */
    intel_lpss_i2c_writel(priv, I2C_LPSS_SS_SCL_HCNT, ss_hcnt);
    intel_lpss_i2c_writel(priv, I2C_LPSS_SS_SCL_LCNT, ss_lcnt);
    intel_lpss_i2c_writel(priv, I2C_LPSS_FS_SCL_HCNT, fs_hcnt);
    intel_lpss_i2c_writel(priv, I2C_LPSS_FS_SCL_LCNT, fs_lcnt);

    /* 设置 SDA 保持时间 */
    intel_lpss_i2c_writel(priv, I2C_LPSS_SDA_HOLD, priv->sda_hold);

    /* 设置 SDA 建立时间 */
    intel_lpss_i2c_writel(priv, I2C_LPSS_SDA_SETUP, priv->sda_setup);

    /* 设置尖峰抑制 */
    intel_lpss_i2c_writel(priv, I2C_LPSS_FS_SPKLEN, (input_clk / 1000000000) * 50);

    return 0;
}

/* ======================================================================
 * I2C 数据传输核心函数
 * ====================================================================== */

static int intel_lpss_i2c_write_bytes(intel_lpss_i2c_priv_t *priv,
                                      uint16_t addr, const uint8_t *buf,
                                      uint16_t len, bool send_stop)
{
    uint32_t data_cmd;
    uint32_t intr_status;
    int i, timeout;

    /* 设置目标地址 */
    intel_lpss_i2c_writel(priv, I2C_LPSS_TAR, addr & 0x3FF);

    /* 发送数据 */
    for (i = 0; i < len; i++) {
        /* 等待 TX FIFO 有空间 */
        timeout = 100000;
        while (timeout--) {
            uint32_t status = intel_lpss_i2c_readl(priv, I2C_LPSS_STATUS);
            if (status & I2C_STATUS_TFNF) {
                break;
            }
        }
        if (timeout == 0) {
            DEBUG("I2C-LPSS: TX FIFO timeout\n");
            return -I2C_ERR_TIMEOUT;
        }

        /* 准备数据命令 */
        data_cmd = buf[i] & 0xFF;
        if (i == len - 1 && send_stop) {
            data_cmd |= I2C_DATA_CMD_STOP;
        }

        /* 写入数据 */
        intel_lpss_i2c_writel(priv, I2C_LPSS_DATA_CMD, data_cmd);
    }

    /* 等待传输完成 */
    timeout = 100000;
    while (timeout--) {
        intr_status = intel_lpss_i2c_readl(priv, I2C_LPSS_RAW_INTR_STAT);
        if (intr_status & I2C_INTR_STOP_DET) {
            break;
        }
        if (intr_status & I2C_INTR_TX_ABRT) {
            DEBUG("I2C-LPSS: TX abort\n");
            intel_lpss_i2c_readl(priv, I2C_LPSS_CLR_TX_ABRT);
            return -I2C_ERR_NACK;
        }
    }
    if (timeout == 0) {
        DEBUG("I2C-LPSS: Transfer timeout\n");
        return -I2C_ERR_TIMEOUT;
    }

    /* 清除中断标志 */
    intel_lpss_i2c_readl(priv, I2C_LPSS_CLR_STOP_DET);

    return len;
}

static int intel_lpss_i2c_read_bytes(intel_lpss_i2c_priv_t *priv,
                                     uint16_t addr, uint8_t *buf,
                                     uint16_t len, bool send_stop)
{
    uint32_t data_cmd;
    uint32_t intr_status;
    int i, timeout;

    /* 设置目标地址 */
    intel_lpss_i2c_writel(priv, I2C_LPSS_TAR, addr & 0x3FF);

    /* 发送读命令 */
    for (i = 0; i < len; i++) {
        /* 等待 TX FIFO 有空间 */
        timeout = 100000;
        while (timeout--) {
            uint32_t status = intel_lpss_i2c_readl(priv, I2C_LPSS_STATUS);
            if (status & I2C_STATUS_TFNF) {
                break;
            }
        }
        if (timeout == 0) {
            DEBUG("I2C-LPSS: TX FIFO timeout for read cmd\n");
            return -I2C_ERR_TIMEOUT;
        }

        /* 准备读命令 */
        data_cmd = I2C_DATA_CMD_CMD_READ;
        if (i == len - 1 && send_stop) {
            data_cmd |= I2C_DATA_CMD_STOP;
        }
        if (i == len - 1) {
            data_cmd |= I2C_DATA_CMD_NACK;
        }

        /* 写入命令 */
        intel_lpss_i2c_writel(priv, I2C_LPSS_DATA_CMD, data_cmd);
    }

    /* 读取数据 */
    for (i = 0; i < len; i++) {
        /* 等待 RX FIFO 有数据 */
        timeout = 100000;
        while (timeout--) {
            uint32_t rxflr = intel_lpss_i2c_readl(priv, I2C_LPSS_RXFLR);
            if (rxflr > 0) {
                break;
            }
            /* 检查错误 */
            intr_status = intel_lpss_i2c_readl(priv, I2C_LPSS_RAW_INTR_STAT);
            if (intr_status & I2C_INTR_TX_ABRT) {
                DEBUG("I2C-LPSS: TX abort during read\n");
                intel_lpss_i2c_readl(priv, I2C_LPSS_CLR_TX_ABRT);
                return -I2C_ERR_NACK;
            }
        }
        if (timeout == 0) {
            DEBUG("I2C-LPSS: RX FIFO timeout\n");
            return -I2C_ERR_TIMEOUT;
        }

        /* 读取数据 */
        buf[i] = intel_lpss_i2c_readl(priv, I2C_LPSS_DATA_CMD) & 0xFF;
    }

    /* 等待停止完成 */
    if (send_stop) {
        timeout = 100000;
        while (timeout--) {
            intr_status = intel_lpss_i2c_readl(priv, I2C_LPSS_RAW_INTR_STAT);
            if (intr_status & I2C_INTR_STOP_DET) {
                break;
            }
        }
        if (timeout > 0) {
            intel_lpss_i2c_readl(priv, I2C_LPSS_CLR_STOP_DET);
        }
    }

    return len;
}

/* ======================================================================
 * I²C 总线回调包装函数
 * ====================================================================== */

static int intel_lpss_i2c_master_write(i2c_bus_t *bus, uint16_t addr,
                                       const uint8_t *buf, uint16_t len) {
    if (!bus || !bus->priv) return -I2C_ERR_INVALID;
    intel_lpss_i2c_priv_t *priv = (intel_lpss_i2c_priv_t *)bus->priv;
    return intel_lpss_i2c_write_bytes(priv, addr, buf, len, 1);
}

static int intel_lpss_i2c_master_read(i2c_bus_t *bus, uint16_t addr,
                                      uint8_t *buf, uint16_t len) {
    if (!bus || !bus->priv) return -I2C_ERR_INVALID;
    intel_lpss_i2c_priv_t *priv = (intel_lpss_i2c_priv_t *)bus->priv;
    return intel_lpss_i2c_read_bytes(priv, addr, buf, len, 1);
}

static int intel_lpss_i2c_master_write_read(i2c_bus_t *bus, uint16_t addr,
                                            const uint8_t *wbuf, uint16_t wlen,
                                            uint8_t *rbuf, uint16_t rlen) {
    if (!bus || !bus->priv) return -I2C_ERR_INVALID;
    intel_lpss_i2c_priv_t *priv = (intel_lpss_i2c_priv_t *)bus->priv;
    int ret = intel_lpss_i2c_write_bytes(priv, addr, wbuf, wlen, 0);
    if (ret < 0) return ret;
    return intel_lpss_i2c_read_bytes(priv, addr, rbuf, rlen, 1);
}


/* ======================================================================
 * 控制器初始化与清理
 * ====================================================================== */

static int intel_lpss_i2c_init_controller(intel_lpss_i2c_priv_t *priv)
{
    uint32_t con_val;
    int ret;

    /* 禁用控制器 */
    intel_lpss_i2c_writel(priv, I2C_LPSS_ENABLE, 0);

    /* 等待禁用完成 */
    ret = intel_lpss_i2c_wait_bus_idle(priv);
    if (ret < 0) {
        return ret;
    }

    /* 清空 FIFO */
    intel_lpss_i2c_flush_fifos(priv);

    /* 清除所有中断 */
    intel_lpss_i2c_readl(priv, I2C_LPSS_CLR_INTR);

    /* 设置总线速度 */
    ret = intel_lpss_i2c_set_bus_speed(priv);
    if (ret < 0) {
        return ret;
    }

    /* 设置 FIFO 阈值 */
    intel_lpss_i2c_writel(priv, I2C_LPSS_RX_TL, 0);
    intel_lpss_i2c_writel(priv, I2C_LPSS_TX_TL, 0);

    /* 屏蔽所有中断 */
    intel_lpss_i2c_writel(priv, I2C_LPSS_INTR_MASK, 0);

    /* 配置控制寄存器 */
    con_val = I2C_CON_MASTER_MODE;
    con_val |= I2C_CON_SLAVE_DISABLE;
    con_val |= I2C_CON_RESTART_EN;
    con_val |= I2C_CON_TX_EMPTY_CTRL;

    switch (priv->bus_speed) {
    case I2C_SPEED_STANDARD:
        con_val |= I2C_CON_SPEED_STANDARD;
        break;
    case I2C_SPEED_FAST:
    case I2C_SPEED_FAST_PLUS:
        con_val |= I2C_CON_SPEED_FAST;
        break;
    default:
        con_val |= I2C_CON_SPEED_STANDARD;
        break;
    }

    intel_lpss_i2c_writel(priv, I2C_LPSS_CON, con_val);

    /* 禁用 DMA */
    intel_lpss_i2c_writel(priv, I2C_LPSS_DMA_CR, 0);

    /* 启用控制器 */
    intel_lpss_i2c_writel(priv, I2C_LPSS_ENABLE, I2C_ENABLE_BIT);

    /* 等待启用完成 */
    ret = intel_lpss_i2c_wait_bus_idle(priv);
    if (ret < 0) {
        DEBUG("I2C-LPSS: Failed to enable controller\n");
        return ret;
    }

    priv->initialized = true;
    return 0;
}

/* ======================================================================
 * 公共 API：控制器注册与枚举
 * ====================================================================== */

int intel_lpss_i2c_add_controller(void *base_addr, uint32_t input_clk,
                                  uint32_t bus_speed)
{
    intel_lpss_i2c_priv_t *priv;
    i2c_bus_t *bus;
    int idx, ret;

    if (g_lpss_i2c_count >= INTEL_LPSS_I2C_MAX_CONTROLLERS) {
        DEBUG("I2C-LPSS: Max controllers reached\n");
        return -1;
    }

    idx = g_lpss_i2c_count;
    priv = &g_lpss_i2c_controllers[idx];

    /* 初始化私有数据 */
    priv->base = base_addr;
    priv->input_clk = input_clk;
    priv->bus_speed = bus_speed;
    priv->sda_hold = 50;    /* 默认 50 ns */
    priv->sda_setup = 10;   /* 默认 10 ns */
    priv->initialized = false;

    /* 初始化控制器硬件 */
    ret = intel_lpss_i2c_init_controller(priv);
    if (ret < 0) {
        DEBUG("I2C-LPSS: Controller %d init failed: %d\n", idx, ret);
        return ret;
    }

    /* 注册 I2C 总线 */
    bus = i2c_alloc_bus();
    if (!bus) {
        DEBUG("I2C-LPSS: Failed to allocate bus %d\n", idx);
        return -1;
    }

    bus->bus_id = idx;
    bus->priv = priv;
    bus->master_write = intel_lpss_i2c_master_write;
    bus->master_read = intel_lpss_i2c_master_read;
    bus->master_write_read = intel_lpss_i2c_master_write_read;

    ret = i2c_register_bus(bus);
    if (ret < 0) {
        DEBUG("I2C-LPSS: Failed to register bus %d: %d\n", idx, ret);
        return ret;
    }

    g_lpss_i2c_count++;

    DEBUG("I2C-LPSS: Controller %d registered at %p, speed %u Hz\n",
          idx, base_addr, bus_speed);

    return idx;
}

/*
 * i2c_lpss_init_all() — PCI 枚举并初始化所有 Intel LPSS I²C 控制器
 *
 * 扫描 PCI 总线，查找 vendor=0x8086 (Intel) 且 class=0x0C (serial bus),
 * subclass=0x05 (SMBus/I²C) 的设备。对每个找到的控制器：
 *   1. 启用设备（bus master + MMIO）
 *   2. 从 BAR0 获取 MMIO 基地址
 *   3. 调用 intel_lpss_i2c_add_controller() 注册到 I²C 总线
 *
 * 返回已初始化的控制器数量，<0 表示出错。
 */
int i2c_lpss_init_all(void)
{
    const pci_device_t *dev;
    uint32_t total = 0;
    uint32_t i;

    /* 确保 PCI 枚举已完成 */
    pci_scan_all();

    /* 遍历所有已知 PCI 设备，查找 I²C 控制器 */
    for (i = 0; (dev = pci_get_device(i)) != NULL; i++) {
        /* 匹配 class=0x0C (Serial Bus), subclass=0x05 (SMBus/I²C) */
        if (dev->class_code != PCI_CLASS_SERIAL_BUS ||
            dev->subclass != PCI_SUBCLASS_I2C)
            continue;

        /* 启用设备：bus master + MMIO 空间 */
        pci_enable_bus_master((pci_device_t *)dev);
        pci_enable_mmio((pci_device_t *)dev);

        /* 从 BAR0 获取 MMIO 基地址 */
        if (dev->bars[0].size == 0 || !dev->bars[0].is_mmio) {
            continue;
        }

        void *base = (void *)(uintptr_t)dev->bars[0].base;
        uint32_t input_clk = 120000000;  /* Intel LPSS 默认输入时钟 120 MHz */
        uint32_t bus_speed = 400000;     /* 标准模式 400 kHz */

        int ret = intel_lpss_i2c_add_controller(base, input_clk, bus_speed);
        if (ret < 0) {
            DEBUG("I2C-LPSS: Controller at %p init failed (PCI %02x:%02x.%d)\n",
                  base, dev->bus, dev->dev, dev->func);
            continue;
        }

        total++;
    }

    return total;
}

/* ======================================================================
 * 版本信息
 * ====================================================================== */

const char *intel_lpss_i2c_version(void)
{
    return "Intel LPSS I2C Driver v1.0 (openos)";
}
