#!/usr/bin/env python3
"""
调试 dentry_t 结构体大小问题
在 create_dentry_under 函数中添加 sizeof(dentry_t) 打印
"""

import re

file_path = r"E:\openos\src\kernel\fs\vfs.c"

with open(file_path, 'r', encoding='utf-8') as f:
    content = f.read()

# 在 create_dentry_under 的 after link 输出后添加 sizeof 调试
old_pattern = '''    serial_write("[CREATE] after link: parent->child=0x");
    serial_write_hex((uint32_t)parent->child);
    serial_write(" child->sibling=0x");
    serial_write_hex(parent->child ? (uint32_t)parent->child->sibling : 0);
    serial_write(" d->sibling=0x");
    serial_write_hex((uint32_t)d->sibling);
    serial_write("\\n");
    
    return d;'''

new_code = '''    serial_write("[CREATE] after link: parent->child=0x");
    serial_write_hex((uint32_t)parent->child);
    serial_write(" child->sibling=0x");
    serial_write_hex(parent->child ? (uint32_t)parent->child->sibling : 0);
    serial_write(" d->sibling=0x");
    serial_write_hex((uint32_t)d->sibling);
    serial_write("\\n");
    serial_write("[CREATE] sizeof(dentry_t)=");
    serial_write_hex((uint32_t)sizeof(dentry_t));
    serial_write("\\n");
    serial_write("[CREATE] &dentry_pool[0]=0x");
    serial_write_hex((uint32_t)&dentry_pool[0]);
    serial_write(" &dentry_pool[1]=0x");
    serial_write_hex((uint32_t)&dentry_pool[1]);
    serial_write("\\n");
    
    return d;'''

if old_pattern in content:
    content = content.replace(old_pattern, new_code)
    with open(file_path, 'w', encoding='utf-8') as f:
        f.write(content)
    print("✓ 已添加 sizeof(dentry_t) 调试输出")
    print("✓ 已添加 dentry_pool 数组步长调试输出")
else:
    print("✗ 未找到匹配模式")
    # 尝试查找实际内容
    lines = content.split('\\n')
    for i, line in enumerate(lines):
        if 'after link' in line:
            print(f"找到 after link 在行 {i+1}: {line.strip()}")