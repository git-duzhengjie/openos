/*
 * power64.c — M6.1a: ACPI FADT + \_S5 discovery.
 *
 * Locates the FADT ("FACP") via the ACPI XSDT/RSDT (reusing the addresses
 * captured by acpi64.c), extracts the PM1 control register blocks and the
 * SMI command port, decodes the DSDT \_S5 sleep package, and captures the
 * reset register. All physical addresses are identity-mapped in this kernel,
 * matching the convention used by acpi64.c.
 *
 * This file intentionally does NOT perform the actual shutdown / reboot;
 * those live in M6.1b / M6.1c. Here we only build the info snapshot.
 */

#include "power64.h"
#include "acpi64.h"
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Minimal ACPI table structs (subset of what we need for the FADT)    */
/* ------------------------------------------------------------------ */

typedef struct __attribute__((packed)) {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} power_sdt_header_t;

/* Generic Address Structure as it appears on disk in the FADT (packed). */
typedef struct __attribute__((packed)) {
    uint8_t  address_space_id;
    uint8_t  register_bit_width;
    uint8_t  register_bit_offset;
    uint8_t  access_size;
    uint64_t address;
} power_gas_disk_t;

/*
 * FADT layout (ACPI 6.x §5.2.9). We only declare fields up to and including
 * the reset register + X_ block; trailing fields are ignored. Note offsets
 * are fixed by the spec so this packed struct is safe to overlay.
 */
typedef struct __attribute__((packed)) {
    power_sdt_header_t hdr;          /* 0   */
    uint32_t firmware_ctrl;          /* 36  */
    uint32_t dsdt;                   /* 40  */
    uint8_t  reserved0;              /* 44  */
    uint8_t  preferred_pm_profile;   /* 45  */
    uint16_t sci_int;                /* 46  */
    uint32_t smi_cmd;                /* 48  */
    uint8_t  acpi_enable;            /* 52  */
    uint8_t  acpi_disable;           /* 53  */
    uint8_t  s4bios_req;             /* 54  */
    uint8_t  pstate_cnt;             /* 55  */
    uint32_t pm1a_evt_blk;           /* 56  */
    uint32_t pm1b_evt_blk;           /* 60  */
    uint32_t pm1a_cnt_blk;           /* 64  */
    uint32_t pm1b_cnt_blk;           /* 68  */
    uint32_t pm2_cnt_blk;            /* 72  */
    uint32_t pm_tmr_blk;             /* 76  */
    uint32_t gpe0_blk;               /* 80  */
    uint32_t gpe1_blk;               /* 84  */
    uint8_t  pm1_evt_len;            /* 88  */
    uint8_t  pm1_cnt_len;            /* 89  */
    uint8_t  pm2_cnt_len;            /* 90  */
    uint8_t  pm_tmr_len;             /* 91  */
    uint8_t  gpe0_blk_len;           /* 92  */
    uint8_t  gpe1_blk_len;           /* 93  */
    uint8_t  gpe1_base;              /* 94  */
    uint8_t  cst_cnt;                /* 95  */
    uint16_t p_lvl2_lat;             /* 96  */
    uint16_t p_lvl3_lat;             /* 98  */
    uint16_t flush_size;             /* 100 */
    uint16_t flush_stride;           /* 102 */
    uint8_t  duty_offset;            /* 104 */
    uint8_t  duty_width;             /* 105 */
    uint8_t  day_alrm;               /* 106 */
    uint8_t  mon_alrm;               /* 107 */
    uint8_t  century;                /* 108 */
    uint16_t iapc_boot_arch;         /* 109 */
    uint8_t  reserved1;              /* 111 */
    uint32_t flags;                  /* 112 */
    power_gas_disk_t reset_reg;      /* 116 */
    uint8_t  reset_value;            /* 128 */
    uint16_t arm_boot_arch;          /* 129 */
    uint8_t  fadt_minor_version;     /* 131 */
    uint64_t x_firmware_ctrl;        /* 132 */
    uint64_t x_dsdt;                 /* 140 */
    /* ... remaining X_ blocks not needed here ... */
} power_fadt_t;

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */

static arch_x86_64_power_info_t g_power_info;
static int                      g_power_ready = 0;

/* ------------------------------------------------------------------ */
/* Small helpers (kept local to avoid coupling with acpi64.c internals)*/
/* ------------------------------------------------------------------ */

static int power_sig_eq4(const char *a, const char *b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static uint8_t power_checksum(const void *base, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)base;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; ++i) sum = (uint8_t)(sum + p[i]);
    return sum;
}

static void power_zero_info(arch_x86_64_power_info_t *o)
{
    uint8_t *p = (uint8_t *)o;
    for (unsigned i = 0; i < sizeof(*o); ++i) p[i] = 0;
}

/* ------------------------------------------------------------------ */
/* FADT discovery: walk XSDT (preferred) or RSDT for sig "FACP".        */
/* ------------------------------------------------------------------ */

static const power_fadt_t *power_find_fadt(void)
{
    const arch_x86_64_acpi_info_t *ai = arch_x86_64_acpi_info();
    if (!ai) return NULL;

    /* Prefer the 64-bit XSDT if present. */
    if (ai->xsdt_phys) {
        const power_sdt_header_t *xsdt =
            (const power_sdt_header_t *)(uintptr_t)ai->xsdt_phys;
        if (power_sig_eq4(xsdt->signature, "XSDT") &&
            power_checksum(xsdt, xsdt->length) == 0) {
            uint32_t n = (xsdt->length - (uint32_t)sizeof(*xsdt)) / 8u;
            const uint64_t *ents =
                (const uint64_t *)((const uint8_t *)xsdt + sizeof(*xsdt));
            for (uint32_t i = 0; i < n; ++i) {
                const power_sdt_header_t *h =
                    (const power_sdt_header_t *)(uintptr_t)ents[i];
                if (power_sig_eq4(h->signature, "FACP") &&
                    power_checksum(h, h->length) == 0) {
                    return (const power_fadt_t *)h;
                }
            }
        }
    }

    /* Fall back to the 32-bit RSDT. */
    if (ai->rsdt_phys) {
        const power_sdt_header_t *rsdt =
            (const power_sdt_header_t *)(uintptr_t)ai->rsdt_phys;
        if (power_sig_eq4(rsdt->signature, "RSDT") &&
            power_checksum(rsdt, rsdt->length) == 0) {
            uint32_t n = (rsdt->length - (uint32_t)sizeof(*rsdt)) / 4u;
            const uint32_t *ents =
                (const uint32_t *)((const uint8_t *)rsdt + sizeof(*rsdt));
            for (uint32_t i = 0; i < n; ++i) {
                const power_sdt_header_t *h =
                    (const power_sdt_header_t *)(uintptr_t)ents[i];
                if (power_sig_eq4(h->signature, "FACP") &&
                    power_checksum(h, h->length) == 0) {
                    return (const power_fadt_t *)h;
                }
            }
        }
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/* DSDT \_S5 decode. Classic osdev AML pattern match (no interpreter).  */
/*                                                                     */
/* We look for the byte sequence for "_S5_" followed by a PackageOp    */
/* (0x12), then decode the first two integers (SLP_TYPa / SLP_TYPb).   */
/* ------------------------------------------------------------------ */

static int power_decode_s5(uint64_t dsdt_phys,
                           uint8_t *out_a, uint8_t *out_b)
{
    if (!dsdt_phys) return 0;
    const power_sdt_header_t *dsdt =
        (const power_sdt_header_t *)(uintptr_t)dsdt_phys;
    if (!power_sig_eq4(dsdt->signature, "DSDT")) return 0;

    const uint8_t *aml = (const uint8_t *)dsdt + sizeof(*dsdt);
    uint32_t aml_len = (dsdt->length > (uint32_t)sizeof(*dsdt))
                           ? (dsdt->length - (uint32_t)sizeof(*dsdt)) : 0;

    for (uint32_t i = 0; i + 4 < aml_len; ++i) {
        if (aml[i] == '_' && aml[i + 1] == 'S' &&
            aml[i + 2] == '5' && aml[i + 3] == '_') {
            /* After "_S5_" comes an optional NameOp already consumed by the
             * scan window; skip forward to the PackageOp 0x12. Allow a few
             * bytes of slack for the enclosing Name()/Package prefix. */
            uint32_t j = i + 4;
            uint32_t limit = j + 8;
            while (j < aml_len && j < limit && aml[j] != 0x12) ++j;
            if (j >= aml_len || aml[j] != 0x12) continue;
            ++j;                       /* consume PackageOp 0x12          */
            /* PkgLength: high 2 bits of first byte give #following bytes. */
            if (j >= aml_len) continue;
            uint8_t pkg_lead = aml[j];
            uint32_t pkg_extra = (uint32_t)(pkg_lead >> 6);
            j += 1 + pkg_extra;        /* skip PkgLength encoding          */
            if (j >= aml_len) continue;
            /* NumElements byte. */
            ++j;
            if (j >= aml_len) continue;

            /* Decode SLP_TYPa: could be a raw byte, or BytePrefix (0x0A). */
            uint8_t va = 0, vb = 0;
            if (aml[j] == 0x0A) { ++j; if (j >= aml_len) continue; }
            else if (aml[j] == 0x00 || aml[j] == 0x01) { /* ZeroOp/OneOp */ }
            va = aml[j];
            if (aml[j] == 0x00) va = 0;
            else if (aml[j] == 0x01) va = 1;
            ++j;
            if (j < aml_len) {
                if (aml[j] == 0x0A) { ++j; }
                if (j < aml_len) {
                    vb = aml[j];
                    if (aml[j] == 0x00) vb = 0;
                    else if (aml[j] == 0x01) vb = 1;
                }
            }
            *out_a = (uint8_t)(va & 0x07);
            *out_b = (uint8_t)(vb & 0x07);
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int arch_x86_64_power_init(void)
{
    if (g_power_ready) return 1;

    const power_fadt_t *fadt = power_find_fadt();
    if (!fadt) return 0;

    power_zero_info(&g_power_info);

    g_power_info.fadt_phys      = (uint64_t)(uintptr_t)fadt;
    g_power_info.pm1a_cnt_port  = fadt->pm1a_cnt_blk;
    g_power_info.pm1b_cnt_port  = fadt->pm1b_cnt_blk;
    g_power_info.smi_cmd_port   = fadt->smi_cmd;
    g_power_info.acpi_enable_val  = fadt->acpi_enable;
    g_power_info.acpi_disable_val = fadt->acpi_disable;
    g_power_info.pm1_cnt_width_bits = 16;

    /* Prefer the 64-bit X_DSDT if present, else the 32-bit DSDT. */
    uint64_t dsdt_phys = fadt->x_dsdt ? fadt->x_dsdt
                                      : (uint64_t)fadt->dsdt;
    g_power_info.dsdt_phys = dsdt_phys;

    /* Reset register (ACPI 2.0+): only trust it if the FADT is long enough
     * to actually contain the field and the register looks sane. */
    if (fadt->hdr.length >= offsetof(power_fadt_t, reset_value) + 1) {
        g_power_info.reset_reg.address_space_id    = fadt->reset_reg.address_space_id;
        g_power_info.reset_reg.register_bit_width  = fadt->reset_reg.register_bit_width;
        g_power_info.reset_reg.register_bit_offset = fadt->reset_reg.register_bit_offset;
        g_power_info.reset_reg.access_size         = fadt->reset_reg.access_size;
        g_power_info.reset_reg.address             = fadt->reset_reg.address;
        g_power_info.reset_value                   = fadt->reset_value;
        g_power_info.reset_supported =
            (fadt->reset_reg.address != 0) ? 1 : 0;
    }

    /* Decode \_S5 for soft-off. */
    uint8_t a = 0, b = 0;
    if (power_decode_s5(dsdt_phys, &a, &b)) {
        g_power_info.s5_slp_typ_a = a;
        g_power_info.s5_slp_typ_b = b;
        g_power_info.s5_valid = 1;
    }

    g_power_info.valid = 1;
    g_power_ready = 1;
    return 1;
}

const arch_x86_64_power_info_t *arch_x86_64_power_info(void)
{
    return g_power_ready ? &g_power_info : NULL;
}

/* ------------------------------------------------------------------ */
/* Port I/O helpers                                                    */
/* ------------------------------------------------------------------ */

static inline void power_outb(uint16_t port, uint8_t val)
{
    __asm__ __volatile__("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline void power_outw(uint16_t port, uint16_t val)
{
    __asm__ __volatile__("outw %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t power_inb(uint16_t port)
{
    uint8_t v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void power_halt_forever(void)
{
    for (;;) __asm__ __volatile__("cli; hlt");
}

/* ------------------------------------------------------------------ */
/* M6.1b — ACPI S5 soft-off                                            */
/* ------------------------------------------------------------------ */

void arch_x86_64_power_shutdown(void)
{
    /* Ensure the FADT/\_S5 snapshot is available. */
    if (!g_power_ready) (void)arch_x86_64_power_init();
    const arch_x86_64_power_info_t *pi = arch_x86_64_power_info();

    if (pi && pi->valid && pi->s5_valid && pi->pm1a_cnt_port) {
        /* Optionally switch the chipset into ACPI mode via SMI_CMD. */
        if (pi->smi_cmd_port && pi->acpi_enable_val) {
            power_outb((uint16_t)pi->smi_cmd_port, pi->acpi_enable_val);
            /* Small settle delay: read PM1a a few times. */
            for (int i = 0; i < 100000; ++i) __asm__ __volatile__("pause");
        }

        uint16_t a = (uint16_t)(((uint16_t)pi->s5_slp_typ_a
                                    << POWER_PM1_CNT_SLP_TYP_SHIFT)
                                   & POWER_PM1_CNT_SLP_TYP_MASK);
        a |= POWER_PM1_CNT_SLP_EN;
        power_outw((uint16_t)pi->pm1a_cnt_port, a);

        if (pi->pm1b_cnt_port) {
            uint16_t b = (uint16_t)(((uint16_t)pi->s5_slp_typ_b
                                        << POWER_PM1_CNT_SLP_TYP_SHIFT)
                                       & POWER_PM1_CNT_SLP_TYP_MASK);
            b |= POWER_PM1_CNT_SLP_EN;
            power_outw((uint16_t)pi->pm1b_cnt_port, b);
        }

        /* If we get here ACPI soft-off did not take effect immediately;
         * fall through to the debug-exit fallbacks below. */
    }

    /* QEMU/Bochs debug shutdown ports (isa-debug-exit / acpi shutdown). */
    power_outw(0x604, 0x2000);   /* QEMU ACPI PM1a soft-off              */
    power_outw(0xB004, 0x2000);  /* Bochs / older QEMU                   */
    power_outw(0x4004, 0x3400);  /* QEMU newer virt shutdown             */

    power_halt_forever();
}

/* ------------------------------------------------------------------ */
/* M6.1c — warm reboot                                                 */
/* ------------------------------------------------------------------ */

static void power_reset_via_fadt(const arch_x86_64_power_info_t *pi)
{
    if (!pi || !pi->reset_supported) return;
    if (pi->reset_reg.address_space_id == POWER_GAS_ASID_SYSTEM_IO) {
        power_outb((uint16_t)pi->reset_reg.address, pi->reset_value);
    } else if (pi->reset_reg.address_space_id ==
               POWER_GAS_ASID_SYSTEM_MEMORY) {
        volatile uint8_t *mmio =
            (volatile uint8_t *)(uintptr_t)pi->reset_reg.address;
        *mmio = pi->reset_value;
    }
}

static void power_reset_via_8042(void)
{
    /* Wait for the input buffer to drain, then pulse the CPU reset line. */
    for (int i = 0; i < 100000; ++i) {
        if ((power_inb(0x64) & 0x02) == 0) break;
        __asm__ __volatile__("pause");
    }
    power_outb(0x64, 0xFE);
}

static void power_triple_fault(void)
{
    /* Load a null IDT and raise an interrupt => unrecoverable => reset. */
    struct __attribute__((packed)) {
        uint16_t limit;
        uint64_t base;
    } null_idt = { 0, 0 };
    __asm__ __volatile__("lidt %0" :: "m"(null_idt));
    __asm__ __volatile__("int3");
}

void arch_x86_64_power_reboot(void)
{
    if (!g_power_ready) (void)arch_x86_64_power_init();
    const arch_x86_64_power_info_t *pi = arch_x86_64_power_info();

    /* 1) Preferred: FADT reset register. */
    power_reset_via_fadt(pi);

    /* 2) Fallback: 8042 keyboard-controller reset pulse. */
    power_reset_via_8042();

    /* 3) Last resort: triple fault. */
    power_triple_fault();

    power_halt_forever();
}
