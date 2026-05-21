#!/usr/bin/env python3
# 测试 VGA 控制台：运行 QEMU，转储 VGA 内存

import subprocess
import time

# 运行 QEMU 3 秒后退出
cmd = [
    'timeout', '3',
    'qemu-system-i386',
    '-drive', 'format=raw,file=target/openos.img',
    '-m', '512M',
    '-serial', 'stdio',
    '-display', 'none',
    '-monitor', 'stdio'
]

print('Starting QEMU to test VGA console...')
proc = subprocess.Popen(
    cmd,
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    cwd='/mnt/e/openos'
)

# 等待 2 秒让内核启动
time.sleep(2)

# 从 QEMU 监视器转储 VGA 内存 (0xB8000, 80x25x2 = 4000 bytes)
# 使用 pmemsave 命令
proc.stdin.write('pmemsave 0xB8000 4000 /tmp/vga_dump.bin\n')
proc.stdin.flush()

time.sleep(1)

# 退出 QEMU
proc.stdin.write('quit\n')
proc.stdin.flush()

# 读取输出
output = proc.stdout.read()
print('Serial output:')
print(output)

# 读取 VGA 转储并解析
try:
    with open('/tmp/vga_dump.bin', 'rb') as f:
        vga_data = f.read(4000)
    
    print('\nVGA Screen (first 10 lines):')
    for y in range(10):
        line = ''
        for x in range(80):
            char = vga_data[(y * 80 + x) * 2]
            if char >= 32 and char < 127:
                line += chr(char)
            else:
                line += ' '
        print(line.rstrip())
except:
    print('Failed to capture VGA memory')
