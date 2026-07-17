@echo off
REM ============================================================
REM openos - Windows native QEMU launcher (GUI mode)
REM Golden params: -vga none + PS/2 kbd/mouse + virtio-gpu
REM Login: user=openos  password=openos
REM ============================================================

set QEMU="C:\Program Files\qemu\qemu-system-x86_64.exe"
set OVMF_CODE="C:\Program Files\qemu\share\edk2-x86_64-code.fd"
set OVMF_VARS="E:\openos\target\OVMF_VARS.fd"
set IMG="E:\openos\target\openos-uefi.img"
set SERIAL="E:\openos\logs\run.ser"

%QEMU% -machine pc -cpu qemu64 -m 256M -vga none ^
  -drive if=pflash,format=raw,unit=0,file=%OVMF_CODE%,readonly=on ^
  -drive if=pflash,format=raw,unit=1,file=%OVMF_VARS% ^
  -drive file=%IMG%,format=raw,media=disk,if=ide,index=0 ^
  -device virtio-gpu-pci ^
  -display gtk,grab-on-hover=on ^
  -boot c ^
  -serial file:%SERIAL% ^
  -no-reboot
