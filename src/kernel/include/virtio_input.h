#ifndef OPENOS_KERNEL_VIRTIO_INPUT_H
#define OPENOS_KERNEL_VIRTIO_INPUT_H

#include "types.h"

/*
 * virtio-input (virtio spec 5.8) — 键盘/鼠标经 virtio eventq 上报事件。
 * PCI: vendor 0x1af4, device 0x1052 (modern)。每个物理输入设备对应
 * 一个独立 virtio-input PCI function，因此需要按 index 遍历枚举。
 *
 * 事件通过单个 eventq (queue 0) 上报，每条 virtio_input_event 占 8 字节：
 *   type(le16) / code(le16) / value(le32)
 * 语义与 Linux evdev 完全一致（EV_KEY / EV_REL / EV_ABS / EV_SYN）。
 *
 * 本驱动把 evdev 事件翻译成现有 GUI 注入通路：
 *   - 键盘 EV_KEY(KEY_*)          -> gui_post_key_code_with_modifiers()
 *   - 鼠标 EV_REL(REL_X/Y/WHEEL)  -> mouse_inject_relative()
 *   - 平板 EV_ABS(ABS_X/Y)        -> mouse_set_absolute_position_with_wheel()
 *   - EV_KEY(BTN_LEFT/RIGHT/MIDDLE) 累积到按钮位图，在 EV_SYN 时提交
 */

/* ---- evdev 事件类型 (type) ---- */
#define VIRTIO_INPUT_EV_SYN   0x00
#define VIRTIO_INPUT_EV_KEY   0x01
#define VIRTIO_INPUT_EV_REL   0x02
#define VIRTIO_INPUT_EV_ABS   0x03

/* ---- EV_REL 轴 (code) ---- */
#define VIRTIO_INPUT_REL_X      0x00
#define VIRTIO_INPUT_REL_Y      0x01
#define VIRTIO_INPUT_REL_WHEEL  0x08

/* ---- EV_ABS 轴 (code) ---- */
#define VIRTIO_INPUT_ABS_X      0x00
#define VIRTIO_INPUT_ABS_Y      0x01

/* ---- EV_KEY 鼠标按钮 (code) ---- */
#define VIRTIO_INPUT_BTN_LEFT    0x110
#define VIRTIO_INPUT_BTN_RIGHT   0x111
#define VIRTIO_INPUT_BTN_MIDDLE  0x112

/* eventq 上报的单条事件（小端布局，与 spec 一致） */
typedef struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} __attribute__((packed)) virtio_input_event_t;

/* 探测并初始化所有 virtio-input 设备；无设备时安静返回（PS/2 继续工作）。 */
void virtio_input_init(void);

/* 已成功初始化的 virtio-input 设备数量。 */
uint32_t virtio_input_device_count(void);

/* 轮询所有设备 eventq，把已完成的事件翻译并注入 GUI。
 * 在 GUI poll loop 中周期调用（与 PS/2 共存，二者叠加注入同一通路）。 */
void virtio_input_poll(void);

#endif /* OPENOS_KERNEL_VIRTIO_INPUT_H */
