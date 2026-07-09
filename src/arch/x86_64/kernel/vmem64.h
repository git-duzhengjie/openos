#ifndef OPENOS_ARCH_X86_64_VMEM64_H
#define OPENOS_ARCH_X86_64_VMEM64_H

/*
 * vmem64 — M4.1b anonymous virtual-memory bookkeeping for the syscall layer.
 *
 * Backs the mmap / munmap / mprotect / brk / sbrk system calls. Until a ring3
 * process with its own address space is running, allocations use the
 * "kernel-backed" mode: pages are taken straight from the PMM and their
 * identity-mapped physical address doubles as the returned virtual address, so
 * the headless self-test can read/write the region for real. The ABI is frozen
 * here; wiring the mappings into a per-process address space is a later step.
 */

#include <stddef.h>
#include <stdint.h>

/* prot bits — match the classic PROT_* ABI values. */
#define OPENOS_X86_64_PROT_NONE  0x0
#define OPENOS_X86_64_PROT_READ  0x1
#define OPENOS_X86_64_PROT_WRITE 0x2
#define OPENOS_X86_64_PROT_EXEC  0x4

/* MAP_FAILED sentinel returned by mmap on error. */
#define OPENOS_X86_64_MAP_FAILED ((uint64_t)-1)

/* Reset all bookkeeping (heap + mmap regions). Called on address-space bind. */
void arch_x86_64_vmem_reset(void);

/*
 * mmap: allocate `length` bytes of anonymous memory with the given prot.
 * Returns the base virtual address, or OPENOS_X86_64_MAP_FAILED on error.
 */
uint64_t arch_x86_64_vmem_mmap(uint64_t length, int prot);

/* munmap: release a region previously returned by mmap. 0 on success, -1 on error. */
int arch_x86_64_vmem_munmap(uint64_t addr, uint64_t length);

/* mprotect: change prot bits of an existing region. 0 on success, -1 on error. */
int arch_x86_64_vmem_mprotect(uint64_t addr, uint64_t length, int prot);

/*
 * brk: set the program break to `addr`. addr==0 queries the current break.
 * Returns the resulting break address (never fails hard; clamps on ENOMEM by
 * leaving the break unchanged and returning the current value).
 */
uint64_t arch_x86_64_vmem_brk(uint64_t addr);

/*
 * sbrk: grow/shrink the heap by `delta` bytes. Returns the PREVIOUS break on
 * success, or OPENOS_X86_64_MAP_FAILED on error.
 */
uint64_t arch_x86_64_vmem_sbrk(int64_t delta);

/* Introspection for the self-test: number of live mmap regions. */
uint32_t arch_x86_64_vmem_region_count(void);

#endif /* OPENOS_ARCH_X86_64_VMEM64_H */
