#!/usr/bin/env python3
"""修复 mode & 0xFF 为 mode & 0x0F，因为 S_IWUSR=0x80 落在低8位内"""

import os

files = [
    r"E:\openos\src\kernel\shell.c",
    r"E:\openos\src\kernel\fs\vfs.c",
]

for fpath in files:
    with open(fpath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # 只替换 mode & 0xFF，不动 mode & 0xFFFF
    new_content = content.replace('mode & 0xFF', 'mode & 0x0F')
    
    count = content.count('mode & 0xFF')
    
    with open(fpath, 'w', encoding='utf-8') as f:
        f.write(new_content)
    
    print(f"✓ {os.path.basename(fpath)}: {count} 处 mode & 0xFF → mode & 0x0F")

print("\n修复完成！重新编译即可测试。")
