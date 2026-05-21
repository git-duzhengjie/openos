#!/usr/bin/env python3
# 修改 build.sh: 添加 vga.c 编译

file_path = '/mnt/e/openos/build.sh'

with open(file_path, 'r', encoding='utf-8') as f:
    content = f.read()

# 1. 在 ASM_FILES 后添加 vga.c 到 C_FILES
old_c_files = 'C_FILES="${KERNEL_DIR}/kernel.c"'
new_c_files = '''C_FILES="${KERNEL_DIR}/kernel.c"
C_FILES="${C_FILES} ${KERNEL_DIR}/drivers/vga.c"'''
content = content.replace(old_c_files, new_c_files)

with open(file_path, 'w', encoding='utf-8') as f:
    f.write(content)

print('build.sh modified: added vga.c')
