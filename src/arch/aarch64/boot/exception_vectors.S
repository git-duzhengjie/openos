/*
 * OpenOS aarch64 EL1 exception vector table.
 *
 * Each architectural vector slot is 0x80 bytes. The common stubs build a
 * compact trap frame for C code, preserving x0-x18/x29/x30 plus ESR/ELR/FAR/SPSR.
 */

.section .text.exceptions, "ax"
.align 11
.global aarch64_exception_vector_table
.type aarch64_exception_vector_table, %function

aarch64_exception_vector_table:
    .macro VECTOR_SLOT id
    b aarch64_exception_vector_\id
    .org aarch64_exception_vector_table + ((\id + 1) * 0x80)
    .endm

    VECTOR_SLOT 0
    VECTOR_SLOT 1
    VECTOR_SLOT 2
    VECTOR_SLOT 3
    VECTOR_SLOT 4
    VECTOR_SLOT 5
    VECTOR_SLOT 6
    VECTOR_SLOT 7
    VECTOR_SLOT 8
    VECTOR_SLOT 9
    VECTOR_SLOT 10
    VECTOR_SLOT 11
    VECTOR_SLOT 12
    VECTOR_SLOT 13
    VECTOR_SLOT 14
    VECTOR_SLOT 15

    .macro SAVE_TRAP_FRAME id
    sub sp, sp, #208
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    str x18, [sp, #144]
    stp x29, x30, [sp, #152]
    mrs x9, esr_el1
    str x9, [sp, #168]
    mrs x9, elr_el1
    str x9, [sp, #176]
    mrs x9, far_el1
    str x9, [sp, #184]
    mrs x9, spsr_el1
    str x9, [sp, #192]
    mov x9, #\id
    str x9, [sp, #200]
    .endm

    .macro RESTORE_TRAP_FRAME
    ldr x9, [sp, #176]
    msr elr_el1, x9
    ldr x9, [sp, #192]
    msr spsr_el1, x9
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldr x18, [sp, #144]
    ldp x29, x30, [sp, #152]
    add sp, sp, #208
    .endm

    .macro EXCEPTION_STUB id
    .align 7
    .global aarch64_exception_vector_\id
    .type aarch64_exception_vector_\id, %function
aarch64_exception_vector_\id:
    SAVE_TRAP_FRAME \id
    mov x0, sp
    bl aarch64_exception_dispatch
    RESTORE_TRAP_FRAME
    eret
    .size aarch64_exception_vector_\id, . - aarch64_exception_vector_\id
    .endm

    EXCEPTION_STUB 0
    EXCEPTION_STUB 1
    EXCEPTION_STUB 2
    EXCEPTION_STUB 3
    EXCEPTION_STUB 4
    EXCEPTION_STUB 5
    EXCEPTION_STUB 6
    EXCEPTION_STUB 7
    EXCEPTION_STUB 8
    EXCEPTION_STUB 9
    EXCEPTION_STUB 10
    EXCEPTION_STUB 11
    EXCEPTION_STUB 12
    EXCEPTION_STUB 13
    EXCEPTION_STUB 14
    EXCEPTION_STUB 15

.size aarch64_exception_vector_table, . - aarch64_exception_vector_table
