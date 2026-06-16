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

    . = ALIGN(4096);
    __kernel_image_end = .;

    /*
     * Runtime BSS is intentionally placed above 1 MiB. The bootloader only
     * needs to load text/rodata/data below the low-memory stack; BSS is zeroed
     * by entry.asm at runtime and does not need disk space. This keeps growing
     * kernel static state away from EBDA/VGA/temporary stack low memory.
     */
    . = 0x100000;

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
    }

    /* 结束地址 (调试用) */
    __kernel_end = .;

    /* 低端加载镜像仍不能进入 VGA/ROM 保留区域。启动栈位于高端 .bss。 */
    ASSERT(__kernel_image_end <= 0xA0000, "kernel image overlaps VGA reserved memory")
    ASSERT(kernel_stack_bottom >= 0x100000, "kernel stack below high-memory area")
}
