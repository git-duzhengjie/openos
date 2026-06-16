.section .text.entry,"ax"
.global _start
.global arm_exception_vectors
.extern arm_kernel_main

_start:
    b arm_kernel_main

.align 5
arm_exception_vectors:
    b arm_exception_vectors
    b arm_exception_vectors
    b arm_exception_vectors
    b arm_exception_vectors
    b arm_exception_vectors
    b arm_exception_vectors
    b arm_exception_vectors
    b arm_exception_vectors
