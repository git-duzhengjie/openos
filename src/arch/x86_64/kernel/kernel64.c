#include "../include/arch64.h"
#include "../include/compat32.h"
#include "../include/early_console64.h"
#include "../include/elf64_loader.h"
#include "../include/gdt64.h"
#include "../include/heap64.h"
#include "../include/idt64.h"
#include "../include/pmm64.h"
#include "../include/sched64.h"
#include "../include/syscall64.h"
#include "../include/tss64.h"
#include "../include/usermode64.h"
#include "../include/vmm64.h"

static const char x86_64_boot_log[] = "[x86_64] OpenOS entered kernel_main64\n";
static const char x86_64_console_log[] = "[x86_64] early console: serial + VGA ready\n";
static const char x86_64_pmm_log[] = "[x86_64] physical memory manager ready\n";
static const char x86_64_vmm_log[] = "[x86_64] 4-level virtual memory manager ready\n";
static const char x86_64_heap_log[] = "[x86_64] kernel heap allocator ready\n";
static const char x86_64_elf64_log[] = "[x86_64] ELF64 loader ready\n";
static const char x86_64_usermode_log[] = "[x86_64] usermode iretq return ready\n";
static const char x86_64_compat32_log[] = "[x86_64] compat32 evaluation ready\n";

void arch_x86_64_early_init(void) {
    arch_x86_64_tss_init();
    arch_x86_64_gdt_init();
    arch_x86_64_tss_load();
    arch_x86_64_idt_init();
    early_console64_init();
    arch_x86_64_sched_init();
    arch_x86_64_syscall_init();
    arch_x86_64_pmm_init(0, 0);
    arch_x86_64_vmm_init();
    arch_x86_64_heap_init();
    arch_x86_64_elf64_loader_init();
    arch_x86_64_usermode_init();
    arch_x86_64_compat32_init();
}

void kernel_main64(void) {
    arch_x86_64_early_init();
    early_console64_write(x86_64_boot_log);
    early_console64_write(x86_64_console_log);
    arch_x86_64_sched_print_status();
    arch_x86_64_syscall_print_status();
    early_console64_write(x86_64_pmm_log);
    arch_x86_64_pmm_print_status();
    early_console64_write(x86_64_vmm_log);
    arch_x86_64_vmm_print_status();
    early_console64_write(x86_64_heap_log);
    arch_x86_64_heap_print_status();
    early_console64_write(x86_64_elf64_log);
    arch_x86_64_elf64_loader_print_status();
    early_console64_write(x86_64_usermode_log);
    arch_x86_64_usermode_print_status();
    early_console64_write(x86_64_compat32_log);
    arch_x86_64_compat32_print_status();
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
