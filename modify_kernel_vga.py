#!/usr/bin/env python3
# 修改 kernel.c: 添加 vga_init() 调用

file_path = '/mnt/e/openos/src/kernel/kernel.c'

with open(file_path, 'r', encoding='utf-8') as f:
    content = f.read()

# 1. 添加 vga.h 头文件
old_include = '#include "keyboard.h"'
new_include = '#include "keyboard.h"\n#include "vga.h"'
content = content.replace(old_include, new_include)

# 2. 在 keyboard_init() 后添加 vga_init() 调用
old_init_order = """    /* 初始化键盘驱动 */
    keyboard_init();
    
    /* 初始化调度器 */"""

new_init_order = """    /* 初始化键盘驱动 */
    keyboard_init();
    
    /* 初始化VGA控制台 */
    vga_init();
    
    /* 初始化调度器 */"""

content = content.replace(old_init_order, new_init_order)

with open(file_path, 'w', encoding='utf-8') as f:
    f.write(content)

print('kernel.c modified: added vga_init() call')
