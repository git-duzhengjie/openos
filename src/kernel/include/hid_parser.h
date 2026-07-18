/* =========================================================================
 * hid_parser.h -- M8-C.1 Minimal HID Report Descriptor parser.
 *
 * 只提取 OPENOS 触摸驱动实际需要的字段，不做通用完备解析：
 *   * Usage Page 0x0D (Digitizer) 相关 Usage
 *   * Usage 0x22 Finger Collection -> 每个手指槽位一个 finger_slot
 *   * Usage 0x42 Tip Switch        -> tip_bit
 *   * Usage 0x51 Contact Identifier-> contact_id_bit
 *   * Usage 0x54 Contact Count     -> contact_count_bit
 *   * Usage Page 0x01 X / Y (0x30/0x31)
 *
 * 结果放在 hid_touch_layout_t 中，字段为 “bit offset + bit size”，
 * 由后续 hid_touch_report_parse() 直接位精确提取。
 *
 * 该模块纯逻辑：无堆分配、无 I/O、无内核依赖，可在宿主 selftest 中直接编译。
 * ========================================================================= */

#ifndef OPENOS_KERNEL_HID_PARSER_H
#define OPENOS_KERNEL_HID_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HID_TOUCH_MAX_SLOTS 10

typedef struct hid_field {
    uint16_t bit_offset;    /* 相对整份 Input Report 的比特偏移 */
    uint8_t  bit_size;      /* 单个 field 的比特宽度 */
    uint8_t  present;       /* 是否解析到该字段 */
    int32_t  logical_min;
    int32_t  logical_max;
} hid_field_t;

typedef struct hid_finger_slot {
    uint8_t     present;      /* 该槽位是否有效 */
    hid_field_t tip;          /* Tip Switch (1 bit) */
    hid_field_t contact_id;   /* Contact Identifier */
    hid_field_t x;            /* X 坐标 */
    hid_field_t y;            /* Y 坐标 */
} hid_finger_slot_t;

typedef struct hid_touch_layout {
    uint8_t           slot_count;                   /* 实际解析到的手指槽位数 */
    uint16_t          report_bytes;                 /* 整份 Report 的字节长度 */
    uint8_t           has_report_id;                /* 首字节是否为 Report ID */
    uint8_t           report_id;                    /* 若 has_report_id=1，则本 layout 对应的 report id */
    hid_field_t       contact_count;                /* Digitizer Contact Count */
    hid_finger_slot_t slots[HID_TOUCH_MAX_SLOTS];
} hid_touch_layout_t;

/* 解析结果：单帧触摸快照，供上层 dispatcher 直接消费。 */
typedef struct hid_touch_sample {
    uint8_t  slot_count;                  /* 本帧被解析出的槽数 */
    uint8_t  contact_count;               /* 设备声明本帧的活跃触点数（0=用 tip 推断） */
    struct {
        uint8_t  present;                 /* 该槽本帧是否被上报（tip=1 或曾经=1） */
        uint8_t  tip;                     /* Tip Switch */
        int32_t  contact_id;              /* Contact Identifier */
        int32_t  x;                       /* logical 坐标（未缩放） */
        int32_t  y;
        int32_t  logical_max_x;           /* 从 descriptor 里学到的 logical_max，供缩放使用 */
        int32_t  logical_max_y;
    } fingers[HID_TOUCH_MAX_SLOTS];
} hid_touch_sample_t;

/* -------------------------------------------------------------------------
 * 解析 HID Report Descriptor。
 * @param desc      Report Descriptor 原始字节
 * @param desc_len  长度（字节）
 * @param out       输出：字段偏移表
 * @return true=成功识别为 Digitizer 触摸设备（>=1 finger 槽位含 X/Y/Tip）
 *         false=非触摸描述符或格式无法解析
 * ------------------------------------------------------------------------- */
bool hid_parse_report_descriptor(const uint8_t *desc, size_t desc_len,
                                 hid_touch_layout_t *out);

/* -------------------------------------------------------------------------
 * 按 layout 从 Input Report 中提取一帧触摸样本。
 * @param layout    hid_parse_report_descriptor 得到的字段表
 * @param report    Interrupt-IN 传回的原始数据（含 report id 时不需要额外剥离）
 * @param report_len 原始数据长度
 * @param sample    输出触摸样本
 * @return true=成功，false=长度不足或 report id 不匹配
 * ------------------------------------------------------------------------- */
bool hid_touch_report_parse(const hid_touch_layout_t *layout,
                            const uint8_t *report, size_t report_len,
                            hid_touch_sample_t *sample);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_KERNEL_HID_PARSER_H */
