#!/bin/bash
# 格式化 AHCI 测试盘为 ext2（整盘，无分区表），并植入测试文件/目录。
# 使用 mkfs.ext2 + debugfs（免 root、免 loop 挂载）。
set -e
IMG=target/openos-ahci.img

# 整盘 ext2，1KB 块（便于测试小块路径），无 journal
mkfs.ext2 -q -F -b 1024 -L OPENOSEXT "$IMG"

# 用 debugfs 写入文件（-w 可写模式）
TMP=$(mktemp -d)
printf 'Hello from ext2 on AHCI!\n' > "$TMP/hello.txt"
printf 'openos ext2 read-only driver works.\nline2\nline3\n' > "$TMP/readme.txt"
# 生成一个大文件测试间接块（>12KB 触发一级间接）
head -c 40000 /dev/zero | tr '\0' 'A' > "$TMP/big.dat"

debugfs -w -R "write $TMP/hello.txt hello.txt" "$IMG" 2>/dev/null
debugfs -w -R "write $TMP/readme.txt readme.txt" "$IMG" 2>/dev/null
debugfs -w -R "write $TMP/big.dat big.dat" "$IMG" 2>/dev/null
debugfs -w -R "mkdir /subdir" "$IMG" 2>/dev/null
debugfs -w -R "write $TMP/hello.txt subdir/inside.txt" "$IMG" 2>/dev/null

rm -rf "$TMP"
echo "ext2 image ready: $IMG"
