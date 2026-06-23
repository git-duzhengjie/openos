/* ============================================================
 * openos - 键盘驱动头文件
 * ============================================================ */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

typedef struct keyboard_state {
    uint8_t shift;
    uint8_t ctrl;
    uint8_t alt;
    uint8_t meta;
    uint8_t caps_lock;
    uint8_t num_lock;
    uint8_t scroll_lock;
    uint8_t extended;
    uint32_t irq_count;
    uint32_t make_count;
    uint32_t break_count;
} keyboard_state_t;

/* 键盘初始化 */
void keyboard_init(void);

/* 获取当前键盘修饰键/锁定键状态 */
const keyboard_state_t *keyboard_get_state(void);
uint32_t keyboard_get_modifiers(void);


#endif /* KEYBOARD_H */
