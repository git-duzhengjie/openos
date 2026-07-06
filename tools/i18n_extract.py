#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
i18n_extract.py — 从现有 src/kernel/i18n.c 的两张写死表提取翻译，
生成：
  1) res/i18n/en.json      —— 英文译文（JSON: { "I18N_KEY_XXX": "value", ... }）
  2) res/i18n/zh.json      —— 中文译文
  3) src/kernel/include/i18n_keys.inc —— 枚举名字符串表 k_key_names[]
     （供 i18n.c 运行时把 JSON key 名映射回枚举下标）

用法：python3 tools/i18n_extract.py
一次性迁移工具：把「写死在 C 里的译文」搬到 JSON。运行后即可删除 i18n.c 里的两张表。
"""
import os
import re
import json
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
I18N_C = os.path.join(ROOT, "src", "kernel", "i18n.c")
I18N_H = os.path.join(ROOT, "src", "kernel", "include", "i18n.h")
OUT_DIR = os.path.join(ROOT, "res", "i18n")
KEYS_INC = os.path.join(ROOT, "src", "kernel", "include", "i18n_keys.inc")

# 匹配一行:  [I18N_KEY_XXX] = "值",   （值里可能含转义 \" \\ \n）
ROW_RE = re.compile(r'\[\s*(I18N_KEY_[A-Z0-9_]+)\s*\]\s*=\s*"((?:[^"\\]|\\.)*)"')


def parse_table(text, table_name):
    """提取 static const char *const table_name[...] = { ... }; 内的 key->raw_value"""
    start = text.find(table_name)
    if start < 0:
        print(f"[ERR] 未找到表 {table_name}", file=sys.stderr)
        sys.exit(1)
    brace = text.find("{", start)
    # 找到与之匹配的右括号
    depth = 0
    i = brace
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    body = text[brace:i]
    result = {}
    for m in ROW_RE.finditer(body):
        result[m.group(1)] = m.group(2)  # 保留 C 转义原样
    return result


def c_escape_to_value(raw):
    """把 C 字符串字面量里的转义序列还原成真实字符（供写入 JSON）。"""
    out = []
    i = 0
    while i < len(raw):
        c = raw[i]
        if c == "\\" and i + 1 < len(raw):
            nxt = raw[i + 1]
            mp = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\", "0": "\0"}
            if nxt in mp:
                out.append(mp[nxt])
                i += 2
                continue
        out.append(c)
        i += 1
    return "".join(out)


def enum_order(text):
    """从 i18n.h 提取枚举 i18n_key_t 的声明顺序（用于生成 k_key_names[] 与表下标一致）。"""
    # 抓取 enum 块内所有 I18N_KEY_XXX 标识符（排除 I18N_KEY_COUNT）
    keys = []
    for m in re.finditer(r'\b(I18N_KEY_[A-Z0-9_]+)\b', text):
        k = m.group(1)
        if k == "I18N_KEY_COUNT":
            continue
        if k not in keys:
            keys.append(k)
    return keys


def main():
    with open(I18N_C, "r", encoding="utf-8") as f:
        c_text = f.read()
    with open(I18N_H, "r", encoding="utf-8") as f:
        h_text = f.read()

    en_raw = parse_table(c_text, "k_strings_en")
    zh_raw = parse_table(c_text, "k_strings_zh")
    keys = enum_order(h_text)

    print(f"[info] 枚举 key 数: {len(keys)}, EN 表: {len(en_raw)}, ZH 表: {len(zh_raw)}")

    en = {k: c_escape_to_value(en_raw.get(k, "")) for k in keys}
    zh = {k: c_escape_to_value(zh_raw.get(k, "")) for k in keys}

    os.makedirs(OUT_DIR, exist_ok=True)
    with open(os.path.join(OUT_DIR, "en.json"), "w", encoding="utf-8") as f:
        json.dump(en, f, ensure_ascii=False, indent=2)
        f.write("\n")
    with open(os.path.join(OUT_DIR, "zh.json"), "w", encoding="utf-8") as f:
        json.dump(zh, f, ensure_ascii=False, indent=2)
        f.write("\n")

    # 生成 k_key_names[] —— 与枚举下标严格对齐
    with open(KEYS_INC, "w", encoding="utf-8") as f:
        f.write("/* 自动生成，勿手改。由 tools/i18n_extract.py 生成。 */\n")
        f.write("/* JSON key 名 → 枚举下标映射，顺序与 i18n_key_t 严格一致。 */\n")
        f.write("static const char *const k_key_names[I18N_KEY_COUNT] = {\n")
        for k in keys:
            f.write(f'    [{k}] = "{k}",\n')
        f.write("};\n")

    print(f"[ok] 写出 {OUT_DIR}/en.json, zh.json 及 {KEYS_INC}")


if __name__ == "__main__":
    main()
