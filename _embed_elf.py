"""将 ELF 文件转为 C 头文件数组，供内核嵌入"""
import sys

elf_path = sys.argv[1] if len(sys.argv) > 1 else "target/hello.elf"
out_path = sys.argv[2] if len(sys.argv) > 2 else "src/kernel/include/embed_hello.h"

with open(elf_path, "rb") as f:
    data = f.read()

name = "hello_elf"
lines = []
lines.append(f"/* Auto-generated - embed {elf_path} */")
lines.append(f"static const unsigned char {name}[] = {{")
for i in range(0, len(data), 16):
    chunk = data[i:i+16]
    hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
    lines.append(f"    {hex_str},")
lines.append("};")
lines.append(f"static const unsigned int {name}_size = {len(data)};")

with open(out_path, "w") as f:
    f.write("\n".join(lines) + "\n")

print(f"Generated {out_path}: {len(data)} bytes")
