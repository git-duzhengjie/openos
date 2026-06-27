#include "../include/smp64.h"
#include "../include/acpi64.h"
#include "../include/lapic64.h"
#include "../include/ap_trampoline64.h"
#include "../include/delay64.h"
#include "../include/vmm64.h"
#include "../include/percpu64.h"
#include "../include/idt64.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define OPENOS_X86_64_SMP_STACK_BASE (0xFFFFD00000000000ULL)

typedef struct smp_state {
    bool     ready;
    bool     trampoline_installed;
    uint8_t  bsp_apic_id;
    uint32_t cpu_count;
    uint32_t ap_count;
    uint8_t  ap_apic_ids[OPENOS_X86_64_SMP_MAX_CPUS];
    uint8_t *stacks[OPENOS_X86_64_SMP_MAX_CPUS];
} smp_state_t;

static smp_state_t g_smp;

bool arch_x86_64_smp_init(void) {
    g_smp.ready = false;
    g_smp.bsp_apic_id = 0;
    g_smp.cpu_count = 0;
    g_smp.ap_count = 0;
    for (uint32_t i = 0; i < OPENOS_X86_64_SMP_MAX_CPUS; ++i) {
        g_smp.stacks[i] = 0;
    }

    if (!arch_x86_64_lapic_is_ready()) {
        return false;
    }
    g_smp.bsp_apic_id = (uint8_t)arch_x86_64_lapic_id();

    const arch_x86_64_acpi_info_t *acpi = arch_x86_64_acpi_info();
    if (acpi == 0 || acpi->cpu_count == 0) {
        g_smp.cpu_count = 1;
        g_smp.ap_count = 0;
        g_smp.ready = true;
        return true;
    }

    uint32_t enabled = 0;
    uint32_t ap_idx = 0;
    for (uint32_t i = 0; i < acpi->cpu_count; ++i) {
        const acpi_cpu_entry_t *cpu = &acpi->cpus[i];
        if ((cpu->flags & 0x1u) == 0u) {
            continue;
        }
        enabled++;
        if (cpu->apic_id == g_smp.bsp_apic_id) {
            continue;
        }
        if (ap_idx < OPENOS_X86_64_SMP_MAX_CPUS) {
            g_smp.ap_apic_ids[ap_idx++] = cpu->apic_id;
        }
    }
    g_smp.cpu_count = enabled;
    g_smp.ap_count  = ap_idx;
    g_smp.ready = true;
    return true;
}

bool arch_x86_64_smp_is_ready(void) { return g_smp.ready; }
uint8_t  arch_x86_64_smp_bsp_apic_id(void) { return g_smp.bsp_apic_id; }
uint32_t arch_x86_64_smp_ap_count(void) { return g_smp.ap_count; }
uint32_t arch_x86_64_smp_cpu_count(void) { return g_smp.cpu_count; }

uint8_t arch_x86_64_smp_ap_apic_id(uint32_t index) {
    if (index >= g_smp.ap_count) {
        return 0xFFu;
    }
    return g_smp.ap_apic_ids[index];
}

uint64_t arch_x86_64_smp_trampoline_phys(void) {
    return OPENOS_X86_64_SMP_TRAMPOLINE_PHYS;
}

bool arch_x86_64_smp_install_trampoline(void) {
    uint64_t phys = OPENOS_X86_64_SMP_TRAMPOLINE_PHYS;
    if (!arch_x86_64_ap_trampoline_install(phys)) {
        g_smp.trampoline_installed = false;
        return false;
    }
    if (!arch_x86_64_ap_trampoline_verify(phys)) {
        g_smp.trampoline_installed = false;
        return false;
    }
    g_smp.trampoline_installed = true;
    return true;
}

bool arch_x86_64_smp_trampoline_installed(void) {
    return g_smp.trampoline_installed;
}

uint32_t arch_x86_64_smp_send_init_all_aps(uint32_t *out_sent) {
    uint32_t sent = 0, ok = 0;
    if (!g_smp.ready) {
        if (out_sent) *out_sent = 0;
        return 0;
    }
    for (uint32_t i = 0; i < g_smp.ap_count; ++i) {
        uint8_t id = g_smp.ap_apic_ids[i];
        sent++;
        if (arch_x86_64_lapic_send_init(id)) {
            ok++;
        }
    }
    if (out_sent) *out_sent = sent;
    return ok;
}

uint32_t arch_x86_64_smp_send_startup_all_aps(uint32_t *out_sent) {
    uint32_t sent = 0, ok = 0;
    if (!g_smp.ready || !g_smp.trampoline_installed) {
        if (out_sent) *out_sent = 0;
        return 0;
    }

    uint64_t tramp_phys = arch_x86_64_smp_trampoline_phys();
    if (tramp_phys == 0 || tramp_phys >= 0x100000ULL ||
        (tramp_phys & 0xFFFULL) != 0) {
        if (out_sent) *out_sent = 0;
        return 0;
    }
    uint8_t vector = (uint8_t)(tramp_phys >> 12);

    for (uint32_t i = 0; i < g_smp.ap_count; ++i) {
        uint8_t id = g_smp.ap_apic_ids[i];
        sent++;

        if (!arch_x86_64_lapic_send_init(id)) continue;
        arch_x86_64_delay_ms(10);

        if (!arch_x86_64_lapic_send_startup(id, vector)) continue;
        arch_x86_64_delay_us(200);

        if (!arch_x86_64_lapic_send_startup(id, vector)) continue;

        ok++;
    }
    if (out_sent) *out_sent = sent;
    return ok;
}

void arch_x86_64_smp_alive_reset(void) {
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_PHYS;
    *p = 0;
}

uint8_t arch_x86_64_smp_alive_count(void) {
    const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_PHYS;
    return *p;
}

uint8_t arch_x86_64_smp_alive_wait(uint8_t expected, uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    for (;;) {
        uint8_t cur = arch_x86_64_smp_alive_count();
        if (cur >= expected) return cur;
        if (elapsed >= timeout_ms) return cur;
        arch_x86_64_delay_ms(1);
        elapsed++;
    }
}

#define OPENOS_X86_64_SMP_ALIVE_RM_PHYS    0x9000ULL
#define OPENOS_X86_64_SMP_ALIVE_PM32_PHYS  0x9008ULL
#define OPENOS_X86_64_SMP_ALIVE_LM64_PHYS  0x9010ULL

uint8_t arch_x86_64_smp_alive_rm(void) {
    const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_RM_PHYS;
    return *p;
}

uint8_t arch_x86_64_smp_alive_pm32(void) {
    const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_PM32_PHYS;
    return *p;
}

uint8_t arch_x86_64_smp_alive_lm64(void) {
    const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_LM64_PHYS;
    return *p;
}

void arch_x86_64_smp_alive_reset_all(void) {
    volatile uint8_t *rm  = (volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_RM_PHYS;
    volatile uint8_t *pm  = (volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_PM32_PHYS;
    volatile uint8_t *lm  = (volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_LM64_PHYS;
    volatile uint8_t *la  = (volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_LAPIC_PHYS;
    volatile uint8_t *pc  = (volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_PERCPU_PHYS;
    volatile uint8_t *id  = (volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_IDLE_PHYS;
    volatile uint8_t *gs  = (volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_GS_PHYS;
    *rm = 0;
    *pm = 0;
    *lm = 0;
    *la = 0;
    *pc = 0;
    *id = 0;
    *gs = 0;
}

uint8_t arch_x86_64_smp_alive_rm_wait(uint8_t expected, uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    for (;;) {
        uint8_t cur = arch_x86_64_smp_alive_rm();
        if (cur >= expected) return cur;
        if (elapsed >= timeout_ms) return cur;
        arch_x86_64_delay_ms(1);
        elapsed++;
    }
}

uint8_t arch_x86_64_smp_alive_pm32_wait(uint8_t expected, uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    for (;;) {
        uint8_t cur = arch_x86_64_smp_alive_pm32();
        if (cur >= expected) return cur;
        if (elapsed >= timeout_ms) return cur;
        arch_x86_64_delay_ms(1);
        elapsed++;
    }
}

uint8_t arch_x86_64_smp_alive_lm64_wait(uint8_t expected, uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    for (;;) {
        uint8_t cur = arch_x86_64_smp_alive_lm64();
        if (cur >= expected) return cur;
        if (elapsed >= timeout_ms) return cur;
        arch_x86_64_delay_ms(1);
        elapsed++;
    }
}

uint8_t arch_x86_64_smp_alive_lapic(void) {
    const volatile uint8_t *p =
        (const volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_LAPIC_PHYS;
    return *p;
}

uint8_t arch_x86_64_smp_alive_lapic_wait(uint8_t expected, uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    for (;;) {
        uint8_t cur = arch_x86_64_smp_alive_lapic();
        if (cur >= expected) return cur;
        if (elapsed >= timeout_ms) return cur;
        arch_x86_64_delay_ms(1);
        elapsed++;
    }
}

uint8_t arch_x86_64_smp_alive_percpu(void) {
    const volatile uint8_t *p =
        (const volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_PERCPU_PHYS;
    return *p;
}

uint8_t arch_x86_64_smp_alive_percpu_wait(uint8_t expected, uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    for (;;) {
        uint8_t cur = arch_x86_64_smp_alive_percpu();
        if (cur >= expected) return cur;
        if (elapsed >= timeout_ms) return cur;
        arch_x86_64_delay_ms(1);
        elapsed++;
    }
}

/* G.6.1: AP-side idle-loop reached. */
uint8_t arch_x86_64_smp_alive_idle(void) {
    const volatile uint8_t *p =
        (const volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_IDLE_PHYS;
    return *p;
}

uint8_t arch_x86_64_smp_alive_idle_wait(uint8_t expected, uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    for (;;) {
        uint8_t cur = arch_x86_64_smp_alive_idle();
        if (cur >= expected) return cur;
        if (elapsed >= timeout_ms) return cur;
        arch_x86_64_delay_ms(1);
        elapsed++;
    }
}

/* G.6.2: per-AP GS_BASE installation confirmation. */
uint8_t arch_x86_64_smp_alive_gs(void) {
    const volatile uint8_t *p =
        (const volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_GS_PHYS;
    return *p;
}

uint8_t arch_x86_64_smp_alive_gs_wait(uint8_t expected, uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    for (;;) {
        uint8_t cur = arch_x86_64_smp_alive_gs();
        if (cur >= expected) return cur;
        if (elapsed >= timeout_ms) return cur;
        arch_x86_64_delay_ms(1);
        elapsed++;
    }
}

uint64_t arch_x86_64_smp_stack_base(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_SMP_MAX_CPUS) return 0;
    return OPENOS_X86_64_SMP_STACK_BASE + ((uint64_t)cpu_idx * OPENOS_X86_64_SMP_STACK_SIZE);
}

uint64_t arch_x86_64_smp_stack_top(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_SMP_MAX_CPUS) return 0;
    return OPENOS_X86_64_SMP_STACK_BASE + ((uint64_t)(cpu_idx + 1) * OPENOS_X86_64_SMP_STACK_SIZE);
}

uint64_t arch_x86_64_smp_cpu_stack_top(uint8_t apic_id) {
    if (apic_id == g_smp.bsp_apic_id) {
        return arch_x86_64_smp_stack_top(0);
    }
    for (uint32_t i = 0; i < g_smp.ap_count; ++i) {
        if (g_smp.ap_apic_ids[i] == apic_id) {
            return arch_x86_64_smp_stack_top(i + 1);
        }
    }
    return 0;
}

static uint64_t smp_read_cr3(void) {
    uint64_t val;
    __asm__ volatile ("movq %%cr3, %0" : "=r"(val));
    return val;
}

/* Static per-AP stacks in BSS (16 KiB each). Index 0 is BSP placeholder. */
static uint8_t g_ap_stacks[OPENOS_X86_64_SMP_MAX_CPUS][OPENOS_X86_64_SMP_STACK_SIZE]
    __attribute__((aligned(16)));

/* Track which AP got which cpu index (filled in ap_entry) */
static volatile uint8_t g_ap_cpu_to_apic[OPENOS_X86_64_SMP_MAX_CPUS];

void arch_x86_64_smp_prepare_aps(void) {
    if (!g_smp.trampoline_installed) return;

    uint64_t phys = arch_x86_64_smp_trampoline_phys();
    uint64_t cr3  = smp_read_cr3();

    arch_x86_64_ap_trampoline_set_cr3(phys, cr3);
    arch_x86_64_ap_trampoline_set_entry(phys, (uint64_t)arch_x86_64_ap_entry);

    /* Fill the per-CPU stack-top table at 0xA000.
     * Slot 0 is reserved for BSP (unused by APs); slots 1..N hold AP stack tops. */
    volatile uint64_t *stack_table =
        (volatile uint64_t *)(uintptr_t)OPENOS_X86_64_SMP_STACK_TABLE_PHYS;
    for (uint32_t i = 0; i < OPENOS_X86_64_SMP_MAX_CPUS; ++i) {
        uint64_t top = (uint64_t)(uintptr_t)&g_ap_stacks[i][OPENOS_X86_64_SMP_STACK_SIZE];
        stack_table[i] = top;
        g_ap_cpu_to_apic[i] = 0xFFu;
    }

    /* Reset the atomic AP cpu-index counter at 0x9018.
     * Init = 1 so the first AP picks slot 1 (slot 0 = BSP). */
    volatile uint64_t *cpu_idx_ctr =
        (volatile uint64_t *)(uintptr_t)OPENOS_X86_64_SMP_CPU_IDX_PHYS;
    *cpu_idx_ctr = 1;
}

void arch_x86_64_ap_entry(uint64_t apic_id) {
    /* At this point %rsp already points to the top of this AP's private stack
     * (set up in trampoline LM64 via the 0xA000 table). Safe to call C now. */

    /* Read back our cpu index from the counter — the trampoline used lock xadd
     * which means the *current* counter value is (max_used + 1). We can't get
     * our own pre-add return that way; instead, derive cpu_idx from apic_id. */
    uint32_t cpu_idx = 0;
    for (uint32_t i = 0; i < g_smp.ap_count; ++i) {
        if (g_smp.ap_apic_ids[i] == (uint8_t)apic_id) {
            cpu_idx = i + 1;
            break;
        }
    }
    if (cpu_idx < OPENOS_X86_64_SMP_MAX_CPUS) {
        g_ap_cpu_to_apic[cpu_idx] = (uint8_t)apic_id;
    }

    /* G.5-lapic: bring this AP's local APIC online and record success in a
     * shared atomic counter so the BSP selftest can wait on it. */
    bool lapic_ok = arch_x86_64_lapic_init_ap();
    if (lapic_ok) {
        /* G.3b-final: each AP programs its own LVT LINT0/LINT1 per the
         * ACPI MADT, so chassis NMIs steered at this CPU's LINT pin get
         * delivered as vector 2 rather than vanishing or escalating to
         * a triple fault. Failures are non-fatal — alive_lapic still
         * counts as "AP is up". */
        (void)arch_x86_64_lapic_setup_nmi_lvt(/*is_bsp=*/false);
        __asm__ __volatile__(
            "lock incb (%0)"
            :
            : "r"((volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_LAPIC_PHYS)
            : "memory");
    }
    (void)arch_x86_64_lapic_id();

    /* G.5-gdt-tss: build this CPU's own GDT + TSS and swap to them.
     * Until this point the AP was running on the BSP's shared GDT, which is
     * fine for executing instructions but unsafe the moment an exception
     * fires (TSS.RSP0 is per-CPU state). */
    arch_x86_64_percpu_setup(cpu_idx);
    arch_x86_64_percpu_load(cpu_idx);
    __asm__ __volatile__(
        "lock incb (%0)"
        :
        : "r"((volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_PERCPU_PHYS)
        : "memory");

    /* G.6.1: install the global IDTR on this AP. The IDT table itself is
     * shared (read-only after BSP build), but IDTR is per-CPU — without this
     * lidt the AP would either still trap into the BSP's IDTR or, worse,
     * into garbage if the BSP's IDTR is no longer mapped. */
    arch_x86_64_idt_load_ap();

    /* G.6.2: install per-CPU "current" pointer via IA32_GS_BASE. Must come
     * *after* percpu_load (which reloads %gs from the GDT data segment and
     * thereby clobbers the hidden GS base) to take effect. */
    arch_x86_64_percpu_install_gs(cpu_idx);
    /* Verify the install: %gs:0 must round-trip back to &g_percpu[cpu_idx]
     * with self-pointer + magic matching. If not, freeze this AP — better
     * than silently bumping the alive counter and confusing the selftest. */
    if (!arch_x86_64_percpu_gs_ok()) {
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }
    __asm__ __volatile__(
        "lock incb (%0)"
        :
        : "r"((volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_GS_PHYS)
        : "memory");

    /* G.6.1: announce the AP has reached its idle loop. With N CPUs total
     * the BSP expects this counter to settle at N-1 after AP bring-up. */
    __asm__ __volatile__(
        "lock incb (%0)"
        :
        : "r"((volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_IDLE_PHYS)
        : "memory");

    /* AP idle loop. Interrupts stay disabled for now — sti + per-CPU
     * scheduler entry lands in G.6.4. */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
