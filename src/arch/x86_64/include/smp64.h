#ifndef OPENOS_ARCH_X86_64_SMP64_H
#define OPENOS_ARCH_X86_64_SMP64_H

#include <stdint.h>
#include <stdbool.h>

#define OPENOS_X86_64_SMP_MAX_CPUS   64u
#define OPENOS_X86_64_SMP_STACK_SIZE (8ULL * 1024ULL)

#define OPENOS_X86_64_SMP_TRAMPOLINE_PHYS  0x00008000ULL
#define OPENOS_X86_64_SMP_ALIVE_PHYS       0x00009000ULL

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

uint64_t arch_x86_64_smp_stack_base(uint32_t cpu_idx);
uint64_t arch_x86_64_smp_stack_top(uint32_t cpu_idx);
uint64_t arch_x86_64_smp_cpu_stack_top(uint8_t apic_id);

void arch_x86_64_smp_prepare_aps(void);
void arch_x86_64_ap_entry(uint64_t apic_id);

#endif /* OPENOS_ARCH_X86_64_SMP64_H */
