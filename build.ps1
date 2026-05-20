# Quick build script for OpenOS Phase2

Write-Host "Building OpenOS Phase2..." -ForegroundColor Cyan

# Clean
Write-Host "Cleaning target..." -ForegroundColor Yellow
Remove-Item -Path target -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path target -Force | Out-Null

Write-Host "Compiling bootloader..." -ForegroundColor Green
# Bootloader (nasm)
nasm -f bin src/boot/boot.asm -o target/boot.bin
if ($LASTEXITCODE -ne 0) { Write-Host "Bootloader failed" -ForegroundColor Red; exit 1 }

Write-Host "Compiling stage2..." -ForegroundColor Green
nasm -f bin src/boot/stage2.asm -o target/stage2.bin
if ($LASTEXITCODE -ne 0) { Write-Host "Stage2 failed" -ForegroundColor Red; exit 1 }

Write-Host "Compiling kernel..." -ForegroundColor Green
# Kernel C files
gcc -ffreestanding -m32 -fno-pie -fno-stack-protector -fno-exceptions -fno-rtti -c src/kernel/kernel.c -o target/kernel.o -I src/kernel
nasm -f elf32 src/kernel/boot.asm -o target/kernel_asm.o
if ($LASTEXITCODE -ne 0) { Write-Host "Kernel asm failed" -ForegroundColor Red; exit 1 }

Write-Host "Linking kernel..." -ForegroundColor Green
# Link kernel ELF
ld -m elf_i386 -T src/kernel/linker.ld -o target/kernel.elf target/kernel.o target/kernel_asm.o
objcopy -O binary target/kernel.elf target/kernel.bin
if ($LASTEXITCODE -ne 0) { Write-Host "Kernel link failed" -ForegroundColor Red; exit 1 }

Write-Host "Creating disk image..." -ForegroundColor Green
# Make 2MB disk
$size = 2 * 1024 * 1024
$fsutilArgs = @("file", "createnew", "target\openos.img", $size)
& fsutil @fsutilArgs 2>&1 | Out-Null

# Write bootloader
$boot = [System.IO.File]::ReadAllBytes("target\boot.bin")
$stream = [System.IO.File]::OpenWrite("target\openos.img")
$stream.Write($boot, 0, $boot.Length)

# Write stage2 at LBA 1
$stage2 = [System.IO.File]::ReadAllBytes("target\stage2.bin")
$stream.Position = 512
$stream.Write($stage2, 0, $stage2.Length)

# Write kernel at LBA 9 (start at sector 9, 512*9=4608)
$kernel = [System.IO.File]::ReadAllBytes("target\kernel.bin")
$stream.Position = 512 * 9
$stream.Write($kernel, 0, $kernel.Length)
$stream.Close()

Write-Host "Build done! Now starting QEMU..." -ForegroundColor Cyan
Start-Sleep -Seconds 1

# Run QEMU
qemu-system-x86_64 -drive format=raw,file=target/openos.img -m 512M -vga std
