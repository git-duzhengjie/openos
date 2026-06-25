/* OpenOS aarch64 QEMU virt early linker script. */
ENTRY(_start)

KERNEL_PHYS_BASE = 0x40200000;

PHDRS
{
    text PT_LOAD FLAGS(5);   /* R + X */
    rodata PT_LOAD FLAGS(4); /* R */
    data PT_LOAD FLAGS(6);   /* R + W */
}

SECTIONS
{
    . = KERNEL_PHYS_BASE;

    .text.boot : ALIGN(4K) {
        KEEP(*(.text.boot))
    } :text

    .text : ALIGN(4K) {
        *(.text .text.*)
    } :text

    .rodata : ALIGN(4K) {
        *(.rodata)
        . = ALIGN(16);
        KEEP(*(.rodata.aarch64_hello64_elf))
        . = ALIGN(16);
        *(.rodata.*)
    } :rodata

    .data : ALIGN(4K) {
        *(.data .data.*)
    } :data

    .bss : ALIGN(4K) {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        __bss_end = .;
    } :data

    . = ALIGN(16);
    __stack_bottom = .;
    . += 64K;
    __stack_top = .;

    . = ALIGN(4K);
    __kernel_end = .;
}
