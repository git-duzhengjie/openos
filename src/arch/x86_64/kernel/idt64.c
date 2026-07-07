#include "../include/gdt64.h"
#include "../include/idt64.h"
#include "../include/early_console64.h"

#include <stdbool.h>

/*
 * Step G.x: post-EXIT kernel-fault sentry state.
 *
 * Any CPU exception (vector 0..31) taken while frame->cs encodes a ring0
 * selector bumps these counters. The very first such hit records vector,
 * error code, RIP, RSP so a regression dumps actionable state instead of
 * the usual silent triple-fault reset.
 *
 * All updates happen with interrupts disabled (we are inside an exception
 * handler), and there is exactly one BSP path for the ring3-drop selftest
 * that samples them, so plain volatile reads/writes are sufficient — no
 * locking needed yet. When AP exception delivery starts firing we will
 * revisit this with a per-CPU split.
 */
static volatile struct x86_64_kernel_fault_snapshot s_kfault = { 0 };

/* G.7g-2: NMI delivery counter — file-scope so stage 23 can sample it.
 * Updated only inside the exception handler (interrupts disabled), but
 * read from C selftest code with IRQs on, so keep it volatile. */
static volatile uint64_t g_nmi_count = 0;

uint64_t arch_x86_64_idt_nmi_count(void)
{
    return g_nmi_count;
}

/*
 * G.3b-3: single-shot recoverable #UD probe.
 *
 *   armed       — 0 = disarmed, 1 = waiting for a matching fault
 *   expected_rip— RIP that the dispatcher must see on entry to count as a hit
 *   insn_len    — bytes to advance frame->rip by (e.g. 2 for ud2)
 *   count       — number of hits accepted so far (monotonic)
 *
 * Lives in BSS; the BSP arms / disarms with IRQs off, and the dispatcher runs
 * with IRQs off (CPU clears IF on exception entry through an interrupt gate),
 * so plain volatile access is sufficient.
 */
static volatile uint32_t g_ud_probe_armed   = 0;
static volatile uint32_t g_ud_probe_insnlen = 0;
static volatile uint64_t g_ud_probe_rip     = 0;
static volatile uint64_t g_ud_probe_count   = 0;

void arch_x86_64_idt_arm_ud_probe(uint64_t expected_rip, uint32_t insn_len)
{
    /* Order: write the payload before flipping the armed flag, so a fault
     * racing with the arm sequence cannot observe armed=1 with a stale RIP. */
    g_ud_probe_rip     = expected_rip;
    g_ud_probe_insnlen = insn_len;
    __asm__ __volatile__("" ::: "memory");
    g_ud_probe_armed   = 1u;
}

void arch_x86_64_idt_disarm_ud_probe(void)
{
    g_ud_probe_armed   = 0u;
    __asm__ __volatile__("" ::: "memory");
    g_ud_probe_rip     = 0;
    g_ud_probe_insnlen = 0;
}

uint64_t arch_x86_64_idt_ud_probe_count(void)
{
    return g_ud_probe_count;
}

int arch_x86_64_idt_ud_probe_is_armed(void)
{
    return (int)g_ud_probe_armed;
}

/*
 * G.3b-4: same shape as the #UD probe, plus a CR2 match. Faulting linear
 * address (CR2) is read from the CPU at exception entry, so we let the
 * dispatcher capture it and compare it against the armed value.
 */
static volatile uint32_t g_pf_probe_armed   = 0;
static volatile uint32_t g_pf_probe_insnlen = 0;
static volatile uint64_t g_pf_probe_rip     = 0;
static volatile uint64_t g_pf_probe_cr2     = 0;
static volatile uint64_t g_pf_probe_count   = 0;

void arch_x86_64_idt_arm_pf_probe(uint64_t expected_rip, uint32_t insn_len, uint64_t expected_cr2)
{
    g_pf_probe_rip     = expected_rip;
    g_pf_probe_cr2     = expected_cr2;
    g_pf_probe_insnlen = insn_len;
    __asm__ __volatile__("" ::: "memory");
    g_pf_probe_armed   = 1u;
}

void arch_x86_64_idt_disarm_pf_probe(void)
{
    g_pf_probe_armed   = 0u;
    __asm__ __volatile__("" ::: "memory");
    g_pf_probe_rip     = 0;
    g_pf_probe_cr2     = 0;
    g_pf_probe_insnlen = 0;
}

uint64_t arch_x86_64_idt_pf_probe_count(void)
{
    return g_pf_probe_count;
}

int arch_x86_64_idt_pf_probe_is_armed(void)
{
    return (int)g_pf_probe_armed;
}

/*
 * G.3b-5: single-shot recoverable #GP probe. Same structure as the #UD probe
 * (no aux value needed — the precise RIP match is the strict gate). On hit
 * we disarm, bump the counter, and advance frame->rip by insn_len so iretq
 * resumes past the faulting instruction.
 */
static volatile uint32_t g_gp_probe_armed   = 0;
static volatile uint32_t g_gp_probe_insnlen = 0;
static volatile uint64_t g_gp_probe_rip     = 0;
static volatile uint64_t g_gp_probe_count   = 0;

void arch_x86_64_idt_arm_gp_probe(uint64_t expected_rip, uint32_t insn_len)
{
    g_gp_probe_rip     = expected_rip;
    g_gp_probe_insnlen = insn_len;
    __asm__ __volatile__("" ::: "memory");
    g_gp_probe_armed   = 1u;
}

void arch_x86_64_idt_disarm_gp_probe(void)
{
    g_gp_probe_armed   = 0u;
    __asm__ __volatile__("" ::: "memory");
    g_gp_probe_rip     = 0;
    g_gp_probe_insnlen = 0;
}

uint64_t arch_x86_64_idt_gp_probe_count(void)
{
    return g_gp_probe_count;
}

int arch_x86_64_idt_gp_probe_is_armed(void)
{
    return (int)g_gp_probe_armed;
}

/*
 * G.3b-6: single-shot recoverable #DE probe. Same structure as the #UD/#GP
 * probes (no aux value — precise RIP match is the strict gate). #DE is a
 * fault so the hardware-pushed RIP points to the faulting div/idiv itself;
 * on hit we disarm, bump the counter, and advance frame->rip by insn_len so
 * iretq resumes past the faulting instruction.
 */
static volatile uint32_t g_de_probe_armed   = 0;
static volatile uint32_t g_de_probe_insnlen = 0;
static volatile uint64_t g_de_probe_rip     = 0;
static volatile uint64_t g_de_probe_count   = 0;

void arch_x86_64_idt_arm_de_probe(uint64_t expected_rip, uint32_t insn_len)
{
    g_de_probe_rip     = expected_rip;
    g_de_probe_insnlen = insn_len;
    __asm__ __volatile__("" ::: "memory");
    g_de_probe_armed   = 1u;
}

void arch_x86_64_idt_disarm_de_probe(void)
{
    g_de_probe_armed   = 0u;
    __asm__ __volatile__("" ::: "memory");
    g_de_probe_rip     = 0;
    g_de_probe_insnlen = 0;
}

uint64_t arch_x86_64_idt_de_probe_count(void)
{
    return g_de_probe_count;
}

int arch_x86_64_idt_de_probe_is_armed(void)
{
    return (int)g_de_probe_armed;
}

/*
 * G.3b-7: single-shot recoverable #BP (int3) probe globals.
 *
 * #BP is a TRAP, not a fault: the hardware-pushed RIP already points to the
 * byte AFTER the int3 (0xCC). We therefore arm with the post-int3 RIP and
 * compare that directly; on hit we do NOT advance frame->rip (the trap
 * already did that for us), we just disarm and bump the counter.
 */
static volatile uint32_t g_bp_probe_armed   = 0;
static volatile uint64_t g_bp_probe_rip     = 0;
static volatile uint64_t g_bp_probe_count   = 0;

void arch_x86_64_idt_arm_bp_probe(uint64_t expected_rip_after)
{
    g_bp_probe_rip   = expected_rip_after;
    __asm__ __volatile__("" ::: "memory");
    g_bp_probe_armed = 1u;
}

void arch_x86_64_idt_disarm_bp_probe(void)
{
    g_bp_probe_armed = 0u;
    __asm__ __volatile__("" ::: "memory");
    g_bp_probe_rip   = 0;
}

uint64_t arch_x86_64_idt_bp_probe_count(void)
{
    return g_bp_probe_count;
}

int arch_x86_64_idt_bp_probe_is_armed(void)
{
    return (int)g_bp_probe_armed;
}

struct idt64_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt64_pointer {
    uint16_t limit;
    x86_64_virt_addr_t base;
} __attribute__((packed));

typedef void (*idt64_handler_t)(void);

extern void x86_64_isr0(void);
extern void x86_64_isr1(void);
extern void x86_64_isr2(void);
extern void x86_64_isr3(void);
extern void x86_64_isr4(void);
extern void x86_64_isr5(void);
extern void x86_64_isr6(void);
extern void x86_64_isr7(void);
extern void x86_64_isr8(void);
extern void x86_64_isr9(void);
extern void x86_64_isr10(void);
extern void x86_64_isr11(void);
extern void x86_64_isr12(void);
extern void x86_64_isr13(void);
extern void x86_64_isr14(void);
extern void x86_64_isr15(void);
extern void x86_64_isr16(void);
extern void x86_64_isr17(void);
extern void x86_64_isr18(void);
extern void x86_64_isr19(void);
extern void x86_64_isr20(void);
extern void x86_64_isr21(void);
extern void x86_64_isr22(void);
extern void x86_64_isr23(void);
extern void x86_64_isr24(void);
extern void x86_64_isr25(void);
extern void x86_64_isr26(void);
extern void x86_64_isr27(void);
extern void x86_64_isr28(void);
extern void x86_64_isr29(void);
extern void x86_64_isr30(void);
extern void x86_64_isr31(void);
extern void x86_64_int80_compat_entry(void);

static struct idt64_entry idt64[OPENOS_X86_64_IDT_ENTRY_COUNT] __attribute__((aligned(16)));
static struct idt64_pointer idt64_ptr;

static const idt64_handler_t exception_handlers[OPENOS_X86_64_EXCEPTION_COUNT] = {
    x86_64_isr0, x86_64_isr1, x86_64_isr2, x86_64_isr3,
    x86_64_isr4, x86_64_isr5, x86_64_isr6, x86_64_isr7,
    x86_64_isr8, x86_64_isr9, x86_64_isr10, x86_64_isr11,
    x86_64_isr12, x86_64_isr13, x86_64_isr14, x86_64_isr15,
    x86_64_isr16, x86_64_isr17, x86_64_isr18, x86_64_isr19,
    x86_64_isr20, x86_64_isr21, x86_64_isr22, x86_64_isr23,
    x86_64_isr24, x86_64_isr25, x86_64_isr26, x86_64_isr27,
    x86_64_isr28, x86_64_isr29, x86_64_isr30, x86_64_isr31
};

static uint8_t exception_ist(uint8_t vector) {
    if (vector == 2u) {
        return 2u; /* NMI */
    }
    if (vector == 8u) {
        return 1u; /* Double fault */
    }
    if (vector == 18u) {
        return 3u; /* Machine check */
    }
    return 0u;
}

static void set_idt64_gate(uint8_t vector, idt64_handler_t handler, uint8_t ist, uint8_t type_attr) {
    x86_64_entry_t offset = (x86_64_entry_t)(uintptr_t)handler;
    struct idt64_entry *entry = &idt64[vector];

    entry->offset_low = (uint16_t)(offset & 0xFFFFu);
    entry->selector = OPENOS_X86_64_GDT_KERNEL_CODE;
    entry->ist = ist & 0x07u;
    entry->type_attr = type_attr;
    entry->offset_mid = (uint16_t)((offset >> 16) & 0xFFFFu);
    entry->offset_high = (uint32_t)((offset >> 32) & 0xFFFFFFFFu);
    entry->reserved = 0;
}

void arch_x86_64_idt_init(void) {
    uint32_t i;

    for (i = 0; i < OPENOS_X86_64_IDT_ENTRY_COUNT; ++i) {
        idt64[i].offset_low = 0;
        idt64[i].selector = 0;
        idt64[i].ist = 0;
        idt64[i].type_attr = 0;
        idt64[i].offset_mid = 0;
        idt64[i].offset_high = 0;
        idt64[i].reserved = 0;
    }

    for (i = 0; i < OPENOS_X86_64_EXCEPTION_COUNT; ++i) {
        uint8_t gate_type = (i == 3u || i == 4u) ? OPENOS_X86_64_IDT_TRAP_GATE : OPENOS_X86_64_IDT_INTERRUPT_GATE;
        set_idt64_gate((uint8_t)i, exception_handlers[i], exception_ist((uint8_t)i), gate_type);
    }

    set_idt64_gate(0x80u, x86_64_int80_compat_entry, 0u, (uint8_t)(OPENOS_X86_64_IDT_TRAP_GATE | 0x60u));

    idt64_ptr.limit = (uint16_t)(sizeof(idt64) - 1u);
    idt64_ptr.base = (x86_64_virt_addr_t)(uintptr_t)&idt64[0];
    __asm__ __volatile__("lidt %0" : : "m"(idt64_ptr) : "memory");
}

/*
 * Step G.6.1: load the (already-initialized) global IDTR on the calling CPU.
 * Intended for APs after they have established their own GDT/TSS — no table
 * mutation, just a bare lidt of the shared idt64_ptr.
 */
void arch_x86_64_idt_load_ap(void) {
    __asm__ __volatile__("lidt %0" : : "m"(idt64_ptr) : "memory");
}

void arch_x86_64_idt_print_status(void) {
    early_console64_write("[x86_64][idt] entries=");
    early_console64_write_hex64(OPENOS_X86_64_IDT_ENTRY_COUNT);
    early_console64_write(" exceptions=");
    early_console64_write_hex64(OPENOS_X86_64_EXCEPTION_COUNT);
    early_console64_write(" int80_dpl=3 base=");
    early_console64_write_hex64(idt64_ptr.base);
    early_console64_write(" limit=");
    early_console64_write_hex64(idt64_ptr.limit);
    early_console64_write("\n");
}

int arch_x86_64_idt_query_gate(uint8_t vector, struct x86_64_idt_gate_info *out) {
    /*
     * Step F.1: non-destructive read-back path for the IDT selftest. We
     * reassemble the 64-bit handler offset from the three descriptor halves
     * and forward selector/IST/type_attr verbatim. No lidt, no entry
     * mutation — the caller decides what a "valid" gate looks like.
     */
    if (!out) {
        return 1;
    }
    const struct idt64_entry *entry = &idt64[vector];
    uint64_t lo = (uint64_t)entry->offset_low;
    uint64_t mid = (uint64_t)entry->offset_mid << 16;
    uint64_t hi = (uint64_t)entry->offset_high << 32;
    out->offset = lo | mid | hi;
    out->selector = entry->selector;
    out->ist = (uint8_t)(entry->ist & 0x07u);
    out->type_attr = entry->type_attr;
    return 0;
}

int arch_x86_64_idt_register_irq(uint8_t cpu_vector, void (*handler)(void)) {
    /*
     * Step F.2: only PIC-mapped vectors are accepted. We deliberately
     * refuse to clobber the 32 CPU-exception slots (0x00..0x1F) or the
     * legacy int 0x80 compat gate (0x80). Future IOAPIC/MSI work can grow
     * a separate registration path.
     *
     * G.6.5a: also accept the dedicated LAPIC-timer vector
     * (OPENOS_X86_64_LAPIC_TIMER_VECTOR = 0x40), used by the AP
     * per-CPU timer. Still refuse 0x80 (int 0x80 compat) and 0xFF
     * (LAPIC spurious).
     *
     * G.6.6a: also accept the cross-CPU reschedule-IPI vector 0x41,
     * sent via fixed-delivery IPI from any CPU to any other CPU.
     */
    if (handler == 0) return -1;
    bool ok = false;
    if (cpu_vector >= 0x20u && cpu_vector < 0x30u) ok = true;
    /* M2.x: MSI vectors 0x30..0x3F 供 PCI 设备（AHCI/NVMe 等）使用 */
    if (cpu_vector >= 0x30u && cpu_vector < 0x40u) ok = true;
    if (cpu_vector == 0x40u) ok = true;
    if (cpu_vector == 0x41u) ok = true;
    if (!ok) return -1;
    set_idt64_gate(cpu_vector, (idt64_handler_t)(uintptr_t)handler, 0u, OPENOS_X86_64_IDT_INTERRUPT_GATE);
    return 0;
}

void arch_x86_64_exception_dispatch(struct x86_64_exception_frame *frame) {
    /*
     * G.3b-3: recoverable #UD probe gate — runs *before* the kernel fault
     * sentry so a probed-and-resumed ud2 does NOT pollute first_rip/etc.
     *
     * Match conditions (all required):
     *   - probe is armed
     *   - vector is 6 (#UD)
     *   - frame->rip matches the armed RIP exactly (no fuzzy match — we want
     *     to catch wild #UD elsewhere, not silently swallow them)
     *
     * On hit we disarm, bump the count, and step rip past the faulting
     * instruction. iretq will then resume execution at rip+insn_len.
     */
    if (frame && g_ud_probe_armed && frame->vector == 6u &&
        (uint64_t)frame->rip == g_ud_probe_rip) {
        uint32_t step = g_ud_probe_insnlen;
        g_ud_probe_armed = 0u;
        ++g_ud_probe_count;
        frame->rip = (x86_64_entry_t)((uint64_t)frame->rip + step);
        return;
    }

    /*
     * G.3b-4: recoverable #PF probe gate. Same single-shot pattern as the
     * #UD probe, but additionally matches CR2 (the faulting linear address)
     * to make sure we never silently swallow an unrelated page fault.
     *
     * NOTE: we intentionally do NOT install any mapping for cr2. If the
     * faulting instruction re-executes after we return, it will fault
     * again — the probe is a one-shot "this exact load/store is allowed
     * to fault here, please skip past it" mechanism, used by the selftest
     * to prove the dispatcher can resume from a synchronous #PF.
     */
    if (frame && g_pf_probe_armed && frame->vector == 14u &&
        (uint64_t)frame->rip == g_pf_probe_rip) {
        uint64_t cr2_now = 0;
        __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2_now));
        if (cr2_now == g_pf_probe_cr2) {
            uint32_t step = g_pf_probe_insnlen;
            g_pf_probe_armed = 0u;
            ++g_pf_probe_count;
            frame->rip = (x86_64_entry_t)((uint64_t)frame->rip + step);
            return;
        }
    }

    /*
     * G.3b-5: single-shot recoverable #GP probe. Strict RIP match (no aux
     * value: the selftest's selector-load reproducer is precise enough).
     * Disarm + bump + skip past the faulting insn, NO kernel-fault sentry
     * pollution. Mismatched #GP falls through to the fatal kernel path.
     */
    if (frame && g_gp_probe_armed && frame->vector == 13u &&
        (uint64_t)frame->rip == g_gp_probe_rip) {
        uint32_t step = g_gp_probe_insnlen;
        g_gp_probe_armed = 0u;
        ++g_gp_probe_count;
        frame->rip = (x86_64_entry_t)((uint64_t)frame->rip + step);
        return;
    }

    /*
     * G.3b-6: single-shot recoverable #DE probe. Strict RIP match (no aux
     * value: the selftest's divl %ecx (with %ecx=0) reproducer is precise
     * enough). Disarm + bump + skip past the faulting div, NO kernel-fault
     * sentry pollution. Mismatched #DE falls through to the fatal path.
     */
    if (frame && g_de_probe_armed && frame->vector == 0u &&
        (uint64_t)frame->rip == g_de_probe_rip) {
        uint32_t step = g_de_probe_insnlen;
        g_de_probe_armed = 0u;
        ++g_de_probe_count;
        frame->rip = (x86_64_entry_t)((uint64_t)frame->rip + step);
        return;
    }

    /*
     * G.3b-7: single-shot recoverable #BP (int3, vector 3) probe.
     *
     * #BP is a TRAP: the hardware-pushed RIP already points to the byte
     * AFTER the int3 instruction. The probe arms with the post-int3 RIP
     * and matches it directly; on hit we do NOT advance frame->rip (the
     * trap already did), we just disarm + bump and return so iretq
     * resumes naturally. NO kernel-fault sentry pollution. Mismatched
     * #BP falls through to the fatal path.
     */
    if (frame && g_bp_probe_armed && frame->vector == 3u &&
        (uint64_t)frame->rip == g_bp_probe_rip) {
        g_bp_probe_armed = 0u;
        ++g_bp_probe_count;
        return;
    }

    /*
     * Step G.x: bump the ring0 fault sentry *before* any vector-specific
     * handling. NMI is intentionally counted as well — if NMI fires from
     * kernel code we still want a visible delta in the selftest sample.
     *
     * CS is the hardware-pushed selector. RPL bits 0..1 == 0 means the
     * faulting code was running at ring0. We also defensively reject the
     * NULL selector (frame->cs == 0) because the iret frame for a half-
     * built context can leave it cleared.
     */
    if (frame && (frame->cs & 0x3u) == 0u && frame->cs != 0u) {
        ++s_kfault.count;
        switch (frame->vector) {
            case 6:  ++s_kfault.ud_count; break;
            case 13: ++s_kfault.gp_count; break;
            case 14: ++s_kfault.pf_count; break;
            default: break;
        }
        if (s_kfault.first_rip == 0u) {
            s_kfault.first_vector = frame->vector;
            s_kfault.first_error = frame->error_code;
            s_kfault.first_rip = (uint64_t)frame->rip;
            s_kfault.first_rsp = (uint64_t)frame->rsp;
        }
    }

    /* G.3b-final: vector 2 (NMI) is recoverable in our model — it can
     * be steered to a CPU via LVT LINT1, and on PC platforms it most
     * commonly represents a chassis / watchdog event we just want to
     * log. Treat it as non-fatal so the kernel keeps running. */
    if (frame && frame->vector == 2u) {
        ++g_nmi_count;
        early_console64_write("\n[x86_64][nmi] count=");
        early_console64_write_hex64(g_nmi_count);
        early_console64_write(" rip=");
        early_console64_write_hex64((uint64_t)frame->rip);
        early_console64_write("\n");
        return;
    }

    /*
     * Step D.2: be loud about exceptions instead of silently halting. Without
     * this, a ring3 #PF on the boot page tables produces zero output and looks
     * exactly like "the user program never executed". Print enough state to
     * tell the two cases apart, then halt as before.
     */
    early_console64_write("\n[x86_64][exception] vector=");
    early_console64_write_hex64(frame ? frame->vector : 0xFFFFFFFFFFFFFFFFULL);
    if (frame) {
        uint64_t cr2 = 0;
        __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
        early_console64_write(" err=");
        early_console64_write_hex64(frame->error_code);
        early_console64_write(" rip=");
        early_console64_write_hex64((uint64_t)frame->rip);
        early_console64_write(" cs=");
        early_console64_write_hex64(frame->cs);
        early_console64_write(" rflags=");
        early_console64_write_hex64(frame->rflags);
        early_console64_write(" rsp=");
        early_console64_write_hex64((uint64_t)frame->rsp);
        early_console64_write(" ss=");
        early_console64_write_hex64(frame->ss);
        early_console64_write(" cr2=");
        early_console64_write_hex64(cr2);
    }
    early_console64_write("\n");
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}

uint64_t arch_x86_64_idt_kernel_fault_count(void) {
    return s_kfault.count;
}

uint64_t arch_x86_64_idt_kernel_ud_count(void) {
    return s_kfault.ud_count;
}

void arch_x86_64_idt_kernel_fault_snapshot(struct x86_64_kernel_fault_snapshot *out) {
    if (!out) {
        return;
    }
    out->count        = s_kfault.count;
    out->ud_count     = s_kfault.ud_count;
    out->gp_count     = s_kfault.gp_count;
    out->pf_count     = s_kfault.pf_count;
    out->first_vector = s_kfault.first_vector;
    out->first_error  = s_kfault.first_error;
    out->first_rip    = s_kfault.first_rip;
    out->first_rsp    = s_kfault.first_rsp;
}

void arch_x86_64_idt_print_kernel_fault_stats(void) {
    early_console64_write("[x86_64][kfault] ring0 exceptions total=");
    early_console64_write_hex64(s_kfault.count);
    early_console64_write(" ud=");
    early_console64_write_hex64(s_kfault.ud_count);
    early_console64_write(" gp=");
    early_console64_write_hex64(s_kfault.gp_count);
    early_console64_write(" pf=");
    early_console64_write_hex64(s_kfault.pf_count);
    early_console64_write("\n");
    if (s_kfault.count) {
        early_console64_write("[x86_64][kfault] first vec=");
        early_console64_write_hex64(s_kfault.first_vector);
        early_console64_write(" err=");
        early_console64_write_hex64(s_kfault.first_error);
        early_console64_write(" rip=");
        early_console64_write_hex64(s_kfault.first_rip);
        early_console64_write(" rsp=");
        early_console64_write_hex64(s_kfault.first_rsp);
        early_console64_write("\n");
    }
}

