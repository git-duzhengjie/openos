#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_i18n_keys.py — 从 src/kernel/include/i18n.h 的枚举 i18n_key_t
生成 src/kernel/include/i18n_keys.inc（JSON key 名 → 枚举下标映射表）。

由 build.sh 每次构建自动调用，保证映射表与枚举永久同步：
新增 / 删除 / 调整 I18N_KEY_* 后无需手工维护 .inc。

用法：python3 tools/gen_i18n_keys.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
I18N_H = os.path.join(ROOT, "src", "kernel", "include", "i18n.h")
KEYS_INC = os.path.join(ROOT, "src", "kernel", "include", "i18n_keys.inc")


def main():
    with open(I18N_H, "r", encoding="utf-8") as f:
        text = f.read()

    keys = []
    for m in re.finditer(r'\b(I18N_KEY_[A-Z0-9_]+)\b', text):
        k = m.group(1)
        if k == "I18N_KEY_COUNT":
            continue
        if k not in keys:
            keys.append(k)

    if not keys:
        print("[ERR] 未在 i18n.h 找到任何 I18N_KEY_* 枚举", file=sys.stderr)
        sys.exit(1)

    with open(KEYS_INC, "w", encoding="utf-8") as f:
        f.write("/* 自动生成，勿手改。由 tools/gen_i18n_keys.py 从 i18n.h 生成。 */\n")
        f.write("/* JSON key 名 -> 枚举下标映射，顺序与 i18n_key_t 严格一致。 */\n")
        f.write("static const char *const k_key_names[I18N_KEY_COUNT] = {\n")
        for k in keys:
            f.write(f'    [{k}] = "{k}",\n')
        f.write("};\n")

    print(f"[ok] gen i18n_keys.inc ({len(keys)} keys)")


if __name__ == "__main__":
    main()
