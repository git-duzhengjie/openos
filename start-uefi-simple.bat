@echo off
set QEMU=C:\Program Files\qemu\qemu-system-x86_64.exe
set OVMF=C:\Program Files\qemu\share\edk2-x86_64-code.fd
set IMAGE=E:\openos\target\openos-uefi.img

echo Starting OpenOS (UEFI Simple Mode)...
"%QEMU%" -machine q35 -cpu qemu64 -m 256M ^
  -bios "%OVMF%" ^
  -hda "%IMAGE%" ^
  -serial stdio -vga std -net none
pause
