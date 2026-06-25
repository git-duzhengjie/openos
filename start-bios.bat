@echo off
set QEMU=C:\Program Files\qemu\qemu-system-x86_64.exe
set IMAGE=E:\openos\target\openos.img

echo Starting OpenOS (Legacy BIOS mode)...
"%QEMU%" -machine pc -cpu qemu64 -m 256M ^
  -drive file="%IMAGE%",format=raw,media=disk,if=ide ^
  -boot c ^
  -netdev tap,id=net0,ifname='OpenOS-TAP',script=no,downscript=no -device e1000,netdev=net0,mac=52:54:00:12:34:56 ^
  -serial stdio -vga std -net none
pause
