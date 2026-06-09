/* openos 用户程序链接脚本 */
/* 用户程序加载地址在 identity 映射范围内 */

OUTPUT_FORMAT("elf32-i386")
ENTRY(_start)

SECTIONS
{
    /* 用户程序加载地址 - 在 identity 映射范围内 */
    . = 0x00400000;  /* 4MB */

    .text : {
        *(.text*)
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.comment*)
        *(.note*)
        *(.eh_frame*)
    }
}