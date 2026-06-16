.section .text.entry,"ax"
.global _start
.extern riscv_kernel_main

_start:
    la sp, riscv_boot_stack_top
    tail riscv_kernel_main

.section .bss.stack,"aw",@nobits
.align 12
.global riscv_boot_stack
riscv_boot_stack:
    .skip 0x4000
.global riscv_boot_stack_top
riscv_boot_stack_top:
