/* G.7a — per-CPU GDT unification.
 *
 * The legacy gdt64.c built a single, global GDT shared by every CPU and
 * embedded a single TSS descriptor in slots 6/7. percpu64.c now owns a
 * per-CPU GDT (with that CPU's own TSS descriptor) and percpu_load
 * commits it via lgdt + ltr in one critical section. To avoid a window
 * where the BSP runs with the old global GDT and then switches to its
 * percpu GDT (re-loading selectors twice), we collapse this layer into
 * a no-op: arch_x86_64_tss_load() in tss64.c invokes percpu_load on
 * cpu0 which performs the full lgdt+ltr+segment-reload sequence.
 *
 * The public surface (gdt_init/print_status and the selector getters)
 * is preserved so
 * we don't have to touch every caller in compat32.c, syscall64.c, etc.
 * The selector accessors return the well-known constants from gdt64.h.
 */

#include "../include/gdt64.h"
#include "../include/early_console64.h"
#include "../include/percpu64.h"

void arch_x86_64_gdt_init(void) {
    /* No-op: percpu_setup builds each CPU's GDT, percpu_load installs
     * it. tss_init() now wraps percpu_setup(0); the BSP early-init
     * sequence (kernel64.c) still calls tss_init → gdt_init → tss_load,
     * with gdt_init landing here. Keeping it as an explicit empty
     * symbol means we don't have to rewrite the early-init order yet. */
}

void arch_x86_64_gdt_print_status(void) {
    /* The percpu module owns the actual GDT bytes. Print the kernel
     * selectors as constants and dump cpu0's TSS base/limit so this
     * stays observably equivalent to the historical output. */
    early_console64_write("[x86_64][gdt] kernel_code=");
    early_console64_write_hex64((uint64_t)
        arch_x86_64_gdt_kernel_code_selector());
    early_console64_write(" kernel_data=");
    early_console64_write_hex64((uint64_t)
        arch_x86_64_gdt_kernel_data_selector());
    early_console64_write(" user_code=");
    early_console64_write_hex64((uint64_t)
        arch_x86_64_gdt_user_code_selector());
    early_console64_write(" user_data=");
    early_console64_write_hex64((uint64_t)
        arch_x86_64_gdt_user_data_selector());
    early_console64_write(" tss=");
    early_console64_write_hex64((uint64_t)
        arch_x86_64_gdt_tss_selector());
    early_console64_write(" cpu0_tss_base=");
    early_console64_write_hex64((uint64_t)
        arch_x86_64_percpu_tss_base(0u));
    early_console64_write("\n");
}

uint16_t arch_x86_64_gdt_kernel_code_selector(void) {
    return (uint16_t)OPENOS_X86_64_GDT_KERNEL_CODE;
}

uint16_t arch_x86_64_gdt_kernel_data_selector(void) {
    return (uint16_t)OPENOS_X86_64_GDT_KERNEL_DATA;
}

uint16_t arch_x86_64_gdt_user_code_selector(void) {
    return (uint16_t)OPENOS_X86_64_GDT_USER_CODE;
}

uint16_t arch_x86_64_gdt_user32_code_selector(void) {
    return (uint16_t)OPENOS_X86_64_GDT_USER32_CODE;
}

uint16_t arch_x86_64_gdt_user_data_selector(void) {
    return (uint16_t)OPENOS_X86_64_GDT_USER_DATA;
}

uint16_t arch_x86_64_gdt_tss_selector(void) {
    return (uint16_t)OPENOS_X86_64_GDT_TSS;
}
