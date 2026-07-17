@echo off
REM ============================================================
REM openos - Touchscreen diagnostic launcher (M8-A)
REM 使用 usb-tablet 模拟单点触屏（在 QEMU 中鼠标点击=手指触碰）
REM
REM 说明：QEMU 目前不含 usb-mtouch (multi-touch) 官方设备，
REM       使用 usb-tablet 结合鼠标操作即可回归验证 M8-A 的
REM       单点触屏 report 解析路径（tip switch + 绝对坐标）
REM
REM 与 run.bat 的区别：
REM   - 添加 -global usb-tablet.usb_version=2 减少 usb1.1 干扰
REM   - 保留 usb-kbd 便于输入锁屏密码
REM Login: user=openos  password=openos
REM ============================================================

set QEMU="C:\Program Files\qemu\qemu-system-x86_64.exe"
set OVMF_CODE="C:\Program Files\qemu\share\edk2-x86_64-code.fd"
set OVMF_VARS="E:\openos\target\OVMF_VARS.fd"
set IMG="E:\openos\target\openos-uefi.img"
set SERIAL="E:\openos\logs\run_touch.ser"

%QEMU% -machine pc -cpu qemu64 -m 256M -vga none ^
  -drive if=pflash,format=raw,unit=0,file=%OVMF_CODE%,readonly=on ^
  -drive if=pflash,format=raw,unit=1,file=%OVMF_VARS% ^
  -drive file=%IMG%,format=raw,media=disk,if=ide,index=0 ^
  -device virtio-gpu-pci ^
  -device qemu-xhci,id=xhci ^
  -device usb-tablet,bus=xhci.0 ^
  -device usb-kbd,bus=xhci.0 ^
  -display sdl ^
  -boot c ^
  -serial file:%SERIAL% ^
  -no-reboot
