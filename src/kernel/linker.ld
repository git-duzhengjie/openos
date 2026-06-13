/* ============================================================
 * openos - 链接脚本 (Linker Script)
 * Phase 2: 支持多模块内核
 * ============================================================ */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    /* 内核加载地址: 0x8000 */
    . = 0x8000;

    /* === 代码段 === */
    .text : AT(ADDR(.text))
    {
        *(.entry.text)       /* entry.asm 入口点 */
        *(.text*)
        *(.gnu.linkonce.t*)
    } = 0x90

    /* === 只读数据段 === */
    .rodata : AT(ADDR(.rodata))
    {
        *(.rodata*)
        *(.rodata.str1.*)
        *(.gnu.linkonce.r*)
    }

    /* === 数据段 === */
    .data : AT(ADDR(.data))
    {
        *(.data*)
        *(.gnu.linkonce.d*)
    }

    /* === BSS 段 === */
    .bss : AT(ADDR(.bss))
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        *(.gnu.linkonce.b*)
        . = ALIGN(4096);
        __bss_end = .;
    }

    /* 结束地址 (调试用) */
    __kernel_end = .;

    /* 低端内存栈顶。必须高于内核映像/BSS，且低于 EBDA/显存区域。 */
    kernel_stack_top = 0x9F000;

    ASSERT(__kernel_end < 0x9A000, "kernel image/BSS overlaps low-memory stack reserve")
    ASSERT(kernel_stack_top <= 0x9F000, "kernel stack top exceeds safe low memory")
}
