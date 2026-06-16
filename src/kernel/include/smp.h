#ifndef SMP_H
#define SMP_H

#include <types.h>

#define SMP_MAX_CPUS 32u
#define SMP_CPU_STATE_OFFLINE 0u
#define SMP_CPU_STATE_BSP     1u
#define SMP_CPU_STATE_PRESENT 2u
#define SMP_CPU_STATE_ENABLED 3u

typedef struct smp_cpu_info {
    uint32_t logical_id;
    uint32_t acpi_processor_id;
    uint32_t apic_id;
    uint32_t flags;
    uint32_t state;
} smp_cpu_info_t;

typedef struct smp_info {
    uint32_t initialized;
    uint32_t cpu_count;
    uint32_t enabled_cpu_count;
    uint32_t bsp_logical_id;
    uint32_t bsp_apic_id;
    smp_cpu_info_t cpus[SMP_MAX_CPUS];
} smp_info_t;

void smp_init(void);
const smp_info_t *smp_get_info(void);
uint32_t smp_get_cpu_count(void);
uint32_t smp_get_enabled_cpu_count(void);
const smp_cpu_info_t *smp_get_cpu(uint32_t logical_id);

#endif /* SMP_H */
