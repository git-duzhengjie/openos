ENTRY(_start)

SECTIONS
{
    . = 0;

    .text ALIGN(0x1000) : {
        *(.text*)
    }

    .rdata ALIGN(0x1000) : {
        *(.rdata*)
        *(.rodata*)
    }

    .data ALIGN(0x1000) : {
        *(.data*)
    }

    .bss ALIGN(0x1000) : {
        *(COMMON)
        *(.bss*)
    }
}
