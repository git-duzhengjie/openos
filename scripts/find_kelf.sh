#!/usr/bin/env bash
cd /mnt/e/openos
echo "=== x86_64 ELF candidates ==="
for f in $(find build target -name '*.elf' 2>/dev/null); do
  t=$(file "$f" | grep -oE 'x86-64')
  [ -n "$t" ] && echo "$f"
done
echo "=== img / EFI ==="
ls -la target/openos-uefi.img 2>/dev/null
find build target -name '*.efi' 2>/dev/null | head
find build -name 'openos*.elf' -o -name 'kernel64*.elf' 2>/dev/null | head
