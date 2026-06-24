/* ============================================================
 * openos - 链接脚本 (Linker Script)
 * Phase 2: 支持多模块内核
 * ============================================================ */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);   /* R + X */
    rodata PT_LOAD FLAGS(4); /* R */
    data PT_LOAD FLAGS(6);   /* R + W */
    bss PT_LOAD FLAGS(6);    /* R + W */
}

SECTIONS
{
    /* 内核高地址加载地址: 1 MiB */
    . = 0x100000;

    /* === 代码段 === */
    .text : AT(ADDR(.text))
    {
        *(.entry.text)       /* entry.asm 入口点 */
        *(.text*)
        *(.gnu.linkonce.t*)
    } :text = 0x90

    /* === 只读数据段 === */
    .rodata : AT(ADDR(.rodata))
    {
        *(.rodata*)
        *(.rodata.str1.*)
        *(.gnu.linkonce.r*)
    } :rodata

    /* === 数据段 === */
    .data : AT(ADDR(.data))
    {
        *(.data*)
        *(.gnu.linkonce.d*)
    } :data

    . = ALIGN(4096);
    __kernel_image_end = .;

    /* === BSS 段 === */
    .bss (NOLOAD) : AT(ADDR(.bss))
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        *(.gnu.linkonce.b*)
        . = ALIGN(16);
        kernel_stack_bottom = .;
        . += 16K;
        kernel_stack_top = .;
        . = ALIGN(4096);
        __bss_end = .;
    } :bss

    /* 结束地址 (调试用) */
    __kernel_end = .;

    ASSERT(__kernel_image_end <= 0x300000, "kernel image exceeds bootloader high-memory load window")
    ASSERT(kernel_stack_bottom >= 0x100000, "kernel stack below high-memory area")
}
