/*
 * acpi64.h — Step G.3a: ACPI RSDP / XSDT / RSDT / MADT parser.
 *
 * Goals (G.3a, prerequisite for SMP AP bring-up):
 *   - Walk the EFI configuration table (still readable post-ExitBootServices)
 *     and find the ACPI 2.0 RSDP (fall back to ACPI 1.0).
 *   - Validate RSDP checksum, locate XSDT (preferred) or RSDT.
 *   - Iterate XSDT/RSDT entries, find MADT (a.k.a. APIC, sig "APIC").
 *   - Parse MADT entries:
 *       * Type 0  Processor Local APIC  -> collect (acpi_id, apic_id, flags)
 *       * Type 1  I/O APIC              -> collect (id, address, gsi_base)
 *       * Type 2  Interrupt Override    -> collect ISA->GSI overrides
 *   - Expose findings via `arch_x86_64_acpi_info_t` and a few getters.
 *
 * G.3b will consume cpu_apic_ids[] to fire INIT-SIPI-SIPI at every
 * APIC ID != bsp_apic_id.
 */
#ifndef OPENOS_ARCH_X86_64_ACPI64_H
#define OPENOS_ARCH_X86_64_ACPI64_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENOS_X86_64_ACPI_MAX_CPUS         32u
#define OPENOS_X86_64_ACPI_MAX_IOAPICS       4u
#define OPENOS_X86_64_ACPI_MAX_IRQ_OVERRIDES 16u
#define OPENOS_X86_64_ACPI_MAX_LAPIC_NMIS   32u

typedef struct {
    uint8_t  acpi_processor_id;
    uint8_t  apic_id;
    uint32_t flags;       /* bit0 = enabled, bit1 = online-capable */
} acpi_cpu_entry_t;

typedef struct {
    uint8_t  id;
    uint32_t address;
    uint32_t gsi_base;
} acpi_ioapic_entry_t;

typedef struct {
    uint8_t  bus;          /* always 0 = ISA */
    uint8_t  source_irq;
    uint32_t gsi;
    uint16_t flags;
} acpi_irq_override_entry_t;

/* MADT type 4: Local APIC NMI source.
 *   acpi_processor_id == 0xFF means "applies to all processors".
 *   lint is 0 or 1 (LINT0 / LINT1).
 *   flags layout matches MPS (same as irq_override flags).
 */
typedef struct {
    uint8_t  acpi_processor_id;
    uint16_t flags;
    uint8_t  lint;
} acpi_lapic_nmi_entry_t;

typedef struct {
    uint32_t valid;        /* 1 once init succeeded */
    uint32_t cpu_count;
    uint32_t ioapic_count;
    uint32_t irq_override_count;
    uint64_t lapic_address; /* MADT-declared LAPIC MMIO base
                             *   - initial value: MADT type 0 header (u32, zext)
                             *   - if MADT type 5 (LAPIC Address Override) is
                             *     present, that u64 supersedes per ACPI spec.
                             *   - lapic_addr_override_present records whether
                             *     the override path was taken (diagnostics).
                             */
    uint8_t  bsp_apic_id;   /* read from CPUID(0x01).EBX[31:24] at init time */
    uint8_t  lapic_addr_override_present; /* 1 if a type-5 entry was applied */
    uint8_t  _pad_g3b[2];

    uint32_t lapic_nmi_count;

    acpi_cpu_entry_t          cpus[OPENOS_X86_64_ACPI_MAX_CPUS];
    acpi_ioapic_entry_t       ioapics[OPENOS_X86_64_ACPI_MAX_IOAPICS];
    acpi_irq_override_entry_t irq_overrides[OPENOS_X86_64_ACPI_MAX_IRQ_OVERRIDES];
    acpi_lapic_nmi_entry_t    lapic_nmis[OPENOS_X86_64_ACPI_MAX_LAPIC_NMIS];

    /* raw RSDP / table pointers retained for diagnostics */
    uint64_t rsdp_phys;
    uint64_t xsdt_phys;
    uint64_t rsdt_phys;
    uint64_t madt_phys;
} arch_x86_64_acpi_info_t;

int arch_x86_64_acpi_init(void);
const arch_x86_64_acpi_info_t *arch_x86_64_acpi_info(void);
uint32_t arch_x86_64_acpi_cpu_count(void);
uint8_t  arch_x86_64_acpi_bsp_apic_id(void);

/* G.3b helpers: consume MADT data without exposing raw arrays.
 *
 * - first_ioapic_*  : 0 if ACPI not initialized or no IOAPIC declared.
 * - resolve_isa_gsi : looks the legacy ISA `irq` (0..15) up against the
 *                     MADT interrupt-override table; falls back to the
 *                     identity mapping (gsi=irq, flags=0) when no entry
 *                     matches. Returns 1 if an explicit override was
 *                     found, 0 if the identity fallback was used, -1
 *                     when ACPI itself is unavailable. *out args may be
 *                     NULL. flags layout matches MPS:
 *                       bits[1:0] = polarity (00=conform, 01=hi, 11=lo)
 *                       bits[3:2] = trigger  (00=conform, 01=edge, 11=level)
 */
uint64_t arch_x86_64_acpi_first_ioapic_base(void);
uint32_t arch_x86_64_acpi_first_ioapic_gsi_base(void);
int      arch_x86_64_acpi_resolve_isa_gsi(uint8_t irq,
                                          uint32_t *out_gsi,
                                          uint16_t *out_flags);

/* G.3b-final: Local APIC NMI lookup.
 *
 * Given an APIC ID (a.k.a. processor's local APIC ID), find the
 * MADT type-4 entry that applies (acpi_processor_id == that CPU's
 * acpi_processor_id, OR acpi_processor_id == 0xFF meaning "all").
 *
 * Returns:
 *   1  - a matching entry was found, *out_lint / *out_flags written.
 *  -1  - ACPI not initialised yet.
 *   0  - no override applies to this CPU (caller should still program
 *        a sane default; typical PC firmware lists LINT1=NMI for all
 *        CPUs, so this is mostly a defensive path).
 * *out_* may be NULL.
 */
int arch_x86_64_acpi_resolve_lapic_nmi(uint8_t apic_id,
                                       uint8_t *out_lint,
                                       uint16_t *out_flags);

/* ================================================================
 * M8-D.5+: FADT 解析与 GPE 事件支持
 * ================================================================
 *
 * FADT (Fixed ACPI Description Table, sig "FADT" / "APIC" 前缀 "FACP")
 *   - 提供 PM1a/b_EVT/CNT, PM2_CNT, GPE0/1_BLK 等寄存器块的地址和长度
 *   - 是 GPE 事件处理的前提
 *
 * GPE (General Purpose Event)
 *   - GPE0_BLK 是一组通过 ACPI SCI 中断上报的通用事件位
 *   - 每个位对应一个 GPE 号 (0..GPE0_BLK_LEN*8-1)
 *   - 常见用途：I²C HID 设备中断、热插拔、电源按钮等
 *   - GPE 寄存器布局：EN -> STS -> DIS (各占 GPE0_BLK_LEN/2 字节)
 */

/* FADT 中与 GPE/PM 相关的寄存器块描述 */
typedef struct {
    uint32_t valid;            /* 1 once FADT parsed */
    uint16_t pm1a_evt_blk;     /* PM1a Event Register Block */
    uint16_t pm1b_evt_blk;     /* PM1b Event Register Block */
    uint16_t pm1a_cnt_blk;     /* PM1a Control Register Block */
    uint16_t pm1b_cnt_blk;     /* PM1b Control Register Block */
    uint16_t pm2_cnt_blk;      /* PM2 Control Register Block */
    uint16_t gpe0_blk;         /* GPE0 Register Block */
    uint16_t gpe1_blk;         /* GPE1 Register Block */
    uint8_t  pm1_evt_len;      /* PM1 Event Register Length */
    uint8_t  pm1_cnt_len;      /* PM1 Control Register Length */
    uint8_t  pm2_cnt_len;      /* PM2 Control Register Length */
    uint8_t  gpe0_blk_len;     /* GPE0 Block Length (bytes, total of EN+STS) */
    uint8_t  gpe1_blk_len;     /* GPE1 Block Length */
    uint8_t  gpe1_base;        /* GPE1 Base (first GPE number in GPE1) */
    uint8_t  sci_int;          /* SCI interrupt vector (GSI) */
    uint8_t  acpi_enable;      /* ACPI Enable value for SMI_CMD */
    uint8_t  acpi_disable;     /* ACPI Disable value for SMI_CMD */
    uint32_t smi_cmd;          /* SMI Command Port */
    uint8_t  acpi_enabled;     /* 1 if ACPI mode is active */
} acpi_fadt_info_t;

/* GPE handler callback type
 *   gpe_number: the GPE index that fired
 *   context:    user-provided context pointer
 * Returns 0 on success, non-zero to indicate handler error.
 */
typedef int (*acpi_gpe_handler_t)(uint32_t gpe_number, void *context);

#define ACPI_MAX_GPE_HANDLERS  32

/* GPE handler entry */
typedef struct {
    uint32_t            gpe_number;
    acpi_gpe_handler_t  handler;
    void               *context;
    uint8_t             enabled;
} acpi_gpe_handler_entry_t;

/* Initialize FADT parsing and GPE subsystem */
int arch_x86_64_acpi_fadt_init(void);

/* Get FADT info */
const acpi_fadt_info_t *arch_x86_64_acpi_fadt_info(void);

/* Install a GPE handler for a specific GPE number */
int arch_x86_64_acpi_gpe_install_handler(uint32_t gpe_number,
                                          acpi_gpe_handler_t handler,
                                          void *context);

/* Remove a GPE handler */
int arch_x86_64_acpi_gpe_remove_handler(uint32_t gpe_number);

/* Enable a specific GPE (write EN register) */
int arch_x86_64_acpi_gpe_enable(uint32_t gpe_number);

/* Disable a specific GPE */
int arch_x86_64_acpi_gpe_disable(uint32_t gpe_number);

/* Clear GPE status (write STS register to clear) */
int arch_x86_64_acpi_gpe_clear_status(uint32_t gpe_number);

/* Dispatch all pending GPE events (call from SCI handler) */
void arch_x86_64_acpi_gpe_dispatch(void);

/* Get the SCI interrupt vector from FADT (returns 0 if not initialized) */
uint8_t arch_x86_64_acpi_get_sci_vector(void);

/* General table lookup via XSDT/RSDT (signature-based)
 * Returns pointer to the table header, or NULL if not found.
 */
const void *arch_x86_64_acpi_find_table(const char *signature);

/* DSDT table parser for I2C HID device enumeration (M8-D.5)
 * Scans the ACPI DSDT namespace for PNP0C50 HID over I2C devices,
 * extracting their bus address and interrupt resources. Must be
 * called AFTER FADT initialization completes.
 */
int acpi_dsdt_init(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_ARCH_X86_64_ACPI64_H */
