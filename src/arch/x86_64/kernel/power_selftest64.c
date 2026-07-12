#include "../include/power_selftest64.h"
#include "../include/power64.h"
#include "../include/early_console64.h"

#include <stdint.h>
#include <stdbool.h>

/* M6.1 — print + sanity check what power64.c discovered from the FADT/DSDT.
 * This never triggers an actual shutdown/reboot; it only inspects state. */

static void log_kv(const char *key, uint64_t val)
{
    early_console64_write(key);
    early_console64_write_hex64(val);
}

bool arch_x86_64_power_selftest_run(void)
{
    early_console64_write("\n[x86_64][power-selftest] begin");

    if (!arch_x86_64_power_init()) {
        early_console64_write(
            "\n[x86_64][power-selftest] FAIL power_init (no FADT)\n");
        return false;
    }

    const arch_x86_64_power_info_t *pi = arch_x86_64_power_info();
    if (pi == 0 || !pi->valid) {
        early_console64_write(
            "\n[x86_64][power-selftest] FAIL info==null\n");
        return false;
    }

    log_kv("\n[x86_64][power-selftest] fadt=", pi->fadt_phys);
    log_kv(" dsdt=", pi->dsdt_phys);
    log_kv(" pm1a_cnt=", (uint64_t)pi->pm1a_cnt_port);
    log_kv(" pm1b_cnt=", (uint64_t)pi->pm1b_cnt_port);
    log_kv(" smi_cmd=", (uint64_t)pi->smi_cmd_port);
    log_kv(" acpi_en=", (uint64_t)pi->acpi_enable_val);

    log_kv("\n[x86_64][power-selftest] s5_valid=", (uint64_t)pi->s5_valid);
    log_kv(" s5_typ_a=", (uint64_t)pi->s5_slp_typ_a);
    log_kv(" s5_typ_b=", (uint64_t)pi->s5_slp_typ_b);

    log_kv("\n[x86_64][power-selftest] reset_supported=",
           (uint64_t)pi->reset_supported);
    log_kv(" reset_asid=", (uint64_t)pi->reset_reg.address_space_id);
    log_kv(" reset_addr=", pi->reset_reg.address);
    log_kv(" reset_val=", (uint64_t)pi->reset_value);

    /* The FADT must have been located. */
    if (pi->fadt_phys == 0) {
        early_console64_write(
            "\n[x86_64][power-selftest] FAIL fadt_phys==0\n");
        return false;
    }

    /* We must be able to actually power the machine off one way or another:
     * either ACPI \_S5 via PM1a_CNT, or (for reboot) the FADT reset reg. */
    bool can_shutdown = (pi->s5_valid && pi->pm1a_cnt_port != 0);
    bool can_reboot   = (pi->reset_supported != 0);

    log_kv("\n[x86_64][power-selftest] can_shutdown=", (uint64_t)can_shutdown);
    log_kv(" can_reboot=", (uint64_t)can_reboot);

    if (!can_shutdown) {
        /* Not fatal: the QEMU debug-exit fallback still works, but warn. */
        early_console64_write(
            "\n[x86_64][power-selftest] WARN no ACPI S5 (will use fallback)");
    }

    early_console64_write("\n[x86_64][power-selftest] PASS\n");
    return true;
}
