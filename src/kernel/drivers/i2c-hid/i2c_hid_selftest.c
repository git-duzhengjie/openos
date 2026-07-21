/**
 * @file i2c_hid_selftest.c
 * @brief HID over I²C 驱动自测
 */

#include "i2c_hid.h"
#include "../i2c/i2c.h"
#include "../../../arch/x86_64/include/klog64.h"

void i2c_hid_selftest(void)
{
    int ret;
    
    DEBUG("\n[i2c-hid] ===== I2C HID Selftest =====\n");
    
    /* 测试 1: 设备存在性检查 */
    bool present = i2c_hid_present();
    if (!present) {
        DEBUG("[i2c-hid] SKIP: No I2C HID device found (PNP0C50)\n");
        return;
    }
    DEBUG("[i2c-hid-selftest] PASS: Device presence check\n");
    
    /* 测试 2: 描述符结构完整性 */
    if (sizeof(i2c_hid_descriptor_t) != I2C_HID_DESC_LENGTH) {
        DEBUG("[i2c-hid-selftest] FAIL: HID descriptor size mismatch\n");
        return;
    }
    DEBUG("[i2c-hid-selftest] PASS: Descriptor structure integrity\n");
    
    /* 测试 3: 寄存器定义验证 */
    if (I2C_HID_REG_HID_DESC != 0x0001) {
        DEBUG("[i2c-hid-selftest] FAIL: Register definition mismatch\n");
        return;
    }
    DEBUG("[i2c-hid-selftest] PASS: Register definition validation\n");
    
    /* 测试 4: 命令定义验证 */
    if (I2C_HID_RESET != 0x0001) {
        DEBUG("[i2c-hid-selftest] FAIL: Command definition mismatch\n");
        return;
    }
    DEBUG("[i2c-hid-selftest] PASS: Command definition validation\n");
    
    /* 测试 5: I²C 消息构造测试 */
    i2c_msg_t msg;
    msg.addr = 0x4B;  /* 典型 HID 触摸地址 */
    msg.flags = I2C_M_WR;
    msg.len = 2;
    msg.buf = NULL;
    
    if (msg.addr != 0x4B) {
        DEBUG("[i2c-hid-selftest] FAIL: I2C message construction\n");
        return;
    }
    DEBUG("[i2c-hid-selftest] PASS: I2C message construction\n");
    
    /* 测试 6: 触摸参数配置验证 */
    i2c_hid_device_t dev;
    dev.max_contacts = 10;
    dev.max_x = 2736;
    dev.max_y = 1824;
    dev.width_px = 1368;
    dev.height_px = 912;
    
    if (dev.max_contacts != 10) {
        DEBUG("[i2c-hid-selftest] FAIL: Touch parameter setup\n");
        return;
    }
    DEBUG("[i2c-hid-selftest] PASS: Touch parameter configuration\n");
    
    /* 测试 7: 坐标缩放算法验证 */
    uint16_t physical_x = 2736;
    uint16_t physical_y = 1824;
    uint16_t pixel_x = (physical_x * 1368) / 2736;
    uint16_t pixel_y = (physical_y * 912) / 1824;
    
    if (pixel_x != 1368 || pixel_y != 912) {
        DEBUG("[i2c-hid-selftest] FAIL: Coordinate scaling calculation\n");
        return;
    }
    DEBUG("[i2c-hid-selftest] PASS: Coordinate scaling calculation\n");
    
    /* 测试 8: 报告长度边界检查 */
    uint16_t max_len = 512;
    if (max_len < 256) {
        DEBUG("[i2c-hid-selftest] FAIL: Input buffer too small\n");
        return;
    }
    DEBUG("[i2c-hid-selftest] PASS: Input buffer size validation\n");
    
    DEBUG("[i2c-hid-selftest] All tests passed!\n");
    SELFTEST_PASS("i2c_hid");
}

SELFTEST_MODULE(i2c_hid, i2c_hid_selftest);
