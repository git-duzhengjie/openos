/* ============================================================
 * OpenOS x86_64 linker script
 *
 * This script is intentionally independent from the current i386
 * linker.ld. It provides a higher-half 64-bit kernel layout while
 * preserving a simple 2 MiB physical load base for the future long-mode
 * boot path.
 * ============================================================ */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start64)

KERNEL_PHYS_BASE = 0x00200000;
KERNEL_VIRT_BASE = 0xFFFFFFFF80000000;
KERNEL_VIRT_OFFSET = KERNEL_VIRT_BASE - KERNEL_PHYS_BASE;

SECTIONS
{
    . = KERNEL_VIRT_BASE;

    __kernel64_start = .;

    .text : AT(ADDR(.text) - KERNEL_VIRT_OFFSET)
    {
        *(.text.entry*)
        *(.text*)
        *(.gnu.linkonce.t*)
    }

    . = ALIGN(4096);
    .rodata : AT(ADDR(.rodata) - KERNEL_VIRT_OFFSET)
    {
        *(.rodata*)
        *(.rodata.str1.*)
        *(.gnu.linkonce.r*)
    }

    . = ALIGN(4096);
    .data : AT(ADDR(.data) - KERNEL_VIRT_OFFSET)
    {
        *(.data*)
        *(.gnu.linkonce.d*)
    }

    . = ALIGN(4096);
    .bss (NOLOAD) : AT(ADDR(.bss) - KERNEL_VIRT_OFFSET)
    {
        __bss64_start = .;
        *(COMMON)
        *(.bss*)
        *(.gnu.linkonce.b*)
        . = ALIGN(16);
        kernel64_stack_bottom = .;
        . += 64K;
        kernel64_stack_top = .;
        . = ALIGN(4096);
        __bss64_end = .;
    }

    __kernel64_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }

    ASSERT((KERNEL_PHYS_BASE % 0x200000) == 0, "x86_64 kernel physical base must be 2MiB aligned")
    ASSERT(kernel64_stack_top > kernel64_stack_bottom, "x86_64 kernel stack layout invalid")
}
