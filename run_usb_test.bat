@echo off
rem ============================================================
rem  USB Mass Storage (U 盘) headless 测试脚本
rem  目的: 挂载 usb-storage 设备, 验证 xHCI 枚举 MSC 接口 +
rem        BOT/SCSI: INQUIRY / READ CAPACITY / READ(10)
rem ------------------------------------------------------------
rem  流程:
rem    1. 清日志, 后台启动 QEMU (headless + USB trace + usb-storage)
rem    2. 等待内核枚举 + usb_msc_init 跑完 (10s)
rem    3. 杀掉 QEMU
rem    4. grep 串口日志 [usb-msc] 输出关键判据
rem ============================================================
setlocal
set "ROOT=E:\openos"
set "QEMU=C:\Program Files\qemu\qemu-system-x86_64.exe"
set "OVMF_CODE=C:\Program Files\qemu\share\edk2-x86_64-code.fd"
set "OVMF_VARS=%ROOT%\target\OVMF_VARS.fd"
set "IMAGE=%ROOT%\target\openos-uefi.img"
set "DATADISK=%ROOT%\target\openos-data.img"
set "FATDISK=%ROOT%\target\openos-fat.img"
set "AHCIDISK=%ROOT%\target\openos-ahci.img"
set "NVMEDISK=%ROOT%\target\openos-nvme.img"
set "USBDISK=%ROOT%\target\openos-usb.img"
set "SERLOG=%ROOT%\logs\qfork.ser"
set "TRACELOG=%ROOT%\logs\usb_trace.log"

if exist "%SERLOG%" del /Q "%SERLOG%" >nul 2>&1
if exist "%TRACELOG%" del /Q "%TRACELOG%" >nul 2>&1

echo [usb-test] killing stale QEMU ...
taskkill /IM qemu-system-x86_64.exe /F >nul 2>&1
ping -n 2 127.0.0.1 >nul

echo [usb-test] starting QEMU headless (usb-storage attached) ...
start "" /B "%QEMU%" -machine pc -cpu qemu64 -smp 4 -m 256M ^
  -drive if=pflash,format=raw,unit=0,file="%OVMF_CODE%",readonly=on ^
  -drive if=pflash,format=raw,unit=1,file="%OVMF_VARS%" ^
  -drive file="%IMAGE%",format=raw,media=disk,if=ide,index=0 ^
  -drive file="%DATADISK%",format=raw,media=disk,if=ide,index=2 ^
  -drive file="%FATDISK%",format=raw,media=disk,if=ide,index=3 ^
  -device ich9-ahci,id=ahci0 -drive file="%AHCIDISK%",format=raw,if=none,id=sata0 -device ide-hd,drive=sata0,bus=ahci0.0 ^
  -drive file="%NVMEDISK%",format=raw,if=none,id=nvm0 -device nvme,serial=openos-nvme0,id=nvme0 -device nvme-ns,drive=nvm0,bus=nvme0 ^
  -device qemu-xhci,id=xhci0 -device usb-kbd,bus=xhci0.0 -device usb-mouse,bus=xhci0.0 ^
  -drive file="%USBDISK%",format=raw,if=none,id=usbstick ^
  -device usb-storage,bus=xhci0.0,drive=usbstick ^
  -boot c ^
  -serial file:"%SERLOG%" -display none ^
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 ^
  -d guest_errors,trace:usb_xhci_ep_kick,trace:usb_xhci_xfer_start ^
  -D "%TRACELOG%" ^
  -trace usb_xhci_slot_address -trace usb_xhci_slot_configure -trace usb_xhci_ep_enable ^
  -trace usb_xhci_xfer_start -trace usb_xhci_xfer_success -trace usb_xhci_xfer_error ^
  -trace usb_xhci_ep_kick -trace usb_msd_cmd -trace scsi_req_parsed ^
  -trace usb_xhci_fetch_trb -trace usb_xhci_xfer_nak -trace usb_xhci_xfer_async -trace usb_xhci_ep_set_dequeue ^
  -trace usb_xhci_ep_set_dequeue -trace usb_xhci_run -trace usb_xhci_slot_configure_error ^
  -trace "usb_xhci_irq*" -trace "usb_xhci_queue*" -trace "usb_xhci_event*" -trace "usb_xhci_ering*"

echo [usb-test] waiting for kernel enumeration + usb_msc_init (25s) ...
ping -n 26 127.0.0.1 >nul

echo [usb-test] stopping QEMU ...
taskkill /IM qemu-system-x86_64.exe /F >nul 2>&1
ping -n 2 127.0.0.1 >nul

echo.
echo [usb-test] ===== serial: USB MSC summary =====
wsl.exe -d Ubuntu -- bash -lc "tr -d '\000\r' </mnt/e/openos/logs/qfork.ser 2>/dev/null | grep -aE 'usb-msc|MSC|is_msc|ep_bulk|mass-storage|U-disk|INQUIRY|capacity|vendor=|max_lun' | tail -40"
echo.
echo [usb-test] ===== serial: xHCI enumerate summary =====
wsl.exe -d Ubuntu -- bash -lc "tr -d '\000\r' </mnt/e/openos/logs/qfork.ser 2>/dev/null | grep -aE 'enumerate|SET_CONFIG|iface=|connected=' | tail -20"
echo.
echo [usb-test] done. serial=%SERLOG%  trace=%TRACELOG%
endlocal
