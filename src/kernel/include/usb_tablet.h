#ifndef USB_TABLET_H
#define USB_TABLET_H

#include "types.h"

/*
 * QEMU usb-tablet 绝对坐标输入驱动。
 * 当前实现目标是最小 UHCI + HID Tablet 支持，用于让 OpenOS 内部光标
 * 与宿主机/QEMU 光标使用同一套绝对坐标。
 */
void usb_tablet_init(void);
void usb_tablet_poll(int screen_width, int screen_height);
int usb_tablet_is_ready(void);
void usb_tablet_print_info(void);

#endif /* USB_TABLET_H */
