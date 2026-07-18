/* ============================================================
 * hid_type_infer.h —— HID 设备类型推断（跨架构纯逻辑）
 *
 * M8-A.1：为 USB HID 提供统一的类型分类。
 *   输入：VID/PID/bInterfaceProtocol + 可选 Report Descriptor
 *   输出：hid_infer_type_t（键盘/鼠标/绝对定位板/单点触屏/未知）
 *
 * 分类优先级（从高到低）：
 *   1. proto=1 → BOOT_KEYBD；proto=2 → BOOT_MOUSE（boot 协议最权威）
 *   2. Report Descriptor Usage Page 0x0D (Digitizer)
 *      + Usage 0x04 (Touch Screen) → TOUCHSCREEN
 *      + Usage 0x02 (Pen)          → TABLET
 *   3. 已知触屏控制器 VID 白名单 → TOUCHSCREEN
 *   4. QEMU tablet VID:PID 0627:0001 → TABLET（可 OPENOS_TOUCH_TEST 覆盖）
 *   5. proto=0 兜底 → TOUCHSCREEN（真机触屏常见）
 * ============================================================ */
#ifndef _HID_TYPE_INFER_H_
#define _HID_TYPE_INFER_H_

#include "types.h"

typedef enum {
    HID_INFER_UNKNOWN     = 0,
    HID_INFER_BOOT_KEYBD  = 1,
    HID_INFER_BOOT_MOUSE  = 2,
    HID_INFER_TABLET      = 3,   /* 绝对定位（QEMU tablet / 数位板 pen） */
    HID_INFER_TOUCHSCREEN = 4,   /* 单点/多点触屏 */
} hid_infer_type_t;

/* 纯函数：给定 VID/PID/proto + 可选 Report Desc，返回推断类型
 *   desc/desc_len 可为 (NULL, 0) —— 表示无描述符可用，跳过 desc 探测
 *   force_touch_test != 0 时，QEMU tablet(0627:0001) 会被覆盖为 TOUCHSCREEN
 *     （对应编译宏 OPENOS_TOUCH_TEST 的运行时等价）
 */
hid_infer_type_t hid_type_infer(uint16_t vid, uint16_t pid, uint8_t proto,
                                const uint8_t *desc, uint32_t desc_len,
                                int force_touch_test);

/* Report Descriptor 扫描：查找 Usage Page 0x0D 后的 Usage。
 *   命中 Touch Screen (0x04) → 返回 HID_INFER_TOUCHSCREEN
 *   命中 Pen (0x02)          → 返回 HID_INFER_TABLET
 *   否则                     → 返回 HID_INFER_UNKNOWN
 * 供 selftest / hid_type_infer 内部共享
 */
hid_infer_type_t hid_desc_scan_digitizer(const uint8_t *desc, uint32_t desc_len);

/* 检查 VID 是否属于已知触屏控制器白名单 */
int hid_vid_is_known_touch(uint16_t vid);

#endif /* _HID_TYPE_INFER_H_ */
