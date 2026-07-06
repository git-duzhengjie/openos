@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem =====================================================================
rem  OpenOS one-shot build+run for Windows
rem  Purpose: WSL Ubuntu incremental build -> Windows QEMU launch
rem  Author : openos (autogen)
rem =====================================================================
rem  Usage:
rem    build+run.bat                 default: build (incremental) + run GUI
rem    build+run.bat build           build only
rem    build+run.bat run             run only (use existing image)
rem    build+run.bat clean           build clean + run GUI
rem    build+run.bat headless        build + run 20s, serial->log, grep
rem    build+run.bat headless clean  clean build + headless run
rem =====================================================================

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "WSL_DISTRO=Ubuntu"
set "QEMU=C:\Program Files\qemu\qemu-system-x86_64.exe"
set "OVMF_CODE=C:\Program Files\qemu\share\edk2-x86_64-code.fd"
set "OVMF_VARS=%ROOT%\target\OVMF_VARS.fd"
set "IMAGE=%ROOT%\target\openos-uefi.img"
set "DATADISK=%ROOT%\target\openos-data.img"
set "FATDISK=%ROOT%\target\openos-fat.img"
set "AHCIDISK=%ROOT%\target\openos-ahci.img"
set "LOGDIR=%ROOT%\logs"
set "SERLOG=%LOGDIR%\qfork.ser"

set "MODE_BUILD=1"
set "MODE_RUN=1"
set "MODE_HEADLESS=0"
set "MODE_CLEAN=0"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="build"    (set "MODE_RUN=0"       & shift & goto parse_args)
if /I "%~1"=="run"      (set "MODE_BUILD=0"     & shift & goto parse_args)
if /I "%~1"=="headless" (set "MODE_HEADLESS=1"  & shift & goto parse_args)
if /I "%~1"=="clean"    (set "MODE_CLEAN=1"     & shift & goto parse_args)
if /I "%~1"=="-h"       (goto usage)
if /I "%~1"=="--help"   (goto usage)
if /I "%~1"=="/?"       (goto usage)
echo [build+run] unknown arg: %~1
goto usage

:args_done
if not exist "%LOGDIR%" mkdir "%LOGDIR%" >nul 2>&1

rem --- ensure persistent data disk exists (64MB raw) for ATA persistence ---
if not exist "%DATADISK%" (
  echo [build+run] creating data disk %DATADISK% ^(64MB^)
  "%QEMU:qemu-system-x86_64.exe=qemu-img.exe%" create -f raw "%DATADISK%" 64M >nul 2>&1
  if not exist "%DATADISK%" (
    echo [build+run] qemu-img failed; creating via fsutil
    fsutil file createnew "%DATADISK%" 67108864 >nul 2>&1
  )
)

rem --- FAT32 exchange disk (32MB) : do NOT overwrite if present ---
if not exist "%FATDISK%" (
  echo [build+run] creating blank FAT32 disk %FATDISK% ^(32MB^)
  "%QEMU:qemu-system-x86_64.exe=qemu-img.exe%" create -f raw "%FATDISK%" 32M >nul 2>&1
  if not exist "%FATDISK%" (
    fsutil file createnew "%FATDISK%" 33554432 >nul 2>&1
  )
)

rem --- AHCI/SATA test disk (64MB) for M2 AHCI driver dev ---
if not exist "%AHCIDISK%" (
  echo [build+run] creating AHCI/SATA disk %AHCIDISK% ^(64MB^)
  "%QEMU:qemu-system-x86_64.exe=qemu-img.exe%" create -f raw "%AHCIDISK%" 64M >nul 2>&1
  if not exist "%AHCIDISK%" (
    fsutil file createnew "%AHCIDISK%" 67108864 >nul 2>&1
  )
)

echo.
echo === OpenOS build+run ===
echo   ROOT      : %ROOT%
echo   WSL       : %WSL_DISTRO%
echo   IMAGE     : %IMAGE%
echo   MODE      : build=%MODE_BUILD% run=%MODE_RUN% headless=%MODE_HEADLESS% clean=%MODE_CLEAN%
echo.

if "%MODE_BUILD%"=="1" goto do_build
goto skip_build

:do_build
echo [build+run] --- WSL build phase ---
where wsl.exe >nul 2>&1
if errorlevel 1 (
  echo [build+run] ERROR: wsl.exe not found in PATH. Cannot build.
  exit /b 10
)
if "%MODE_CLEAN%"=="1" (
  echo [build+run] wsl -d %WSL_DISTRO% -- bash -lc "cd /mnt/e/openos && ./build.sh clean"
  wsl.exe -d %WSL_DISTRO% -- bash -lc "cd /mnt/e/openos && ARCH=x86_64 ./build.sh clean"
  if errorlevel 1 (
    echo [build+run] ERROR: WSL clean failed rc=%ERRORLEVEL%
    exit /b 11
  )
)
echo [build+run] wsl -d %WSL_DISTRO% -- bash -lc "cd /mnt/e/openos && ARCH=x86_64 ./build.sh"
wsl.exe -d %WSL_DISTRO% -- bash -lc "cd /mnt/e/openos && ARCH=x86_64 ./build.sh"
set "BRC=%ERRORLEVEL%"
if not "%BRC%"=="0" (
  echo [build+run] ERROR: WSL build failed rc=%BRC%. QEMU launch skipped.
  exit /b 12
)
echo [build+run] --- build OK ---

:skip_build

if not exist "%IMAGE%" (
  echo [build+run] ERROR: image not found: %IMAGE%
  echo [build+run] Run without 'run' arg to build first.
  exit /b 20
)
if not exist "%QEMU%" (
  echo [build+run] ERROR: QEMU not found: %QEMU%
  exit /b 21
)
if not exist "%OVMF_CODE%" (
  echo [build+run] ERROR: OVMF CODE not found: %OVMF_CODE%
  exit /b 22
)
if not exist "%OVMF_VARS%" (
  echo [build+run] WARN: OVMF_VARS not found, copying VARS template edk2-i386-vars.fd
  copy /Y "C:\Program Files\qemu\share\edk2-i386-vars.fd" "%OVMF_VARS%" >nul 2>&1
)

if "%MODE_RUN%"=="0" (
  echo [build+run] run skipped by mode.
  exit /b 0
)

echo.
echo [build+run] --- QEMU launch phase ---

if "%MODE_HEADLESS%"=="1" goto run_headless
goto run_gui

:run_gui
echo [build+run] GUI mode; serial mirrored to %SERLOG%
"%QEMU%" -machine pc -cpu qemu64 -smp 4 -m 256M ^
  -drive if=pflash,format=raw,unit=0,file="%OVMF_CODE%",readonly=on ^
  -drive if=pflash,format=raw,unit=1,file="%OVMF_VARS%" ^
  -drive file="%IMAGE%",format=raw,media=disk,if=ide,index=0 ^
  -drive file="%DATADISK%",format=raw,media=disk,if=ide,index=2 ^
  -drive file="%FATDISK%",format=raw,media=disk,if=ide,index=3 ^
  -device ich9-ahci,id=ahci0 -drive file="%AHCIDISK%",format=raw,if=none,id=sata0 -device ide-hd,drive=sata0,bus=ahci0.0 ^
  -boot c ^
  -serial file:"%SERLOG%" -vga std -netdev user,id=n0 -device virtio-net-pci,netdev=n0 -object filter-dump,id=f0,netdev=n0,file=%CD%\logs\net.pcap
set "QRC=%ERRORLEVEL%"
echo [build+run] QEMU exited rc=%QRC%
echo [build+run] serial log: %SERLOG%
exit /b %QRC%

:run_headless
set "HEADLESS_TIMEOUT_S=90"
echo [build+run] headless mode; timeout %HEADLESS_TIMEOUT_S% s, serial -> %SERLOG%
if exist "%SERLOG%" del /Q "%SERLOG%" >nul 2>&1
set "QEMU_STDERR=%LOGDIR%\qemu.stderr"
if exist "%QEMU_STDERR%" del /Q "%QEMU_STDERR%" >nul 2>&1
rem  Launch QEMU detached; wait up to N seconds; force-kill remaining processes.
start "" /B "%QEMU%" -machine pc -cpu qemu64 -smp 4 -m 256M ^
  -drive "if=pflash,format=raw,unit=0,file=%OVMF_CODE%,readonly=on" ^
  -drive "if=pflash,format=raw,unit=1,file=%OVMF_VARS%" ^
  -drive "file=%IMAGE%,format=raw,media=disk,if=ide,index=0" ^
  -drive "file=%DATADISK%,format=raw,media=disk,if=ide,index=2" ^
  -drive "file=%FATDISK%,format=raw,media=disk,if=ide,index=3" ^
  -device ich9-ahci,id=ahci0 -drive "file=%AHCIDISK%,format=raw,if=none,id=sata0" -device ide-hd,drive=sata0,bus=ahci0.0 ^
  -boot c -serial "file:%SERLOG%" -netdev user,id=n0,hostfwd=udp::15353-:53 -device virtio-net-pci,netdev=n0 -object filter-dump,id=f0,netdev=n0,file=%CD%\logs\net.pcap -display none -no-reboot -no-shutdown 2>"%QEMU_STDERR%"
timeout /T %HEADLESS_TIMEOUT_S% /NOBREAK >nul 2>&1
rem  Fallback: 'timeout' bails on non-interactive stdin; use ping as a portable sleep.
set /A _PN=%HEADLESS_TIMEOUT_S% + 1
ping -n %_PN% 127.0.0.1 >nul
taskkill /IM qemu-system-x86_64.exe /F >nul 2>&1
set "QRC=0"
if exist "%QEMU_STDERR%" (
  for %%A in ("%QEMU_STDERR%") do if %%~zA GTR 0 (
    echo [build+run] === QEMU stderr ===
    type "%QEMU_STDERR%"
  )
)
echo.
echo [build+run] === serial log grep ===
wsl.exe -d %WSL_DISTRO% -- bash -lc "tr -d '\000' </mnt/e/openos/logs/qfork.ser 2>/dev/null | sed 's/\x1b\[[0-9;=]*[a-zA-Z]//g' | grep -aE 'hello64|hello_fork|\[fork|\[wait|\[do_exit|\[exit|BUG|#PF|#GP|panic|reboot|PASS|FAIL|ready|loader' | tail -80"
echo.
echo [build+run] === serial tail (last 20 raw) ===
wsl.exe -d %WSL_DISTRO% -- bash -lc "tr -d '\000' </mnt/e/openos/logs/qfork.ser 2>/dev/null | sed 's/\x1b\[[0-9;=]*[a-zA-Z]//g' | tail -20"
echo.
echo [build+run] full serial log: %SERLOG%
exit /b %QRC%

:usage
echo.
echo Usage:
echo   build+run.bat                 build (incremental) + run GUI (default)
echo   build+run.bat build           build only
echo   build+run.bat run             run only, no build
echo   build+run.bat clean           clean build + run GUI
echo   build+run.bat headless        build + run 25s no-window, log grep
echo   build+run.bat headless clean  clean build + headless run
echo.
exit /b 1
