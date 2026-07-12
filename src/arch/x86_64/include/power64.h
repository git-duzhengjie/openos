/*
 * power64.h — M6.1: ACPI power management (shutdown / reboot / sleep).
 *
 * Builds on top of the existing ACPI RSDP/XSDT parser (acpi64.h). Whereas
 * acpi64.c only locates the MADT (for SMP), this module walks the same
 * XSDT/RSDT to locate the FADT ("FACP"), extracts the fixed power-management
 * register blocks, and scans the DSDT for the \_S5 sleep package so the
 * kernel can perform an ACPI soft-off. It also captures the FADT RESET_REG
 * for warm reboot, with an 8042 keyboard-controller pulse as a fallback.
 *
 * Design notes:
 *   - Kept in a *separate* module from acpi64.c on purpose: the MADT parser
 *     is on the SMP boot critical path and should stay lean. Power management
 *     is an on-demand facility invoked from a syscall / GUI action.
 *   - FADT discovery reuses arch_x86_64_acpi_info()->xsdt_phys / rsdt_phys,
 *     so we do not re-locate the RSDP.
 *   - The DSDT \_S5 scan is a deliberately small AML pattern match (the
 *     classic osdev approach), NOT a full AML interpreter. It is robust on
 *     QEMU/OVMF and typical PC firmware.
 *
 * Milestone breakdown:
 *   M6.1a  FADT + \_S5 discovery, register extraction, getters (this file).
 *   M6.1b  arch_x86_64_power_shutdown()  — ACPI S5 soft-off.
 *   M6.1c  arch_x86_64_power_reboot()    — FADT RESET_REG, 8042 fallback.
 */
#ifndef OPENOS_ARCH_X86_64_POWER64_H
#define OPENOS_ARCH_X86_64_POWER64_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Generic Address Structure (GAS), ACPI 6.x §5.2.3.2. 12 bytes packed. */
typedef struct {
    uint8_t  address_space_id;   /* 0 = system memory, 1 = system I/O */
    uint8_t  register_bit_width;
    uint8_t  register_bit_offset;
    uint8_t  access_size;
    uint64_t address;
} power_gas_t;

#define POWER_GAS_ASID_SYSTEM_MEMORY 0u
#define POWER_GAS_ASID_SYSTEM_IO     1u

/* Snapshot of the power-management state extracted from the FADT / DSDT. */
typedef struct {
    uint32_t valid;              /* 1 once arch_x86_64_power_init() succeeded */

    uint64_t fadt_phys;          /* physical address of the FADT ("FACP")   */
    uint64_t dsdt_phys;          /* physical address of the DSDT            */

    /* Fixed PM1 control register blocks (I/O port addresses on PC). */
    uint32_t pm1a_cnt_port;      /* FADT PM1a_CNT_BLK (u32 legacy) */
    uint32_t pm1b_cnt_port;      /* FADT PM1b_CNT_BLK (0 if absent) */

    /* SMI command port + ACPI enable value, to switch to ACPI mode. */
    uint32_t smi_cmd_port;       /* FADT SMI_CMD */
    uint8_t  acpi_enable_val;    /* FADT ACPI_ENABLE */
    uint8_t  acpi_disable_val;   /* FADT ACPI_DISABLE */

    /* \_S5 sleep-type values decoded from the DSDT (for soft-off). */
    uint8_t  s5_slp_typ_a;       /* SLP_TYPa for the \_S5 state */
    uint8_t  s5_slp_typ_b;       /* SLP_TYPb for the \_S5 state */
    uint8_t  s5_valid;           /* 1 if the \_S5 package was decoded */

    /* FADT reset register (ACPI 2.0+). reset_supported==0 => use 8042. */
    uint8_t  reset_supported;    /* 1 if RESET_REG/RESET_VALUE are usable */
    uint8_t  reset_value;        /* FADT RESET_VALUE */
    power_gas_t reset_reg;       /* FADT RESET_REG (GAS) */

    uint16_t pm1_cnt_width_bits; /* PM1 control register width in bits (16) */
} arch_x86_64_power_info_t;

/* PM1_CNT bit fields (ACPI 6.x §4.8.3.2.1). */
#define POWER_PM1_CNT_SLP_TYP_SHIFT 10u
#define POWER_PM1_CNT_SLP_TYP_MASK  0x1C00u   /* bits [12:10] */
#define POWER_PM1_CNT_SLP_EN        0x2000u   /* bit 13        */

/* SYS_POWER op codes (shared with the user-space openos64.h ABI). */
#ifndef OPENOS64_POWER_SHUTDOWN
#define OPENOS64_POWER_SHUTDOWN 0ULL
#define OPENOS64_POWER_REBOOT   1ULL
#define OPENOS64_POWER_QUERY    2ULL
#endif

/*
 * Discover the FADT via the ACPI XSDT/RSDT, extract PM1 control registers,
 * decode the DSDT \_S5 package, and capture the reset register.
 *
 * Preconditions: arch_x86_64_acpi_init() must have already succeeded so
 * that arch_x86_64_acpi_info()->xsdt_phys / rsdt_phys are populated.
 *
 * Returns 1 on success (FADT found + parsed), 0 otherwise. Safe to call
 * more than once (idempotent).
 */
int arch_x86_64_power_init(void);

/* Read-only snapshot; NULL until arch_x86_64_power_init() has succeeded. */
const arch_x86_64_power_info_t *arch_x86_64_power_info(void);

/*
 * M6.1b — Attempt an ACPI S5 soft-off. Writes SLP_TYPa|SLP_EN to PM1a_CNT
 * (and PM1b_CNT if present). On real hardware / QEMU this powers the
 * machine off and never returns. If ACPI \_S5 is unavailable it falls back
 * to the QEMU/Bochs debug shutdown ports (0x604 / 0xB004) and, as a last
 * resort, halts the CPU. This function does NOT return on success.
 */
void arch_x86_64_power_shutdown(void);

/*
 * M6.1c — Attempt a warm reboot. Prefers the FADT RESET_REG (I/O or MMIO)
 * with RESET_VALUE; falls back to pulsing the 8042 keyboard controller
 * (port 0x64 <- 0xFE); as a last resort triple-faults via a null IDT.
 * This function does NOT return on success.
 */
void arch_x86_64_power_reboot(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_ARCH_X86_64_POWER64_H */
