@echo off
rem M1.4 TCP handshake + ICMP/DHCP test (temp, does not affect build+run.bat)
setlocal enabledelayedexpansion
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "QEMU=C:\Program Files\qemu\qemu-system-x86_64.exe"
set "OVMF_CODE=C:\Program Files\qemu\share\edk2-x86_64-code.fd"
set "OVMF_VARS=%ROOT%\target\OVMF_VARS.fd"
set "IMAGE=%ROOT%\target\openos-uefi.img"
set "DATADISK=%ROOT%\target\openos-data.img"
set "FATDISK=%ROOT%\target\openos-fat.img"
set "SERLOG=%ROOT%\logs\net.ser"
set "PCAP=%ROOT%\logs\net.pcap"
set "PY=C:\Users\%USERNAME%\Downloads\python\python.exe"
set "TCPPORT=8888"
set "FWDIP=10.0.2.100"

if not exist "%PY%" set "PY=python"

echo [test] starting host TCP echo server on 127.0.0.1:%TCPPORT% ...
rem guestfwd cmd mode handles the connection via SLIRP; no separate listener needed

if exist "%SERLOG%" del /Q "%SERLOG%" >nul 2>&1
if exist "%PCAP%" del /Q "%PCAP%" >nul 2>&1

set "TIMEOUT_S=25"
echo [test] launching QEMU (guestfwd %FWDIP%:%TCPPORT% via cmd echo) ...
start "" /B "%QEMU%" -machine pc -cpu qemu64 -smp 4 -m 256M -drive "if=pflash,format=raw,unit=0,file=%OVMF_CODE%,readonly=on" -drive "if=pflash,format=raw,unit=1,file=%OVMF_VARS%" -drive "file=%IMAGE%,format=raw,media=disk,if=ide,index=0" -drive "file=%DATADISK%,format=raw,media=disk,if=ide,index=2" -drive "file=%FATDISK%,format=raw,media=disk,if=ide,index=3" -boot c -serial "file:%SERLOG%" -netdev "user,id=n0,guestfwd=tcp:%FWDIP%:%TCPPORT%-cmd:%PY% %ROOT%\tools\stdio_echo.py" -device virtio-net-pci,netdev=n0 -object filter-dump,id=f0,netdev=n0,file=%PCAP% -display none -no-reboot -no-shutdown

set /A _PN=%TIMEOUT_S% + 1
ping -n %_PN% 127.0.0.1 >nul
taskkill /IM qemu-system-x86_64.exe /F >nul 2>&1
taskkill /FI "WINDOWTITLE eq tcp-echo" /F >nul 2>&1

echo [test] done. serial=%SERLOG% pcap=%PCAP%
endlocal
