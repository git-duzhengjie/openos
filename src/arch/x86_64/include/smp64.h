#ifndef OPENOS_ARCH_X86_64_SMP64_H
#define OPENOS_ARCH_X86_64_SMP64_H

#include <stdint.h>
#include <stdbool.h>

#define OPENOS_X86_64_SMP_MAX_CPUS   64u
#define OPENOS_X86_64_SMP_STACK_SIZE (16ULL * 1024ULL)

#define OPENOS_X86_64_SMP_TRAMPOLINE_PHYS  0x00008000ULL
#define OPENOS_X86_64_SMP_ALIVE_PHYS       0x00009000ULL

/* Per-CPU bring-up shared memory (low physical addresses, identity mapped):
 *   0x9018: 8-byte atomic CPU index counter (BSP zeroes, AP uses lock xadd)
 *   0xA000: 8-byte-per-slot stack-top table (filled by BSP, read by AP) */
#define OPENOS_X86_64_SMP_CPU_IDX_PHYS     0x00009018ULL
#define OPENOS_X86_64_SMP_STACK_TABLE_PHYS 0x0000A000ULL

/* G.5-lapic: AP-side LAPIC bring-up alive counter (1 byte, atomic incb).
 * Each AP that successfully programmed its own LAPIC SVR/TPR bumps this. */
#define OPENOS_X86_64_SMP_ALIVE_LAPIC_PHYS 0x00009020ULL

/* G.5-gdt-tss: AP-side per-CPU GDT+TSS installed alive counter. */
#define OPENOS_X86_64_SMP_ALIVE_PERCPU_PHYS 0x00009028ULL

/* G.6.1: AP-side idle-loop reached counter. Each AP bumps it after its own
 * per-CPU GDT/TSS is loaded and its own IDTR is installed, i.e. right before
 * entering the AP idle (hlt) loop. With N CPUs total the BSP expects N-1. */
#define OPENOS_X86_64_SMP_ALIVE_IDLE_PHYS  0x00009030ULL

/* G.6.2: AP-side GS_BASE installed (per-CPU "current") confirmation. */
#define OPENOS_X86_64_SMP_ALIVE_GS_PHYS    0x00009038ULL

/* G.6.4: AP-side per-CPU idle slot registered in the scheduler
 * (sched_register_ap_idle returned the AP's own cpu_idx). Each AP bumps
 * this once its idle slot is RUNNING and this_cpu()->sched_current_idx
 * points at it. With N CPUs the BSP expects this to settle at N-1. */
#define OPENOS_X86_64_SMP_ALIVE_SCHED_PHYS 0x00009040ULL

bool arch_x86_64_smp_init(void);
bool arch_x86_64_smp_is_ready(void);

uint8_t  arch_x86_64_smp_bsp_apic_id(void);
uint32_t arch_x86_64_smp_ap_count(void);
uint32_t arch_x86_64_smp_cpu_count(void);
uint8_t  arch_x86_64_smp_ap_apic_id(uint32_t index);

uint64_t arch_x86_64_smp_trampoline_phys(void);
bool arch_x86_64_smp_install_trampoline(void);
bool arch_x86_64_smp_trampoline_installed(void);

uint32_t arch_x86_64_smp_send_init_all_aps(uint32_t *out_sent);
uint32_t arch_x86_64_smp_send_startup_all_aps(uint32_t *out_sent);

void arch_x86_64_smp_alive_reset(void);
uint8_t arch_x86_64_smp_alive_count(void);
uint8_t arch_x86_64_smp_alive_wait(uint8_t expected, uint32_t timeout_ms);

uint8_t arch_x86_64_smp_alive_rm(void);
uint8_t arch_x86_64_smp_alive_pm32(void);
uint8_t arch_x86_64_smp_alive_lm64(void);
void arch_x86_64_smp_alive_reset_all(void);

uint8_t arch_x86_64_smp_alive_rm_wait(uint8_t expected, uint32_t timeout_ms);
uint8_t arch_x86_64_smp_alive_pm32_wait(uint8_t expected, uint32_t timeout_ms);
uint8_t arch_x86_64_smp_alive_lm64_wait(uint8_t expected, uint32_t timeout_ms);

/* G.5-lapic: per-AP LAPIC bring-up confirmation. */
uint8_t arch_x86_64_smp_alive_lapic(void);
uint8_t arch_x86_64_smp_alive_lapic_wait(uint8_t expected, uint32_t timeout_ms);

/* G.5-gdt-tss: per-AP private GDT+TSS confirmation. */
uint8_t arch_x86_64_smp_alive_percpu(void);
uint8_t arch_x86_64_smp_alive_percpu_wait(uint8_t expected, uint32_t timeout_ms);

/* G.6.1: per-AP idle-loop reached confirmation. */
uint8_t arch_x86_64_smp_alive_idle(void);
uint8_t arch_x86_64_smp_alive_idle_wait(uint8_t expected, uint32_t timeout_ms);

/* G.6.2: per-AP GS_BASE installation confirmation. */
uint8_t arch_x86_64_smp_alive_gs(void);
uint8_t arch_x86_64_smp_alive_gs_wait(uint8_t expected, uint32_t timeout_ms);

/* G.6.4: per-AP scheduler-registered (idle slot RUNNING) confirmation. */
uint8_t arch_x86_64_smp_alive_sched(void);
uint8_t arch_x86_64_smp_alive_sched_wait(uint8_t expected, uint32_t timeout_ms);

uint64_t arch_x86_64_smp_stack_base(uint32_t cpu_idx);
uint64_t arch_x86_64_smp_stack_top(uint32_t cpu_idx);
uint64_t arch_x86_64_smp_cpu_stack_top(uint8_t apic_id);

void arch_x86_64_smp_prepare_aps(void);
void arch_x86_64_ap_entry(uint64_t apic_id);

#endif /* OPENOS_ARCH_X86_64_SMP64_H */
