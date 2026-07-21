/**
 * @file intel_lpss_i2c.h
 * @brief Intel LPSS (Low Power Subsystem) I2C 控制器驱动头文件
 *
 * 本头文件定义 Intel LPSS I2C 控制器驱动的公共 API。
 *
 * @author openos
 * @date 2026
 */

#ifndef __INTEL_LPSS_I2C_H__
#define __INTEL_LPSS_I2C_H__

#include <stdint.h>
#include <stdbool.h>

/* ======================================================================
 * 公共 API
 * ====================================================================== */

/**
 * @brief 添加并初始化 Intel LPSS I2C 控制器
 *
 * @param base_addr 控制器寄存器基地址
 * @param input_clk 输入时钟频率 (Hz)
 * @param bus_speed 总线速度 (Hz)，使用 I2C_SPEED_* 常量
 * @return 成功返回总线 ID (>=0)，失败返回错误码 (<0)
 */
int intel_lpss_i2c_add_controller(void *base_addr, uint32_t input_clk,
                                  uint32_t bus_speed);

/**
 * @brief 获取驱动版本信息
 *
 * @return 版本字符串
 */
const char *intel_lpss_i2c_version(void);

/* ======================================================================
 * Selftest 支持
 * ====================================================================== */

#ifdef CONFIG_I2C_LPSS_SELFTEST
int intel_lpss_i2c_selftest(void);
#endif

#endif /* __INTEL_LPSS_I2C_H__ */
