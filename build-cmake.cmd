@echo off
REM ============================================================
REM openos - 构建脚本 (Windows via CMake + WSL)
REM ============================================================

setlocal

set OPENOS_ROOT=E:\openos
set BUILD_DIR=%OPENOS_ROOT%\target

REM 检查 WSL 是否可用
wsl -e ls /mnt/e/openos >nul 2>&1
if errorlevel 1 (
    echo [ERROR] WSL 未安装或未运行
    echo 请先安装 WSL (Ubuntu) 并确保 WSL 可用
    pause
    exit /b 1
)

REM 清理旧构建
if "%1"=="clean" (
    echo Cleaning build directory...
    wsl -d Ubuntu -u root bash -c "rm -rf /mnt/e/openos/target/*"
    echo Done.
    goto :eof
)

REM 切换到项目根目录
cd /d %OPENOS_ROOT%

REM 确保 build 目录存在
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo.
echo ========================================
echo   Building openos (CMake + WSL)
echo ========================================
echo.

REM 运行 CMake 构建 (通过 WSL)
wsl -d Ubuntu -u root bash -c "cd /mnt/e/openos && mkdir -p target && cd target && cmake .. && make"

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo.
echo ========================================
echo   Build successful!
echo ========================================
echo.
echo Output: %BUILD_DIR%\openos.img
echo.
echo Run with: make run
echo.

endlocal