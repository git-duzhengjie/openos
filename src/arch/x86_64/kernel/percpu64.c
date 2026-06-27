#include "../include/percpu64.h"

#include "../include/early_console64.h"
#include "../include/gdt64.h"
#include "../include/tss64.h"

/*
 * GDT layout (8 entries x 8 bytes = 64 bytes per CPU):
 *   0x00 null
 *   0x08 kernel code (64-bit)
 *   0x10 kernel data
 *   0x18 user data
 *   0x20 user code (64-bit)
 *   0x28 user code (32-bit, compatibility)
 *   0x30 TSS low
 *   0x38 TSS high   (system descriptor occupies two 8-byte slots)
 */
#define GDT_ENTRY_COUNT 9u

#define GDT_ACCESS_PRESENT 0x80u
#define GDT_ACCESS_RING0   0x00u
#define GDT_ACCESS_RING3   0x60u
#define GDT_ACCESS_CODE    0x18u
#define GDT_ACCESS_DATA    0x10u
#define GDT_ACCESS_TSS     0x09u
#define GDT_ACCESS_RW      0x02u

#define GDT_FLAG_GRANULAR  0x8u
#define GDT_FLAG_64BIT     0x2u
#define GDT_FLAG_32BIT     0x4u

struct gdt_pointer {
    uint16_t           limit;
    x86_64_virt_addr_t base;
} __attribute__((packed));

/*
 * Per-CPU storage. Kept as plain BSS arrays so APs can populate their
 * own slot without depending on the heap (heap allocations happen on
 * the BSP before SMP bring-up, but using BSS keeps the lifetime story
 * obvious and avoids any "did the AP see the new mapping yet" concerns
 * around per-CPU pages.)
 */
static uint64_t           g_gdt[OPENOS_X86_64_PERCPU_MAX_CPUS][GDT_ENTRY_COUNT]
    __attribute__((aligned(16)));
static struct gdt_pointer g_gdt_ptr[OPENOS_X86_64_PERCPU_MAX_CPUS];
static struct x86_64_tss  g_tss[OPENOS_X86_64_PERCPU_MAX_CPUS]
    __attribute__((aligned(16)));
static uint8_t            g_rsp0_stack[OPENOS_X86_64_PERCPU_MAX_CPUS]
                                       [OPENOS_X86_64_PERCPU_RSP0_SIZE]
    __attribute__((aligned(16)));
static uint8_t            g_ist_stack[OPENOS_X86_64_PERCPU_MAX_CPUS]
                                      [OPENOS_X86_64_PERCPU_IST_COUNT]
                                      [OPENOS_X86_64_PERCPU_IST_SIZE]
    __attribute__((aligned(16)));

static uint64_t make_descriptor(uint32_t base, uint32_t limit,
                                uint8_t access, uint8_t flags) {
    uint64_t d = 0;
    d |= (uint64_t)(limit & 0xFFFFu);
    d |= (uint64_t)(base & 0xFFFFFFu) << 16;
    d |= (uint64_t)access << 40;
    d |= (uint64_t)((limit >> 16) & 0x0Fu) << 48;
    d |= (uint64_t)(flags & 0x0Fu) << 52;
    d |= (uint64_t)((base >> 24) & 0xFFu) << 56;
    return d;
}

static x86_64_stack_ptr_t stack_top(uint8_t *base, uint32_t size) {
    return (x86_64_stack_ptr_t)(uintptr_t)(base + size);
}

static void build_tss(uint32_t cpu) {
    struct x86_64_tss *tss = &g_tss[cpu];
    uint32_t           i;

    tss->reserved0 = 0;
    for (i = 0; i < OPENOS_X86_64_TSS_RSP_COUNT; ++i) {
        tss->rsp[i] = 0;
    }
    tss->reserved1 = 0;
    for (i = 0; i < OPENOS_X86_64_TSS_IST_COUNT; ++i) {
        /* Only first PERCPU_IST_COUNT IST slots are backed; the rest
         * stay zero so a stray reference faults immediately. */
        if (i < OPENOS_X86_64_PERCPU_IST_COUNT) {
            tss->ist[i] = stack_top(&g_ist_stack[cpu][i][0],
                                    OPENOS_X86_64_PERCPU_IST_SIZE);
        } else {
            tss->ist[i] = 0;
        }
    }
    tss->reserved2 = 0;
    tss->reserved3 = 0;
    tss->iomap_base = (uint16_t)sizeof(*tss);

    tss->rsp[0] = stack_top(&g_rsp0_stack[cpu][0],
                            OPENOS_X86_64_PERCPU_RSP0_SIZE);
}

static void build_gdt(uint32_t cpu) {
    const uint8_t k_code = GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 |
                           GDT_ACCESS_CODE   | GDT_ACCESS_RW;
    const uint8_t k_data = GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 |
                           GDT_ACCESS_DATA   | GDT_ACCESS_RW;
    const uint8_t u_code = GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 |
                           GDT_ACCESS_CODE   | GDT_ACCESS_RW;
    const uint8_t u_data = GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 |
                           GDT_ACCESS_DATA   | GDT_ACCESS_RW;
    const uint8_t tss_acc = GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 |
                            GDT_ACCESS_TSS;

    uint64_t            tss_base  = (uint64_t)(uintptr_t)&g_tss[cpu];
    uint32_t            tss_limit = (uint32_t)(sizeof(struct x86_64_tss) - 1u);
    uint32_t            tss_index = OPENOS_X86_64_GDT_TSS >> 3;
    uint64_t           *gdt       = &g_gdt[cpu][0];

    gdt[0] = 0;
    gdt[1] = make_descriptor(0, 0, k_code, GDT_FLAG_64BIT);
    gdt[2] = make_descriptor(0, 0, k_data, 0);
    gdt[3] = make_descriptor(0, 0, u_data, 0);
    gdt[4] = make_descriptor(0, 0, u_code, GDT_FLAG_64BIT);
    gdt[5] = make_descriptor(0, 0xFFFFFu, u_code,
                             GDT_FLAG_GRANULAR | GDT_FLAG_32BIT);
    /* TSS occupies two 8-byte slots. */
    gdt[tss_index] = make_descriptor((uint32_t)tss_base, tss_limit,
                                     tss_acc, 0);
    gdt[tss_index + 1u] = tss_base >> 32;

    g_gdt_ptr[cpu].limit = (uint16_t)(sizeof(g_gdt[cpu]) - 1u);
    g_gdt_ptr[cpu].base  = (x86_64_virt_addr_t)(uintptr_t)gdt;
}

void arch_x86_64_percpu_setup(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) {
        return;
    }
    build_tss(cpu_idx);
    build_gdt(cpu_idx);
}

void arch_x86_64_percpu_load(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) {
        return;
    }

    /* Take the GDT pointer's address once into a local variable so the
     * inline asm sees a plain `m` operand, not an array indexing
     * expression. gcc was happily emitting `lgdt 0(%rdx,%rdx,1)` when we
     * passed g_gdt_ptr[cpu_idx] as the constraint, which loaded a wild
     * limit/base pair and tripled the CPU. */
    const struct gdt_pointer *gp = &g_gdt_ptr[cpu_idx];

    __asm__ __volatile__(
        "lgdt (%[gp])\n"
        "pushq %[code]\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw %[data], %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %[tss],  %%ax\n"
        "ltr  %%ax\n"
        :
        : [gp]   "r"(gp),
          [code] "i"((uint64_t)OPENOS_X86_64_GDT_KERNEL_CODE),
          [data] "i"(OPENOS_X86_64_GDT_KERNEL_DATA),
          [tss]  "i"(OPENOS_X86_64_GDT_TSS)
        : "rax", "memory");
}

uint32_t arch_x86_64_percpu_max(void) {
    return OPENOS_X86_64_PERCPU_MAX_CPUS;
}

x86_64_stack_ptr_t arch_x86_64_percpu_rsp0(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) {
        return 0;
    }
    return g_tss[cpu_idx].rsp[0];
}

x86_64_virt_addr_t arch_x86_64_percpu_tss_base(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) {
        return 0;
    }
    return (x86_64_virt_addr_t)(uintptr_t)&g_tss[cpu_idx];
}

/* ---------------- G.6.2: per-CPU "current" via GS_BASE ---------------- */

static arch_x86_64_percpu_t g_percpu[OPENOS_X86_64_PERCPU_MAX_CPUS]
    __attribute__((aligned(64)));

arch_x86_64_percpu_t *arch_x86_64_percpu_slot(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) return (void *)0;
    return &g_percpu[cpu_idx];
}

static inline void wrmsr64(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static inline uint64_t rdmsr64(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

void arch_x86_64_percpu_install_gs(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) {
        return;
    }
    arch_x86_64_percpu_t *p = &g_percpu[cpu_idx];
    p->self                 = (uint64_t)(uintptr_t)p;
    p->cpu_idx              = cpu_idx;
    p->magic                = OPENOS_X86_64_PERCPU_MAGIC;
    p->sched_current_idx    = 0;
    p->sched_quantum_left   = 0;
    p->sched_switch_count   = 0;
    p->sched_preempt_count  = 0;
    p->lapic_timer_count    = 0;
    p->sched_tick_calls     = 0;
    /* G.6.6a/G.6.7a: clear reschedule-IPI counters and need_resched flag.
     * Note: install_gs() runs once per CPU during its bring-up, so this
     * is the right place to zero them. BSP=0 contract for need_resched
     * is enforced here by initial value + by smp_send_resched_ipi
     * rejecting BSP targets. */
    p->resched_ipi_count        = 0;
    p->need_resched             = 0;
    p->_resv_after_need_resched = 0;
    p->resched_dispatch_count   = 0;
    /* G.6.7b: preempt-disable depth and deferred-dispatch counter
     * start at 0 on every CPU. depth=0 is the "preemption enabled"
     * state, the natural resting position. */
    p->preempt_disable_depth    = 0;
    p->_resv_after_pdd          = 0;
    p->preempt_deferred_count   = 0;

    /* Write IA32_GS_BASE directly. Note: a subsequent `mov <selector>, %gs`
     * would reload the hidden base from the GDT descriptor and *clobber*
     * this value. callers must therefore install GS after percpu_load. */
    wrmsr64(OPENOS_X86_64_MSR_GS_BASE, (uint64_t)(uintptr_t)p);

    /* Also seed IA32_KERNEL_GS_BASE to the same value so a stray swapgs
     * (e.g. from a future syscall path) doesn't land us in zeroland. */
    wrmsr64(OPENOS_X86_64_MSR_KERNEL_GS_BASE, (uint64_t)(uintptr_t)p);
}

bool arch_x86_64_percpu_gs_ok(void) {
    uint64_t base = rdmsr64(OPENOS_X86_64_MSR_GS_BASE);
    if (base == 0) return false;
    arch_x86_64_percpu_t *p = (arch_x86_64_percpu_t *)(uintptr_t)base;
    /* Sanity: %gs:0 must self-pointer back to p, magic must match. */
    if (p->self != base) return false;
    if (p->magic != OPENOS_X86_64_PERCPU_MAGIC) return false;
    if (p->cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) return false;
    return true;
}
