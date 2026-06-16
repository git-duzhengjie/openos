/* openos 用户程序链接脚本 */
/* 用户程序加载到独立用户虚拟地址空间，并拆分 W^X 段权限 */

OUTPUT_FORMAT("elf32-i386")
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5); /* R-X */
    data PT_LOAD FLAGS(6); /* RW- */
}

SECTIONS
{
    /* 用户程序加载地址 - 避开内核低端 identity 映射 */
    . = 0x40000000;  /* 1GB */

    .text : {
        *(.text*)
    } :text

    .rodata : {
        *(.rodata*)
    } :text

    . = ALIGN(0x1000);

    .data : {
        *(.data*)
    } :data

    .bss : {
        *(.bss*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.comment*)
        *(.note*)
        *(.eh_frame*)
    }
}
