# ============================================================
# openos - 构建系统 (Makefile)
# 支持：Windows (通过WSL) 和 Linux 环境
# ============================================================

# 检测是否在 WSL 中运行
ifdef WSL_DISTRO_NAME
    # 已在WSL中
    WSL_PREFIX :=
else
    # 从Windows调用WSL
    WSL_PREFIX := wsl -d Ubuntu --cd "%CD%"
endif

# 目标架构
ARCH     ?= x86_64
BITS     := 32

# 工具链
ASM      := nasm
CC       := gcc
LD       := ld
QEMU     := qemu-system-x86_64

# 编译器标志
CFLAGS   := -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
            -fno-pie -fno-stack-protector -fno-builtin \
            -I src/kernel/include

# 链接器标志
LDFLAGS  := -m elf_i386 -T src/kernel/linker.ld

# 汇编器标志
ASMFLAGS := -f elf32

# 目录
BOOT_SRC  := src/boot
KERNEL_SRC := src/kernel
BUILD_DIR := target
ISO_DIR   := iso

# 目标文件
BOOT_BIN  := $(BUILD_DIR)/boot.bin
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
OS_IMG    := $(BUILD_DIR)/openos.img

# 源文件
BOOT_SRC_FILE := $(BOOT_SRC)/boot.asm
KERNEL_C_FILES := $(KERNEL_SRC)/kernel.c $(KERNEL_SRC)/idt.c
KERNEL_ASM_FILES := $(KERNEL_SRC)/entry.asm $(KERNEL_SRC)/isr.asm

# 目标文件
KERNEL_OBJS := $(BUILD_DIR)/kernel.o $(BUILD_DIR)/entry.o $(BUILD_DIR)/idt.o $(BUILD_DIR)/isr.o

# ============================================================
# 默认目标：构建
# ============================================================
all: $(OS_IMG)

# ============================================================
# 构建引导加载程序
# ============================================================
$(BOOT_BIN): $(BOOT_SRC_FILE) | $(BUILD_DIR)
	$(WSL_PREFIX) $(ASM) -f bin $(BOOT_SRC_FILE) -o $(BOOT_BIN)

# ============================================================
# 构建内核
# ============================================================
$(BUILD_DIR)/entry.o: $(KERNEL_SRC)/entry.asm | $(BUILD_DIR)
	$(WSL_PREFIX) $(ASM) $(ASMFLAGS) $< -o $@

$(BUILD_DIR)/isr.o: $(KERNEL_SRC)/isr.asm | $(BUILD_DIR)
	$(WSL_PREFIX) $(ASM) $(ASMFLAGS) $< -o $@

$(BUILD_DIR)/kernel.o: $(KERNEL_SRC)/kernel.c | $(BUILD_DIR)
	$(WSL_PREFIX) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idt.o: $(KERNEL_SRC)/idt.c | $(BUILD_DIR)
	$(WSL_PREFIX) $(CC) $(CFLAGS) -c $< -o $@

# 链接内核
$(KERNEL_ELF): $(KERNEL_OBJS) $(KERNEL_SRC)/linker.ld
	$(WSL_PREFIX) $(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)
	$(WSL_PREFIX) objcopy -O binary $@ $(KERNEL_BIN)

# ============================================================
# 生成磁盘镜像
# ============================================================
$(OS_IMG): $(BOOT_BIN) $(KERNEL_ELF)
	$(WSL_PREFIX) dd if=/dev/zero of=$(OS_IMG) bs=512 count=2880 2>/dev/null
	$(WSL_PREFIX) dd if=$(BOOT_BIN) of=$(OS_IMG) bs=512 count=1 conv=notrunc 2>/dev/null
	$(WSL_PREFIX) dd if=$(KERNEL_BIN) of=$(OS_IMG) bs=512 seek=1 conv=notrunc 2>/dev/null
	@echo "========================================"
	@echo "  openos 构建成功!"
	@echo "  镜像: $(OS_IMG)"
	@echo "========================================"

# ============================================================
# 在QEMU中运行
# ============================================================
run: $(OS_IMG)
	$(WSL_PREFIX) $(QEMU) -drive format=raw,file=$(OS_IMG) -m 512M -vga std

run-debug: $(OS_IMG)
	$(WSL_PREFIX) $(QEMU) -drive format=raw,file=$(OS_IMG) -m 512M -vga std -d cpu_reset -D qemu.log

# ============================================================
# 创建目标目录
# ============================================================
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# ============================================================
# 清理
# ============================================================
clean:
	rm -rf $(BUILD_DIR)/* qemu.log

distclean: clean
	rm -rf $(ISO_DIR)/boot/kernel.elf

.PHONY: all clean distclean run run-debug
