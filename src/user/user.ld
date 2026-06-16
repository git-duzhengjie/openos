/* openos 用户程序链接脚本 */
/* 用户程序加载到独立用户虚拟地址空间 */

OUTPUT_FORMAT("elf32-i386")
ENTRY(_start)

SECTIONS
{
    /* 用户程序加载地址 - 避开内核低端 identity 映射 */
    . = 0x40000000;  /* 1GB */

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