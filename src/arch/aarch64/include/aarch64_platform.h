#ifndef OPENOS_ARCH_AARCH64_PLATFORM_H
#define OPENOS_ARCH_AARCH64_PLATFORM_H

#include <stdint.h>
#include "bootinfo.h"

#define AARCH64_QEMU_VIRT_GICD_BASE      0x08000000ULL
#define AARCH64_QEMU_VIRT_GICC_BASE      0x08010000ULL
#define AARCH64_QEMU_VIRT_UART0_BASE     0x09000000ULL
#define AARCH64_QEMU_VIRT_RTC_BASE       0x09010000ULL
#define AARCH64_QEMU_VIRT_PCIE_ECAM_BASE 0x4010000000ULL
#define AARCH64_QEMU_VIRT_MEMORY_BASE    0x40000000ULL
#define AARCH64_QEMU_VIRT_MEMORY_SIZE    0x40000000ULL

typedef struct aarch64_dtb_info {
    uint64_t base;
    uint64_t size;
    int valid;
} aarch64_dtb_info_t;

typedef struct aarch64_platform_state {
    uint64_t dtb_base;
    uint32_t timer_frequency_hz;
    int gic_ready;
    int timer_ready;
    int psci_ready;
    int dtb_ready;
} aarch64_platform_state_t;

void aarch64_gic_init(void);
void aarch64_timer_init(void);
uint64_t aarch64_timer_read_counter(void);
uint32_t aarch64_timer_read_frequency(void);
void aarch64_psci_init(void);
int aarch64_psci_system_off(void);
int aarch64_psci_system_reset(void);
aarch64_dtb_info_t aarch64_dtb_probe(uint64_t dtb_base);
void aarch64_platform_init(uint64_t dtb_base);
const aarch64_platform_state_t *aarch64_platform_get_state(void);
const openos_bootinfo_t *aarch64_platform_bootinfo_from_dtb(uint64_t dtb_base);

#endif /* OPENOS_ARCH_AARCH64_PLATFORM_H */
