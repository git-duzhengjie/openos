@echo off
setlocal
cd /d %~dp0\..

qemu-system-i386 ^
  -drive format=raw,file=target/openos.img ^
  -m 512M ^
  -serial stdio ^
  -device piix3-usb-uhci,id=uhci ^
  -device usb-tablet,bus=uhci.0 ^
  -display gtk,show-cursor=on

endlocal
