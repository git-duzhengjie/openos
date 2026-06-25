/*
 * OpenOS aarch64 QEMU virt minimal boot path.
 *
 * QEMU virt raw-kernel entry:
 *   x0 = device tree blob physical address
 *   x1..x3 = reserved
 */

.section .text.boot, "ax"
.global _start
.type _start, %function
_start:
    mrs x20, CurrentEL
    lsr x20, x20, #2
    mov x19, x0                  /* preserve DTB base */

    cmp x20, #2
    b.ne 1f
    msr SPSel, #1
    adr x0, 1f
    msr elr_el2, x0
    mov x0, #0x3c5               /* EL1h, DAIF masked */
    msr spsr_el2, x0
    eret

1:
    ldr x0, =__stack_top
    mov sp, x0

    ldr x0, =__bss_start
    ldr x1, =__bss_end
2:
    cmp x0, x1
    b.hs 3f
    str xzr, [x0], #8
    b 2b

3:
    mov x0, x19
    bl aarch64_kernel_main

4:
    wfe
    b 4b

.size _start, . - _start
