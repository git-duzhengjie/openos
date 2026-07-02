/* G.4.3b-2 + G.5 — x86_64 AP trampoline (v0x05)
 *
 * Full three-stage transition: real → PM32 → LM64 → C entry.
 * Loaded at physical address 0x8000, size < 512 bytes.
 *
 * Layout (offsets from base = 0x8000):
 *   +0x000: .code16 — real mode entry
 *   +0x040: .code32 — protected mode entry
 *   +0x0A0: .code64 — long mode entry
 *   +0x0C0: GDT (8 descriptors, 64 bytes)
 *   +0x100: GDTR (10 bytes)
 *   +0x10A: CR3 slot (8 bytes, backfilled)
 *   +0x112: entry point slot (8 bytes, backfilled)
 *   +0x11A: magic 'APTR' (4) + version (1) = 5 bytes
 *
 * Alive counters (physical memory, atomically incremented):
 *   0x9000 — real mode reached
 *   0x9008 — protected mode reached
 *   0x9010 — long mode reached
 *
 * Temporary stack for AP C entry: 0xC000 (512 bytes per CPU)
 *
 * Compile with gas -m64, embedded into the kernel binary as a blob.
 */

.global __ap_trampoline_blob_start
.global __ap_trampoline_blob_end

.section .ap_trampoline64_blob, "ax"
.balign 256

__ap_trampoline_blob_start:

/* ————————————————————————————————————
 * STAGE 1: 16-bit real mode entry
 * Offset: +0x000
 * ———————————————————————————————————— */
.code16
ap_real_mode_entry:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    xorw    %sp, %sp
    cld

    /* Atomic: alive[rm] += 1 */
    lock incb (0x9000)

    /* Load 32-bit GDT with data32 prefix (24-bit default in real mode) */
    data32 lgdt (gdt64_ptr - __ap_trampoline_blob_start + 0x8000)

    /* Enable protected mode */
    movl    %cr0, %eax
    orb     $0x01, %al
    movl    %eax, %cr0

    /* Far jump to 32-bit protected mode.
     * Manually encode operand-size prefix + far jmp with 32-bit offset
     * to avoid gas assembling 16-bit offset ljmp in .code16.
     * Bytes: 0x66 0xEA offset(4) selector(2)
     */
    .byte 0x66
    .byte 0xEA
    .long (ap_pm32_entry - __ap_trampoline_blob_start + 0x8000)
    .word 0x0008

/* ————————————————————————————————————
 * STAGE 2: 32-bit protected mode entry
 * Offset: +0x040
 * ———————————————————————————————————— */
.code32
ap_pm32_entry:
    /* Set up segment registers for PM32 */
    movw    $0x0010, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    xorl    %esp, %esp

    /* Atomic: alive[pm32] += 1 */
    lock incb (0x9008)

    /* G.5: real→PM32 verified, now proceed to LM64 transition. */

    /* Load CR3 backfilled by BSP (physical address of PML4) */
    movl    (0x8000 + (cr3_slot - __ap_trampoline_blob_start)), %eax
    movl    %eax, %cr3

    /* Enable PAE (required for long mode) */
    movl    %cr4, %eax
    orb     $0x20, %al
    movl    %eax, %cr4

    /* Set LME (bit 8) and NXE (bit 11) in EFER MSR.
     * EFER is read into EDX:EAX. LME|NXE = 0x100|0x800 = 0x900.
     * NOTE: `orb $0x100,%ah` would silently truncate to 0 — use a
     * word-sized OR on AX so the bits actually land at EAX bit 8/11.
     * NXE MUST be set here to match BSP (UEFI enabled NXE by default);
     * otherwise any PTE with bit63=1 triggers reserved-bit #PF on APs
     * — this bit us hard in gamma.3b-S2a child dispatch. */
    movl    $0xC0000080, %ecx
    rdmsr
    orw     $0x0900, %ax
    wrmsr

    /* Enable paging (this activates long mode) */
    movl    %cr0, %eax
    orl     $0x80000000, %eax
    movl    %eax, %cr0

    /* Far jump to 64-bit long mode */
    ljmp    $0x0018, $(ap_lm64_entry - __ap_trampoline_blob_start + 0x8000)

/* ————————————————————————————————————
 * STAGE 3: 64-bit long mode entry
 * Offset: +0x0A0
 * ———————————————————————————————————— */
.code64
ap_lm64_entry:
    /* Atomic: alive[lm64] += 1 */
    lock incb (0x9010)

    /* G.5-SSE: Enable x87/SSE on this AP before running any C code.
     * BSP gets SSE pre-enabled by UEFI firmware; APs come up cold and
     * the compiler emits SSE instructions (pxor %xmm, %xmm) for memset/
     * struct-zero, which #UD without OSFXSR. Triple-fault would result.
     * Intel SDM Vol.3A 13.1.4:
     *   CR0.MP=1, CR0.EM=0, CR0.NE=1
     *   CR4.OSFXSR=1 (bit 9), CR4.OSXMMEXCPT=1 (bit 10) */
    movq    %cr0, %rax
    andq    $~(1<<2), %rax           /* clear CR0.EM */
    orq     $((1<<1)|(1<<5)), %rax   /* set CR0.MP, CR0.NE */
    movq    %rax, %cr0
    movq    %cr4, %rax
    orq     $((1<<9)|(1<<10)), %rax  /* set CR4.OSFXSR, CR4.OSXMMEXCPT */
    movq    %rax, %cr4

    /* G.5: Per-CPU stack allocation.
     *
     * 0x9018 = 8-byte atomic CPU-index counter (init = 1, slot 0 reserved for BSP)
     * 0xA000 = 8-byte-per-slot stack-top table, filled by BSP before SIPI.
     *
     * Each AP atomically grabs its own index, then loads the corresponding
     * 64-bit stack top from the table. No two APs share a stack. */
    movq    $1, %rax
    lock xaddq %rax, (0x9018)        /* rax = old counter (this AP's index) */
    movq    (0xA000)(,%rax,8), %rsp  /* rsp = stack_top_table[rax] */
    movq    %rsp, %rbp

    /* Read APIC ID via CPUID (leaf 01h, EBX bits 31-24).
     * Per Intel SDM Vol 2A: initial Local APIC ID lives in EBX[31:24],
     * NOT EDX. (EDX holds feature flags.) */
    pushq   %rbx
    pushq   %rcx
    pushq   %rdx
    movl    $0x01, %eax
    cpuid
    shrl    $24, %ebx
    andl    $0xFF, %ebx
    movq    %rbx, %rdi  /* first argument to ap_entry() = apic_id */
    popq    %rdx
    popq    %rcx
    popq    %rbx

    /* Load entry point backfilled by BSP and jump */
    movq    (0x8000 + (entry_slot - __ap_trampoline_blob_start)), %rax
    jmp     *%rax

/* ————————————————————————————————————
 * GDT embedded in blob — position independent
 * Base address computed as 0x8000 + offset at compile time
 * ———————————————————————————————————— */
.balign 8
gdt64:
    /* 0x00: null descriptor */
    .quad 0x0000000000000000
    /* 0x08: 32-bit code (base=0, limit=4GB, G=1, D=1, P=1, S=1, DPL=0, Type=A) */
    .quad 0x00CF9A000000FFFF
    /* 0x10: 32-bit data (base=0, limit=4GB, G=1, D=1, P=1, S=1, DPL=0, Type=W) */
    .quad 0x00CF92000000FFFF
    /* 0x18: 64-bit code (L=1, D=0, P=1, S=1, DPL=0, Type=A) */
    .quad 0x00AF9A000000FFFF
    /* 0x20: 64-bit data (P=1, S=1, DPL=0, Type=W) */
    .quad 0x00AF92000000FFFF
    /* 0x28: reserved for TSS (two descriptors) */
    .quad 0x0000000000000000
    .quad 0x0000000000000000

gdt64_ptr:
    .word gdt64_ptr - gdt64 - 1   /* limit */
    .long (gdt64 - __ap_trampoline_blob_start + 0x8000)

/* ————————————————————————————————————
 * Backfill slots and magic footer
 * ———————————————————————————————————— */
.balign 8
cr3_slot:
    .quad 0x0000000000000000

entry_slot:
    .quad 0x0000000000000000

magic:
    .byte 'A', 'P', 'T', 'R'
    .byte 0x05

__ap_trampoline_blob_end:
