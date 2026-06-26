#ifndef OPENOS_ARCH_X86_64_APIC_SELFTEST64_H
#define OPENOS_ARCH_X86_64_APIC_SELFTEST64_H

#include <stdbool.h>

/* Step G.1.3 — bring up LAPIC + IOAPIC and switch IRQ0 routing from the
 * 8259A to the IOAPIC.
 *
 * Returns true on success (IRQ0 is now LAPIC-routed; PIC is masked off).
 * Returns false if any init step fails; caller may keep the legacy PIC
 * path. Output is logged via early_console64 either way. */
bool arch_x86_64_apic_selftest_run(void);

#endif /* OPENOS_ARCH_X86_64_APIC_SELFTEST64_H */
