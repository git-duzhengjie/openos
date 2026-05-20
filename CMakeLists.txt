cmake_minimum_required(VERSION 3.15)
project(openos C ASM)

# ============================================================
# 目标架构
# ============================================================
set(TARGET_ARCH "i386")
set(CMAKE_SYSTEM_NAME "OpenOS")
set(CMAKE_C_COMPILER_WORKS 1)

# ============================================================
# 工具链 (32位裸机)
# ============================================================
set(CMAKE_C_COMPILER "gcc")
set(CMAKE_ASM_COMPILER "nasm")
set(CMAKE_LINKER "ld")

# 编译器标志
set(CMAKE_C_FLAGS_INIT "-m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
-fno-pie -fno-stack-protector -fno-builtin -fno-pic \
-I ${CMAKE_SOURCE_DIR}/src/kernel/include")

set(CMAKE_ASM_FLAGS_INIT "-f elf32")

# 链接器标志
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m elf_i386 -T ${CMAKE_SOURCE_DIR}/src/kernel/linker.ld")

# ============================================================
# 输出目录
# ============================================================
set(BUILD_DIR "${CMAKE_SOURCE_DIR}/target")
set(ISO_DIR "${CMAKE_SOURCE_DIR}/iso")

file(MAKE_DIRECTORY ${BUILD_DIR})
file(MAKE_DIRECTORY ${ISO_DIR}/boot)

# ============================================================
# 1. 编译 Bootloader (纯汇编 → bin)
# ============================================================
add_custom_command(
    OUTPUT ${BUILD_DIR}/boot.bin
    COMMAND ${CMAKE_ASM_COMPILER} -f bin ${CMAKE_SOURCE_DIR}/src/boot/boot.asm -o ${BUILD_DIR}/boot.bin
    DEPENDS ${CMAKE_SOURCE_DIR}/src/boot/boot.asm
    COMMENT "Assembling bootloader..."
)

# ============================================================
# 2. Phase 2 内核对象文件
# ============================================================

# entry.asm
add_custom_command(
    OUTPUT ${BUILD_DIR}/entry.o
    COMMAND ${CMAKE_ASM_COMPILER} ${CMAKE_ASM_FLAGS_INIT} ${CMAKE_SOURCE_DIR}/src/kernel/entry.asm -o ${BUILD_DIR}/entry.o
    DEPENDS ${CMAKE_SOURCE_DIR}/src/kernel/entry.asm
    COMMENT "Assembling entry.asm..."
)

# isr.asm
add_custom_command(
    OUTPUT ${BUILD_DIR}/isr.o
    COMMAND ${CMAKE_ASM_COMPILER} ${CMAKE_ASM_FLAGS_INIT} ${CMAKE_SOURCE_DIR}/src/kernel/isr.asm -o ${BUILD_DIR}/isr.o
    DEPENDS ${CMAKE_SOURCE_DIR}/src/kernel/isr.asm
    COMMENT "Assembling isr.asm..."
)

# gdt_flush.asm
add_custom_command(
    OUTPUT ${BUILD_DIR}/gdt_flush.o
    COMMAND ${CMAKE_ASM_COMPILER} ${CMAKE_ASM_FLAGS_INIT} ${CMAKE_SOURCE_DIR}/src/kernel/gdt_flush.asm -o ${BUILD_DIR}/gdt_flush.o
    DEPENDS ${CMAKE_SOURCE_DIR}/src/kernel/gdt_flush.asm
    COMMENT "Assembling gdt_flush.asm..."
)

# context_switch.asm
add_custom_command(
    OUTPUT ${BUILD_DIR}/context_switch.o
    COMMAND ${CMAKE_ASM_COMPILER} ${CMAKE_ASM_FLAGS_INIT} ${CMAKE_SOURCE_DIR}/src/kernel/sched/context_switch.asm -o ${BUILD_DIR}/context_switch.o
    DEPENDS ${CMAKE_SOURCE_DIR}/src/kernel/sched/context_switch.asm
    COMMENT "Assembling context_switch.asm..."
)

# kernel.c
add_custom_command(
    OUTPUT ${BUILD_DIR}/kernel.o
    COMMAND ${CMAKE_C_COMPILER} ${CMAKE_C_FLAGS_INIT} -c ${CMAKE_SOURCE_DIR}/src/kernel/kernel.c -o ${BUILD_DIR}/kernel.o
    DEPENDS ${CMAKE_SOURCE_DIR}/src/kernel/kernel.c
    COMMENT "Compiling kernel.c..."
)

# idt.c
add_custom_command(
    OUTPUT ${BUILD_DIR}/idt.o
    COMMAND ${CMAKE_C_COMPILER} ${CMAKE_C_FLAGS_INIT} -c ${CMAKE_SOURCE_DIR}/src/kernel/idt.c -o ${BUILD_DIR}/idt.o
    DEPENDS ${CMAKE_SOURCE_DIR}/src/kernel/idt.c
    COMMENT "Compiling idt.c..."
)

# gdt.c
add_custom_command(
    OUTPUT ${BUILD_DIR}/gdt.o
    COMMAND ${CMAKE_C_COMPILER} ${CMAKE_C_FLAGS_INIT} -c ${CMAKE_SOURCE_DIR}/src/kernel/gdt.c -o ${BUILD_DIR}/gdt.o
    DEPENDS ${CMAKE_SOURCE_DIR}/src/kernel/gdt.c
    COMMENT "Compiling gdt.c..."
)

# pmm.c
add_custom_command(
    OUTPUT ${BUILD_DIR}/pmm.o
    COMMAND ${CMAKE_C_COMPILER} ${CMAKE_C_FLAGS_INIT} -c ${CMAKE_SOURCE_DIR}/src/kernel/mm/pmm.c -o ${BUILD_DIR}/pmm.o
    DEPENDS ${CMAKE_SOURCE_DIR}/src/kernel/mm/pmm.c
    COMMENT "Compiling pmm.c..."
)

# vmm.c
add_custom_command(
    OUTPUT ${BUILD_DIR}/vmm.o
    COMMAND ${CMAKE_C_COMPILER} ${CMAKE_C_FLAGS_INIT} -c ${CMAKE_SOURCE_DIR}/src/kernel/mm/vmm.c -o ${BUILD_DIR}/vmm.o
    DEPENDS ${CMAKE_SOURCE_DIR}/src/kernel/mm/vmm.c
    COMMENT "Compiling vmm.c..."
)

# scheduler.c
add_custom_command(
    OUTPUT ${BUILD_DIR}/scheduler.o
    COMMAND ${CMAKE_C_COMPILER} ${CMAKE_C_FLAGS_INIT} -c ${CMAKE_SOURCE_DIR}/src/kernel/sched/scheduler.c -o ${BUILD_DIR}/scheduler.o
    DEPENDS ${CMAKE_SOURCE_DIR}/src/kernel/sched/scheduler.c
    COMMENT "Compiling scheduler.c..."
)

# syscall.c
add_custom_command(
    OUTPUT ${BUILD_DIR}/syscall.o
    COMMAND ${CMAKE_C_COMPILER} ${CMAKE_C_FLAGS_INIT} -c ${CMAKE_SOURCE_DIR}/src/kernel/ipc/syscall.c -o ${BUILD_DIR}/syscall.o
    DEPENDS ${CMAKE_SOURCE_DIR}/src/kernel/ipc/syscall.c
    COMMENT "Compiling syscall.c..."
)

# ============================================================
# 3. 链接内核 (ELF)
# ============================================================
add_custom_command(
    OUTPUT ${BUILD_DIR}/kernel.elf
    COMMAND ${CMAKE_LINKER} ${CMAKE_EXE_LINKER_FLAGS_INIT}
            -o ${BUILD_DIR}/kernel.elf
            ${BUILD_DIR}/entry.o
            ${BUILD_DIR}/isr.o
            ${BUILD_DIR}/gdt_flush.o
            ${BUILD_DIR}/context_switch.o
            ${BUILD_DIR}/kernel.o
            ${BUILD_DIR}/idt.o
            ${BUILD_DIR}/gdt.o
            ${BUILD_DIR}/pmm.o
            ${BUILD_DIR}/vmm.o
            ${BUILD_DIR}/scheduler.o
            ${BUILD_DIR}/syscall.o
    DEPENDS
        ${BUILD_DIR}/entry.o
        ${BUILD_DIR}/isr.o
        ${BUILD_DIR}/gdt_flush.o
        ${BUILD_DIR}/context_switch.o
        ${BUILD_DIR}/kernel.o
        ${BUILD_DIR}/idt.o
        ${BUILD_DIR}/gdt.o
        ${BUILD_DIR}/pmm.o
        ${BUILD_DIR}/vmm.o
        ${BUILD_DIR}/scheduler.o
        ${BUILD_DIR}/syscall.o
        ${CMAKE_SOURCE_DIR}/src/kernel/linker.ld
    COMMENT "Linking kernel..."
)

# ============================================================
# 4. 提取内核二进制
# ============================================================
add_custom_command(
    OUTPUT ${BUILD_DIR}/kernel.bin
    COMMAND objcopy -O binary ${BUILD_DIR}/kernel.elf ${BUILD_DIR}/kernel.bin
    DEPENDS ${BUILD_DIR}/kernel.elf
    COMMENT "Extracting kernel binary..."
)

# ============================================================
# 5. 生成磁盘镜像 (1.44MB 软盘)
# ============================================================
add_custom_command(
    OUTPUT ${BUILD_DIR}/openos.img
    COMMAND dd if=/dev/zero of=${BUILD_DIR}/openos.img bs=512 count=2880
    COMMAND dd if=${BUILD_DIR}/boot.bin of=${BUILD_DIR}/openos.img bs=512 count=1 conv=notrunc
    COMMAND dd if=${BUILD_DIR}/kernel.bin of=${BUILD_DIR}/openos.img bs=512 seek=1 conv=notrunc
    DEPENDS
        ${BUILD_DIR}/boot.bin
        ${BUILD_DIR}/kernel.bin
    COMMENT "Creating disk image..."
)

# ============================================================
# 主目标: openos.img
# ============================================================
add_custom_target(openos ALL
    DEPENDS ${BUILD_DIR}/openos.img
)

# ============================================================
# 运行目标
# ============================================================
add_custom_target(run
    COMMAND qemu-system-x86_64 -drive format=raw,file=${BUILD_DIR}/openos.img -m 512M -vga std
    DEPENDS openos
    COMMENT "Running openos in QEMU..."
)

add_custom_target(run-debug
    COMMAND qemu-system-x86_64 -drive format=raw,file=${BUILD_DIR}/openos.img -m 512M -vga std -d cpu_reset -D ${BUILD_DIR}/qemu.log
    DEPENDS openos
    COMMENT "Running openos in QEMU (debug mode)..."
)

# ============================================================
# 清理
# ============================================================
add_custom_target(clean
    COMMAND rm -rf ${BUILD_DIR}/* ${BUILD_DIR}/qemu.log
    COMMENT "Cleaning build directory..."
)

add_custom_target(distclean
    COMMAND rm -rf ${BUILD_DIR} ${ISO_DIR}
    COMMENT "Removing all generated files..."
)