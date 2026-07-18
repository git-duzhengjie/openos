/* ============================================================
 * hid_type_infer.c —— HID 设备类型推断（纯逻辑，跨架构复用）
 *
 * 详见 hid_type_infer.h。零依赖（只依赖 types.h），可被 x86_64 /
 * aarch64 / selftest 三方 include 编译。
 * ============================================================ */
#include "hid_type_infer.h"

/* 已知触屏控制器 VID 白名单（真机常见）：
 *   0x27C6  Goodix Technology (GT911/GT9271)
 *   0x2AE0  Focaltech Systems
 *   0x222A  ILITEK Corporation
 *   0x0EEF  eGalax Inc.
 *   0x0596  Wacom（含触屏系列）
 *   0x0483  STMicro（部分自研触屏 MCU）
 *   0x256C  Huion（数位板/触屏）
 */
static const uint16_t k_touch_vids[] = {
    0x27C6, 0x2AE0, 0x222A, 0x0EEF, 0x0596, 0x0483, 0x256C
};
#define K_TOUCH_VIDS_N ((int)(sizeof(k_touch_vids)/sizeof(k_touch_vids[0])))

int hid_vid_is_known_touch(uint16_t vid) {
    for (int i = 0; i < K_TOUCH_VIDS_N; i++) {
        if (k_touch_vids[i] == vid) return 1;
    }
    return 0;
}

/* HID Report Descriptor item 头部：
 *   b7..b4 = bTag, b3..b2 = bType, b1..b0 = bSize
 *   bSize: 00=0, 01=1, 10=2, 11=4 字节数据
 *   bType: 00=Main, 01=Global, 10=Local, 11=Reserved
 *   Usage Page (Global, tag=0) 头部 = 0x04 | size；1B 版本 = 0x05
 *   Usage      (Local,  tag=0) 头部 = 0x08 | size；1B 版本 = 0x09
 *
 * 我们只做保守扫描：遇到 Usage Page(Digitizer) 后，紧接着的第一个 Usage 决定结果。
 * 长格式 item（首字节 0xFE）本实现直接放弃扫描，避免误读。
 */
hid_infer_type_t hid_desc_scan_digitizer(const uint8_t *desc, uint32_t desc_len) {
    if (!desc || desc_len < 2) return HID_INFER_UNKNOWN;

    int in_digitizer_page = 0;
    uint32_t i = 0;
    while (i < desc_len) {
        uint8_t head = desc[i];
        if (head == 0xFE) {
            /* long item：不解析，直接放弃 */
            return HID_INFER_UNKNOWN;
        }
        uint8_t size_code = head & 0x03;
        uint8_t type      = (head >> 2) & 0x03;
        uint8_t tag       = (head >> 4) & 0x0F;
        uint32_t data_len = (size_code == 3) ? 4u : (uint32_t)size_code;

        if (i + 1 + data_len > desc_len) return HID_INFER_UNKNOWN;

        /* 读取小端数据（HID 描述符固定 LE） */
        uint32_t data = 0;
        for (uint32_t b = 0; b < data_len; b++) {
            data |= (uint32_t)desc[i + 1 + b] << (8 * b);
        }

        if (type == 1 /* Global */ && tag == 0 /* Usage Page */) {
            in_digitizer_page = (data == 0x0D);
        } else if (type == 2 /* Local */ && tag == 0 /* Usage */) {
            if (in_digitizer_page) {
                if (data == 0x04) return HID_INFER_TOUCHSCREEN;
                if (data == 0x02) return HID_INFER_TABLET;
                /* Usage 0x01=Digitizer / 0x05=Touch Pad 等其他也归为 TOUCHSCREEN */
                if (data == 0x01 || data == 0x05) return HID_INFER_TOUCHSCREEN;
                /* 命中 Digitizer Page 但 Usage 未识别，保守不判定 */
            }
        }
        i += 1 + data_len;
    }
    return HID_INFER_UNKNOWN;
}

hid_infer_type_t hid_type_infer(uint16_t vid, uint16_t pid, uint8_t proto,
                                const uint8_t *desc, uint32_t desc_len,
                                int force_touch_test) {
    /* 1. boot 协议最权威 */
    if (proto == 1) return HID_INFER_BOOT_KEYBD;
    if (proto == 2) return HID_INFER_BOOT_MOUSE;
    if (proto != 0) return HID_INFER_UNKNOWN;

    /* 2. Report Descriptor 探测 */
    hid_infer_type_t t = hid_desc_scan_digitizer(desc, desc_len);
    if (t != HID_INFER_UNKNOWN) return t;

    /* 3. 已知触屏 VID 白名单 */
    if (hid_vid_is_known_touch(vid)) return HID_INFER_TOUCHSCREEN;

    /* 4. QEMU tablet（可被 force_touch_test 覆盖） */
    if (vid == 0x0627 && pid == 0x0001) {
        return force_touch_test ? HID_INFER_TOUCHSCREEN : HID_INFER_TABLET;
    }

    /* 5. proto=0 兜底：假定为单点触屏 */
    return HID_INFER_TOUCHSCREEN;
}
