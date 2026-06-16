#include "../include/compat32.h"

#include "../include/early_console64.h"
#include "../include/gdt64.h"

static x86_64_compat32_info_t compat32_info;

void arch_x86_64_compat32_init(void) {
    compat32_info.user32_code_selector = arch_x86_64_gdt_user32_code_selector();
    compat32_info.user32_data_selector = arch_x86_64_gdt_user_data_selector();
    compat32_info.int80_entry_available = 1u;
    compat32_info.elf32_loader_required = 1u;
    compat32_info.user_stack_required = 1u;
    compat32_info.pointer_thunk_required = 1u;

    compat32_info.status_flags = OPENOS_X86_64_COMPAT32_STATUS_READY |
                                 OPENOS_X86_64_COMPAT32_STATUS_GDT |
                                 OPENOS_X86_64_COMPAT32_STATUS_INT80 |
                                 OPENOS_X86_64_COMPAT32_STATUS_ELF32_TODO |
                                 OPENOS_X86_64_COMPAT32_STATUS_STACK_TODO |
                                 OPENOS_X86_64_COMPAT32_STATUS_PTR_TODO;

    if (compat32_info.user32_code_selector == 0 || compat32_info.user32_data_selector == 0 ||
        compat32_info.int80_entry_available == 0) {
        compat32_info.status_flags &= ~OPENOS_X86_64_COMPAT32_STATUS_READY;
    }
}

const x86_64_compat32_info_t *arch_x86_64_compat32_get_info(void) {
    return &compat32_info;
}

int arch_x86_64_compat32_is_ready(void) {
    return (compat32_info.status_flags & OPENOS_X86_64_COMPAT32_STATUS_READY) != 0;
}

void arch_x86_64_compat32_print_status(void) {
    early_console64_write("[x86_64][compat32] eval ready=");
    early_console64_write_hex64((uint64_t)arch_x86_64_compat32_is_ready());
    early_console64_write(" user32_cs=");
    early_console64_write_hex64(compat32_info.user32_code_selector);
    early_console64_write(" user32_ds=");
    early_console64_write_hex64(compat32_info.user32_data_selector);
    early_console64_write(" int80=");
    early_console64_write_hex64(compat32_info.int80_entry_available);
    early_console64_write(" todo=elf32,stack,ptr-thunk\n");
}
