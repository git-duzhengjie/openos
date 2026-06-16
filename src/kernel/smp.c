/* ============================================================
 * openos - SMP CPU topology bootstrap
 * ============================================================ */
#include "include/smp.h"
#include "include/apic.h"
#include "include/serial.h"

static smp_info_t g_smp_info;

static void smp_zero_info(void) {
    uint32_t i;

    g_smp_info.initialized = 0;
    g_smp_info.cpu_count = 0;
    g_smp_info.enabled_cpu_count = 0;
    g_smp_info.bsp_logical_id = 0;
    g_smp_info.bsp_apic_id = 0;
    for (i = 0; i < SMP_MAX_CPUS; ++i) {
        g_smp_info.cpus[i].logical_id = i;
        g_smp_info.cpus[i].acpi_processor_id = 0;
        g_smp_info.cpus[i].apic_id = 0;
        g_smp_info.cpus[i].flags = 0;
        g_smp_info.cpus[i].state = SMP_CPU_STATE_OFFLINE;
    }
}

static uint32_t smp_find_bsp_logical_id(uint32_t bsp_apic_id) {
    uint32_t i;

    for (i = 0; i < g_smp_info.cpu_count; ++i) {
        if (g_smp_info.cpus[i].apic_id == bsp_apic_id) {
            return i;
        }
    }
    return 0;
}

static void smp_add_fallback_bsp(const apic_info_t *apic) {
    g_smp_info.cpu_count = 1;
    g_smp_info.enabled_cpu_count = 1;
    g_smp_info.bsp_logical_id = 0;
    g_smp_info.bsp_apic_id = apic->bsp_apic_id;
    g_smp_info.cpus[0].logical_id = 0;
    g_smp_info.cpus[0].acpi_processor_id = 0;
    g_smp_info.cpus[0].apic_id = apic->bsp_apic_id;
    g_smp_info.cpus[0].flags = 1u;
    g_smp_info.cpus[0].state = SMP_CPU_STATE_BSP;
}

void smp_init(void) {
    const apic_info_t *apic;
    uint32_t i;

    smp_zero_info();
    apic = apic_get_info();
    g_smp_info.bsp_apic_id = apic->bsp_apic_id;

    if (apic->lapic_cpu_count == 0) {
        smp_add_fallback_bsp(apic);
    } else {
        g_smp_info.cpu_count = apic->lapic_cpu_count;
        if (g_smp_info.cpu_count > SMP_MAX_CPUS) {
            g_smp_info.cpu_count = SMP_MAX_CPUS;
        }

        for (i = 0; i < g_smp_info.cpu_count; ++i) {
            const apic_cpu_info_t *cpu = &apic->lapic_cpus[i];
            g_smp_info.cpus[i].logical_id = i;
            g_smp_info.cpus[i].acpi_processor_id = cpu->acpi_processor_id;
            g_smp_info.cpus[i].apic_id = cpu->apic_id;
            g_smp_info.cpus[i].flags = cpu->flags;
            if ((cpu->flags & 1u) != 0) {
                g_smp_info.cpus[i].state = SMP_CPU_STATE_ENABLED;
                g_smp_info.enabled_cpu_count++;
            } else {
                g_smp_info.cpus[i].state = SMP_CPU_STATE_PRESENT;
            }
        }

        g_smp_info.bsp_logical_id = smp_find_bsp_logical_id(g_smp_info.bsp_apic_id);
        if (g_smp_info.bsp_logical_id < g_smp_info.cpu_count) {
            g_smp_info.cpus[g_smp_info.bsp_logical_id].state = SMP_CPU_STATE_BSP;
        }
        if (g_smp_info.enabled_cpu_count == 0) {
            g_smp_info.enabled_cpu_count = 1;
        }
    }

    g_smp_info.initialized = 1;

    serial_write("=====================================\n");
    serial_write("SMP CPU Topology\n");
    serial_write("=====================================\n");
    serial_write("[SMP] CPUs=");
    serial_write_hex(g_smp_info.cpu_count);
    serial_write(" enabled=");
    serial_write_hex(g_smp_info.enabled_cpu_count);
    serial_write(" BSP_APIC_ID=");
    serial_write_hex(g_smp_info.bsp_apic_id);
    serial_write(" BSP_LOGICAL_ID=");
    serial_write_hex(g_smp_info.bsp_logical_id);
    serial_write("\n");

    for (i = 0; i < g_smp_info.cpu_count; ++i) {
        serial_write("[SMP] cpu logical=");
        serial_write_hex(g_smp_info.cpus[i].logical_id);
        serial_write(" acpi=");
        serial_write_hex(g_smp_info.cpus[i].acpi_processor_id);
        serial_write(" apic=");
        serial_write_hex(g_smp_info.cpus[i].apic_id);
        serial_write(" state=");
        serial_write_hex(g_smp_info.cpus[i].state);
        serial_write(" flags=");
        serial_write_hex(g_smp_info.cpus[i].flags);
        serial_write("\n");
    }

    serial_write("[SMP] AP startup deferred until trampoline/IPI path is ready\n");
    serial_write("=====================================\n");
}

const smp_info_t *smp_get_info(void) {
    return &g_smp_info;
}

uint32_t smp_get_cpu_count(void) {
    return g_smp_info.cpu_count;
}

uint32_t smp_get_enabled_cpu_count(void) {
    return g_smp_info.enabled_cpu_count;
}

const smp_cpu_info_t *smp_get_cpu(uint32_t logical_id) {
    if (logical_id >= g_smp_info.cpu_count) {
        return 0;
    }
    return &g_smp_info.cpus[logical_id];
}
