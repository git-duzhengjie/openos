/* ============================================================
 * openos - 链接脚本 (Linker Script)
 * 定义内核的内存布局
 * ============================================================ */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    /* 内核加载地址 (0x8000) */
    . = 0x8000;

    /* 代码段 - entry.o必须最先链接 */
    .text : AT(ADDR(.text))
    {
        *(.entry.text)     /* entry.asm的_start入口 */
        *(.text*)
        *(.gnu.linkonce.t*)
    }

    /* 只读数据段 */
    .rodata : AT(ADDR(.rodata))
    {
        *(.rodata*)
        *(.gnu.linkonce.r*)
    }

    /* 数据段 */
    .data : AT(ADDR(.data))
    {
        *(.data*)
        *(.gnu.linkonce.d*)
    }

    /* BSS段 (未初始化数据) */
    .bss : AT(ADDR(.bss))
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        *(.gnu.linkonce.b*)
        __bss_end = .;
    }

    /* 内核结束地址 */
    __kernel_end = .;
}
