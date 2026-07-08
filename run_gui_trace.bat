@echo off
rem  GUI + USB trace: reuse existing image, capture xHCI/USB internal events
set "ROOT=E:\openos"
set "QEMU=C:\Program Files\qemu\qemu-system-x86_64.exe"
set "OVMF_CODE=C:\Program Files\qemu\share\edk2-x86_64-code.fd"
set "OVMF_VARS=%ROOT%\target\OVMF_VARS.fd"
set "IMAGE=%ROOT%\target\openos-uefi.img"
set "DATADISK=%ROOT%\target\openos-data.img"
set "FATDISK=%ROOT%\target\openos-fat.img"
set "AHCIDISK=%ROOT%\target\openos-ahci.img"
set "NVMEDISK=%ROOT%\target\openos-nvme.img"
set "SERLOG=%ROOT%\logs\qfork.ser"
set "TRACELOG=%ROOT%\logs\usb_trace.log"
if exist "%SERLOG%" del /Q "%SERLOG%" >nul 2>&1
if exist "%TRACELOG%" del /Q "%TRACELOG%" >nul 2>&1
echo [trace] GUI mode; serial -> %SERLOG% ; usb trace -> %TRACELOG%
"%QEMU%" -machine pc -cpu qemu64 -smp 4 -m 256M ^
  -drive if=pflash,format=raw,unit=0,file="%OVMF_CODE%",readonly=on ^
  -drive if=pflash,format=raw,unit=1,file="%OVMF_VARS%" ^
  -drive file="%IMAGE%",format=raw,media=disk,if=ide,index=0 ^
  -drive file="%DATADISK%",format=raw,media=disk,if=ide,index=2 ^
  -drive file="%FATDISK%",format=raw,media=disk,if=ide,index=3 ^
  -device ich9-ahci,id=ahci0 -drive file="%AHCIDISK%",format=raw,if=none,id=sata0 -device ide-hd,drive=sata0,bus=ahci0.0 ^
  -drive file="%NVMEDISK%",format=raw,if=none,id=nvm0 -device nvme,serial=openos-nvme0,id=nvme0 -device nvme-ns,drive=nvm0,bus=nvme0 ^
  -device qemu-xhci,id=xhci0 -device usb-kbd,bus=xhci0.0 -device usb-mouse,bus=xhci0.0 ^
  -boot c ^
  -serial file:"%SERLOG%" -vga std -netdev user,id=n0 -device virtio-net-pci,netdev=n0 ^
  -D "%TRACELOG%" ^
  -trace usb_xhci_doorbell_write -trace usb_xhci_ep_kick -trace usb_xhci_xfer_start ^
  -trace usb_xhci_xfer_success -trace usb_xhci_xfer_nak -trace usb_xhci_xfer_async ^
  -trace usb_xhci_xfer_retry -trace usb_xhci_xfer_error -trace usb_xhci_queue_event ^
  -trace usb_xhci_slot_address -trace usb_xhci_slot_configure -trace usb_xhci_ep_enable ^
  -trace usb_xhci_port_notify -trace usb_packet_state_change
echo [trace] QEMU exited rc=%ERRORLEVEL%
