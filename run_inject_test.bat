@echo off
rem ============================================================
rem  USB-HID QMP 注入验证脚本 (headless)
rem  目的: 在无窗口环境下, 通过 QMP input-send-event 注入
rem        鼠标/键盘事件, 验证 xHCI 中断 IN 端点 (epid 3)
rem        能否真正产生 usb_xhci_xfer_success (数据进 USB 层)。
rem ------------------------------------------------------------
rem  流程:
rem    1. 清日志, 后台启动 QEMU (headless + QMP:4444 + USB trace)
rem    2. 运行 tools\qmp_inject.py 注入鼠标+键盘事件
rem    3. 等待若干秒后杀掉 QEMU
rem    4. grep 串口日志 + trace, 输出关键判据
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
set "SERLOG=%ROOT%\logs\qfork.ser"
set "TRACELOG=%ROOT%\logs\usb_trace.log"
set "QMPPORT=4444"
set "PYTHON=C:\Users\Administrator\AppData\Local\Programs\Python\Python312\python.exe"
if not exist "%PYTHON%" set "PYTHON=python"

if exist "%SERLOG%" del /Q "%SERLOG%" >nul 2>&1
if exist "%TRACELOG%" del /Q "%TRACELOG%" >nul 2>&1

echo [inject] killing stale QEMU ...
taskkill /IM qemu-system-x86_64.exe /F >nul 2>&1
ping -n 2 127.0.0.1 >nul

echo [inject] starting QEMU headless (QMP tcp:%QMPPORT%) ...
start "" /B "%QEMU%" -machine pc -cpu qemu64 -smp 4 -m 256M ^
  -drive if=pflash,format=raw,unit=0,file="%OVMF_CODE%",readonly=on ^
  -drive if=pflash,format=raw,unit=1,file="%OVMF_VARS%" ^
  -drive file="%IMAGE%",format=raw,media=disk,if=ide,index=0 ^
  -drive file="%DATADISK%",format=raw,media=disk,if=ide,index=2 ^
  -drive file="%FATDISK%",format=raw,media=disk,if=ide,index=3 ^
  -device ich9-ahci,id=ahci0 -drive file="%AHCIDISK%",format=raw,if=none,id=sata0 -device ide-hd,drive=sata0,bus=ahci0.0 ^
  -drive file="%NVMEDISK%",format=raw,if=none,id=nvm0 -device nvme,serial=openos-nvme0,id=nvme0 -device nvme-ns,drive=nvm0,bus=nvme0 ^
  -device qemu-xhci,id=xhci0 -device usb-kbd,bus=xhci0.0 -device usb-mouse,bus=xhci0.0 ^
  -boot c ^
  -serial file:"%SERLOG%" -display none ^
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 ^
  -qmp tcp:127.0.0.1:%QMPPORT%,server,nowait ^
  -D "%TRACELOG%" ^
  -trace usb_xhci_doorbell_write -trace usb_xhci_ep_kick -trace usb_xhci_xfer_start ^
  -trace usb_xhci_xfer_success -trace usb_xhci_xfer_nak -trace usb_xhci_xfer_async ^
  -trace usb_xhci_xfer_retry -trace usb_xhci_xfer_error -trace usb_xhci_ep_state ^
  -trace usb_xhci_slot_address -trace usb_xhci_slot_configure -trace usb_xhci_ep_enable ^
  -trace usb_xhci_port_notify -trace usb_packet_state_change

echo [inject] running QMP injector ...
"%PYTHON%" "%ROOT%\tools\qmp_inject.py" --host 127.0.0.1 --port %QMPPORT% --wait 6 --rounds 3

echo [inject] letting interrupt endpoints drain (3s) ...
ping -n 4 127.0.0.1 >nul

echo [inject] stopping QEMU ...
taskkill /IM qemu-system-x86_64.exe /F >nul 2>&1
ping -n 2 127.0.0.1 >nul

echo.
echo [inject] ===== serial: xHCI HID summary =====
wsl.exe -d Ubuntu -- bash -lc "tr -d '\000\r' </mnt/e/openos/logs/qfork.ser 2>/dev/null | grep -aE 'connected=|\[arm\]|\[evt-raw\]|\[hid-|proto=|configured' | tail -30"
echo.
echo [inject] ===== trace: interrupt-EP (epid 3) activity =====
wsl.exe -d Ubuntu -- bash -lc "grep -aE 'xfer_success|xfer_start|packet_state_change' /mnt/e/openos/logs/usb_trace.log 2>/dev/null | grep -aE 'ep 1|epid 3' | tail -20; echo '--- xfer_success total:'; grep -ac 'xfer_success' /mnt/e/openos/logs/usb_trace.log 2>/dev/null; echo '--- ep 1 packets total:'; grep -aE 'packet_state_change' /mnt/e/openos/logs/usb_trace.log 2>/dev/null | grep -ac 'ep 1'"
echo.
echo [inject] done. serial=%SERLOG%  trace=%TRACELOG%
endlocal
