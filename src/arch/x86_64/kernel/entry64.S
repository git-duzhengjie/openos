/*
 * x86_64 kernel entry point.
 *
 * UEFI loader jumps to phys 0x00200000 (segment paddr) and that maps to
 * vaddr 0xFFFFFFFF80200000 in the linked image.  Since the linker resolved
 * every RIP-relative reference against vaddr, we must install a page table
 * that maps the higher-half virtual range before any such reference fires.
 *
 * Layout we build in .bss (4 KiB each):
 *   pml4[0]     -> pdpt          (so we keep running on the identity map
 *                                  while we switch CR3)
 *   pml4[511]   -> pdpt          (higher-half: vaddr 0xFFFFFFFF80000000..)
 *   pdpt[0]     -> pd            (identity, vaddr 0..1 GiB)
 *   pdpt[510]   -> pd            (higher-half, vaddr 0xFFFFFFFF80000000..1 GiB)
 *   pd[0..511]: phys = i*2 MiB, P|RW|PS  (2 MiB pages)
 *
 * 2 MiB pages instead of 1 GiB pages because QEMU's default CPU model does
 * NOT advertise CPUID.80000001H:EDX.PDPE1GB (bit 26); using PS=1 in a PDPT
 * entry on such a CPU yields a reserved-bit page fault, then triple-fault.
 * Every long-mode CPU supports 2 MiB pages, so this is universally safe.
 */

.section .text.entry,"ax"
.global _start64
.extern kernel_main64_with_handoff

.section .bss
.align 4096
boot64_pml4:    .skip 4096
boot64_pdpt_hi: .skip 4096
boot64_pd_hi:   .skip 4096
boot64_pdpt_lo: .skip 4096
boot64_pd_lo0:  .skip 4096          /* identity 0..1 GiB */
boot64_pd_lo1:  .skip 4096          /* identity 1..2 GiB */
boot64_pd_lo2:  .skip 4096          /* identity 2..3 GiB */
boot64_pd_lo3:  .skip 4096          /* identity 3..4 GiB */

.section .text.entry,"ax"
_start64:
    cli

    /* PROBE '1': entry hit (still on UEFI identity map, safe to OUT). */
    movw $0x3F8, %dx
    movb $'1', %al
    outb %al, %dx

    /* UEFI loader (mingw, ms_abi) passes the handoff in %rcx.  Move to
     * %rdi for the System V ABI used by the rest of the kernel. */
    movq %rcx, %rdi
    movq %rdi, %r15

    /* Determine the runtime KERNEL_VIRT_OFFSET = vaddr(_start64) - phys(RIP).
     *
     * vaddr(_start64) is known at link time (0xFFFFFFFF80000000), but the
     * actual phys load address is whatever UEFI's AllocatePages handed back
     * (which is NOT guaranteed to equal the ELF p_paddr if AllocateAddress
     * failed and we fell back to AnyPages).  We must compute the offset
     * dynamically. */
    call _get_rip
_get_rip:
    popq %r9                            /* r9 = phys address of _get_rip label */
    movabsq $_get_rip, %rax             /* rax = vaddr of _get_rip (link-time) */
    subq %r9, %rax                      /* rax = KERNEL_VIRT_OFFSET (vaddr - phys) */
    movq %rax, %r10                     /* r10 stays as the offset for the rest of _start64 */

    leaq boot64_pml4(%rip), %rax
    subq %r10, %rax
    movq %rax, %r11                     /* r11 = phys(pml4) */

    leaq boot64_pdpt_hi(%rip), %rax
    subq %r10, %rax
    movq %rax, %r12                     /* r12 = phys(pdpt_hi) */

    leaq boot64_pd_hi(%rip), %rax
    subq %r10, %rax
    movq %rax, %r13                     /* r13 = phys(pd_hi)   */

    leaq boot64_pdpt_lo(%rip), %rax
    subq %r10, %rax
    movq %rax, %r14                     /* r14 = phys(pdpt_lo) */

    /* Phys of identity PDs (rbx points to pd_lo0; pd_lo1..3 follow at
     * fixed +4 KiB / +8 KiB / +12 KiB offsets since they are contiguous
     * .bss entries declared in order above). */
    leaq boot64_pd_lo0(%rip), %rax
    subq %r10, %rax
    movq %rax, %rbx                     /* rbx = phys(pd_lo0) */

    /* Zero pml4, pdpt_hi, pdpt_lo (pd_hi and pd_lo overwritten in full). */
    movq %r11, %rdi
    xorq %rax, %rax
    movq $512, %rcx
    rep stosq
    movq %r12, %rdi
    movq $512, %rcx
    rep stosq
    movq %r14, %rdi
    movq $512, %rcx
    rep stosq

    /* PML4[0]   = pdpt_lo | P | RW       (identity; kernel-only after P4.c)
     * PML4[511] = pdpt_hi | P | RW       (higher-half, kernel only)
     *
     * A2.P4.c (shipped after P4.c.1): seed_user_stack now biases every
     * user-visible pointer (argv/envp, RSP) through USER_VBASE (PML4[1]),
     * and fork resume restores RSP from the biased frame, so ring3 no
     * longer needs the low identity alias to walk its own stack. Drop U
     * on PML4[0] so a stray ring3 deref of a low-identity VA traps as
     * #PF instead of silently succeeding. PML4[1] (USER_VBASE) carries
     * its own U-bit chain. */
    movq %r14, %rax
    orq  $0x3, %rax
    movq %rax, 0(%r11)
    movq %r12, %rax
    orq  $0x3, %rax
    movq %rax, 4088(%r11)               /* slot 511 */

    /* pdpt_hi[510] = pd_hi | P | RW   (covers vaddr 0xFFFFFFFF80000000..1 GiB above) */
    movq %r13, %rax
    orq  $0x3, %rax
    movq %rax, 4080(%r12)               /* slot 510 */

    /* pdpt_lo[0..3] = pd_lo{0..3} | P | RW | U   (identity 0..4 GiB; U for ring3).
     *
     * UEFI is free to relocate us anywhere in the 32-bit phys range, so
     * cover all of it.  Each PDPT entry maps 1 GiB, so we need 4 distinct
     * PDs. */
    movq %rbx, %rax                     /* pd_lo0 */
    orq  $0x7, %rax
    movq %rax, 0(%r14)

    movq %rbx, %rax
    addq $0x1000, %rax                  /* pd_lo1 = pd_lo0 + 4 KiB */
    orq  $0x7, %rax
    movq %rax, 8(%r14)

    movq %rbx, %rax
    addq $0x2000, %rax                  /* pd_lo2 */
    orq  $0x7, %rax
    movq %rax, 16(%r14)

    movq %rbx, %rax
    addq $0x3000, %rax                  /* pd_lo3 */
    orq  $0x7, %rax
    movq %rax, 24(%r14)

    /* Fill pd_hi[i] = (kernel_phys_base + i * 2 MiB) | P | RW | PS, for i in 0..511.
     *
     * kernel_phys_base is the 2 MiB-aligned phys address backing vaddr
     * 0xFFFFFFFF80000000.  At link time this would be 0x200000, but if UEFI
     * relocated us elsewhere it could be e.g. 0x80200000.  Recompute now:
     *   phys(_start64) = vaddr(_start64) - r10
     * Since _start64 sits at vaddr 0xFFFFFFFF80000000, phys(_start64) is
     * already the base we want (it is 2 MiB aligned thanks to the linker
     * script's page alignment of the .text segment). */
    movabsq $_start64, %rax
    subq %r10, %rax                     /* rax = phys(_start64) = kernel_phys_base */
    andq $-0x200000, %rax               /* belt-and-suspenders 2 MiB align */

    /* Stash kernel_phys_base into a global so C code (vmm64.c) can recompute
     * virt<->phys translations consistently with the boot trampoline.  We do
     * this _before_ folding in the page-table flag bits below.  Use a
     * RIP-relative store while we are still executing at the low identity
     * mapping; the symbol resolves to its higher-half vaddr but the active
     * CR3 already covers both. */
    movq %rax, kernel64_phys_base(%rip)

    orq  $0x83, %rax                    /* P | RW | PS */

    movq %r13, %rdi
    movq $512, %rcx
1:  movq %rax, (%rdi)
    addq $0x200000, %rax                /* +2 MiB */
    addq $8, %rdi
    decq %rcx
    jnz 1b

    /* Fill pd_lo0..pd_lo3: contiguous identity map of phys 0..4 GiB,
     * 2 MiB entries.  Total of 4*512 = 2048 entries.  Since pd_lo0..3 are
     * declared contiguously in .bss, we just stream from rbx for 2048
     * iterations.  Leaf flags: P|RW|U|PS so ring3 can read/write/exec
     * anywhere in low 4 GiB (we have no SMEP/SMAP enabled). */
    movq %rbx, %rdi
    movq $0x00000087, %rax              /* P | RW | U | PS, phys=0 */
    movq $2048, %rcx
2:  movq %rax, (%rdi)
    addq $0x200000, %rax                /* +2 MiB */
    addq $8, %rdi
    decq %rcx
    jnz 2b

    /* Switch CR3.  Our identity map covers phys [0..1 GiB) including the
     * RIP we're currently executing from (phys ~0x200xxx), so we survive. */
    movq %r11, %cr3

    /* PROBE '2': CR3 loaded successfully. */
    movw $0x3F8, %dx
    movb $'2', %al
    outb %al, %dx

    /* Trampoline to higher half by absolute jump so every subsequent
     * RIP-relative reference resolves to a vaddr in our new mapping. */
    movabsq $3f, %rax
    jmpq *%rax

3:
    /* PROBE '3' (now executing at vaddr ~0xFFFFFFFF8020....). */
    movw $0x3F8, %dx
    movb $'3', %al
    outb %al, %dx
    movb $'\n', %al
    outb %al, %dx

    /* Set up stack (kernel64_stack_top is also a vaddr symbol) and call
     * the C entry point with the handoff pointer we stashed in %r15. */
    leaq kernel64_stack_top(%rip), %rsp
    andq $-16, %rsp
    movq %r15, %rdi
    xorq %rbp, %rbp
    call kernel_main64_with_handoff

4:  hlt
    jmp 4b

.section .bss,"aw",@nobits
.global kernel64_phys_base
.align 8
kernel64_phys_base:
    .quad 0
