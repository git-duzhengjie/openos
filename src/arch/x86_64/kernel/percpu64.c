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

/* G.6.2 / G.7c: per-CPU control block selected by GS_BASE. Defined up here
 * (before any function reaching for it through g_percpu[]) so the back-
 * references in arch_x86_64_percpu_set_rsp0() etc. resolve cleanly. */
static arch_x86_64_percpu_t g_percpu[OPENOS_X86_64_PERCPU_MAX_CPUS]
    __attribute__((aligned(64)));

_Static_assert(__builtin_offsetof(arch_x86_64_percpu_t, syscall_kernel_rsp)
                   == OPENOS_X86_64_PERCPU_OFF_SYSCALL_KRSP,
               "syscall_kernel_rsp offset must match asm constant");
_Static_assert(__builtin_offsetof(arch_x86_64_percpu_t, syscall_user_rsp)
                   == OPENOS_X86_64_PERCPU_OFF_SYSCALL_URSP,
               "syscall_user_rsp offset must match asm constant");
_Static_assert(__builtin_offsetof(arch_x86_64_percpu_t, baseline_rsp0)
                   == OPENOS_X86_64_PERCPU_OFF_BASELINE_RSP0,
               "baseline_rsp0 offset must match asm constant");
_Static_assert(__builtin_offsetof(arch_x86_64_percpu_t, user_dispatch_count)
                   == OPENOS_X86_64_PERCPU_OFF_USER_DISPATCH,
               "user_dispatch_count offset must match asm constant");

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

x86_64_stack_ptr_t arch_x86_64_percpu_set_rsp0(uint32_t cpu_idx,
                                                x86_64_stack_ptr_t new_rsp0) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) {
        return 0;
    }
    x86_64_stack_ptr_t old = g_tss[cpu_idx].rsp[0];
    g_tss[cpu_idx].rsp[0]  = new_rsp0;
    /* G.7c: keep the per-CPU syscall save-area's kernel stack pointer in
     * lock-step with the TSS RSP0 field. The syscall path swaps to this
     * cached value through %gs (it cannot read the TSS), and the iretq/
     * interrupt path swaps to TSS.RSP0 in hardware. They MUST agree so
     * that an interrupt taken mid-syscall lands on the same kernel
     * stack we are already using. */
    g_percpu[cpu_idx].syscall_kernel_rsp = (uint64_t)new_rsp0;
    return old;
}

uint64_t arch_x86_64_percpu_baseline_rsp0(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) {
        return 0;
    }
    return g_percpu[cpu_idx].baseline_rsp0;
}

uint64_t arch_x86_64_percpu_user_dispatch_count(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) {
        return 0;
    }
    /* Read the field via its address: the trampoline updates it via
     * `incq %gs:OFF_USER_DISPATCH`, so a plain volatile-style read is
     * sufficient as observer. */
    return *(volatile uint64_t *)&g_percpu[cpu_idx].user_dispatch_count;
}

uint64_t arch_x86_64_percpu_lapic_timer_count(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) {
        return 0;
    }
    return *(volatile uint64_t *)&g_percpu[cpu_idx].lapic_timer_count;
}

/* gamma.5-P1: per-CPU histogram of timer preempt hits (see the block
 * comment on tick_hits_user / tick_hits_kernel in include/percpu64.h).
 * The two counters are written by arch_x86_64_lapic_timer_irq_handler
 * using the CS from the iret frame forwarded by isr64.S. */
uint32_t arch_x86_64_percpu_tick_hits_user(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) {
        return 0;
    }
    return *(volatile uint32_t *)&g_percpu[cpu_idx].tick_hits_user;
}

uint32_t arch_x86_64_percpu_tick_hits_kernel(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) {
        return 0;
    }
    return *(volatile uint32_t *)&g_percpu[cpu_idx].tick_hits_kernel;
}

x86_64_stack_ptr_t arch_x86_64_percpu_ist(uint32_t cpu_idx, uint32_t ist_index) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) {
        return 0;
    }
    /* 1-based: caller passes 1..7, TSS field is 0-based. */
    if (ist_index == 0u || ist_index > OPENOS_X86_64_PERCPU_IST_COUNT) {
        return 0;
    }
    return g_tss[cpu_idx].ist[ist_index - 1u];
}

/* ---------------- G.6.2: per-CPU "current" via GS_BASE ---------------- */

/* (g_percpu storage definition lives at the top of the file alongside the
 * other per-CPU arrays; the _Static_assert layout guards are right next to
 * it.) */

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
    /* gamma.5-P1: histogram of preempted rings. See tick_hits_user /
     * tick_hits_kernel in include/percpu64.h. Reset per CPU here for
     * the same reason as the counters above. */
    p->tick_hits_user       = 0;
    p->tick_hits_kernel     = 0;
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
    /* G.7c: seed the syscall stack-swap save-area.
     *
     * syscall_kernel_rsp must come up matching the TSS RSP0 we just
     * latched into g_tss[cpu_idx].rsp[0] via percpu_setup(); otherwise
     * the very first syscall on this CPU would jump to RSP=0 and
     * triple-fault on the first push. syscall_user_rsp is a scratch
     * slot and starts cleared. */
    p->syscall_kernel_rsp       = (uint64_t)g_tss[cpu_idx].rsp[0];
    p->syscall_user_rsp         = 0;

    /* G.7e: snapshot the per-CPU baseline TSS.RSP0. percpu_setup() has
     * already installed RSP0 at this point. The scheduler reads this
     * baseline back via arch_x86_64_percpu_baseline_rsp0() when it
     * switches *out of* a USER sched slot and needs to restore TSS.RSP0
     * for subsequent KERNEL slots / interrupt entries on this CPU. */
    p->baseline_rsp0            = (uint64_t)g_tss[cpu_idx].rsp[0];

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

/* G.7b: read the raw IA32_GS_BASE / IA32_KERNEL_GS_BASE pair on the
 * current CPU. Used by Stage 18 of the SMP selftest to prove that
 * percpu_install_gs() has populated *both* MSRs and that they point
 * to the same percpu slot (the foundation on which the swapgs paths
 * added in this commit rely). Kept out-of-line because callers should
 * not need to know the MSR numbers, and to keep wrmsr/rdmsr helpers
 * file-local. */
void arch_x86_64_percpu_read_gs_pair(uint64_t *out_gs_base,
                                     uint64_t *out_kernel_gs_base) {
    if (out_gs_base) {
        *out_gs_base = rdmsr64(OPENOS_X86_64_MSR_GS_BASE);
    }
    if (out_kernel_gs_base) {
        *out_kernel_gs_base = rdmsr64(OPENOS_X86_64_MSR_KERNEL_GS_BASE);
    }
}
