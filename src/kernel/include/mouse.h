#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

/* PS/2 鼠标状态 */
#define MOUSE_LEFT_BUTTON   0x01
#define MOUSE_RIGHT_BUTTON  0x02
#define MOUSE_MIDDLE_BUTTON 0x04

typedef struct {
    int x;
    int y;
    uint8_t buttons;       /* bit0=left, bit1=right, bit2=middle */
    int dx;                /* 相对移动量 */
    int dy;
    int present;           /* 鼠标是否存在 */
    uint32_t irq_count;     /* 已接收 IRQ12 包字节数 */
    uint32_t packet_count;  /* 已解析完整数据包数量 */
    uint32_t desync_count;  /* 丢弃的错位字节数量 */
    uint8_t last_ack;       /* 初始化时最后一次 ACK/响应 */
    int packet_index;       /* 当前包字节索引 (0,1,2) */
    uint8_t packet[3];      /* 3字节包缓存 */
    int max_x;              /* 坐标右边界，通常为屏幕宽度-1 */
    int max_y;              /* 坐标下边界，通常为屏幕高度-1 */
} mouse_state_t;

/* 初始化 PS/2 鼠标 (IRQ12) */
void mouse_init(void);

/* 获取当前鼠标状态 */
mouse_state_t *mouse_get_state(void);

/* 设置鼠标坐标范围与当前位置，GUI 切换分辨率后必须同步 */
void mouse_set_bounds(int width, int height);
void mouse_set_position(int x, int y);

void mouse_print_info(void);

/* IRQ 处理 – 由中断处理函数调用 */
void mouse_irq_handle(void);

#endif /* MOUSE_H */