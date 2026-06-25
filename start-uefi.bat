@echo off
set QEMU=C:\Program Files\qemu\qemu-system-x86_64.exe
set OVMF_CODE=C:\Program Files\qemu\share\edk2-x86_64-code.fd
set OVMF_VARS=E:\openos\target\OVMF_VARS.fd
set IMAGE=E:\openos\target\openos-uefi.img

"%QEMU%" -machine q35 -cpu qemu64 -m 256M ^
  -drive if=pflash,format=raw,unit=0,file="%OVMF_CODE%",readonly=on ^
  -drive if=pflash,format=raw,unit=1,file="%OVMF_VARS%" ^
  -drive file="%IMAGE%",format=raw,media=disk,if=ide,index=0,media=disk ^
  -boot c ^
  -serial stdio -vga std -net none
pause
