#!/usr/bin/env python3
# 临时补丁: 优化 xhci_wait_event 的 spin 循环, 快速轮询避免 delay 拖死
f = 'src/arch/x86_64/gui64/xhci64.c'
s = open(f, encoding='utf-8').read()

# 1) 提高 spin 上限
old1 = 'for (uint32_t spin = 0; spin < 200000; spin++) {'
new1 = 'for (uint32_t spin = 0; spin < 20000000; spin++) {'
assert old1 in s, 'spin loop not found'
s = s.replace(old1, new1, 1)

# 2) else 分支的 delay 改轻量 pause
old2 = '        } else {\n            xhci_delay(2000);\n        }'
new2 = '        } else {\n            __asm__ volatile("pause");\n        }'
assert old2 in s, 'delay branch not found'
s = s.replace(old2, new2, 1)

open(f, 'w', encoding='utf-8').write(s)
print('patched OK')
