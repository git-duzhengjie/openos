#include "aarch64_usermode.h"

#include "aarch64_memory.h"
#include "aarch64_syscall.h"
#include "aarch64_uart.h"

extern const unsigned char __aarch64_hello64_elf_start[];
extern const unsigned char __aarch64_hello64_elf_end[];

static aarch64_user_process_t last_process;
static uint32_t next_pid = 1u;

void aarch64_usermode_init(void)
{
    last_process.entry = 0;
    last_process.stack_bottom = 0;
    last_process.stack_top = 0;
    last_process.pid = 0;
    last_process.exited = 0;
    last_process.exit_code = 0;
    next_pid = 1u;
}

int aarch64_user_process_create_from_elf(const aarch64_elf64_image_t *image, aarch64_user_process_t *process)
{
    void *stack;

    if (image == 0 || process == 0 || image->entry == 0u) {
        return -1;
    }

    stack = aarch64_heap_alloc(AARCH64_USER_STACK_SIZE, 16u);
    if (stack == 0) {
        return -2;
    }

    process->entry = image->entry;
    process->stack_bottom = (uintptr_t)stack;
    process->stack_top = (uintptr_t)stack + AARCH64_USER_STACK_SIZE;
    process->pid = next_pid++;
    process->exited = 0;
    process->exit_code = 0;
    return 0;
}

void aarch64_user_enter_el0(uintptr_t entry, uintptr_t stack_top)
{
    aarch64_uart_write("A5.5: EL0 entry prepared\n");
    __asm__ volatile(
        "msr elr_el1, %0\n"
        "msr sp_el0, %1\n"
        "mov x2, #0\n"
        "msr spsr_el1, x2\n"
        "isb\n"
        :
        : "r"(entry), "r"(stack_top)
        : "x2", "memory");
}

int aarch64_run_embedded_hello64(void)
{
    aarch64_elf64_image_t image;
    size_t elf_size;
    int rc;

    aarch64_usermode_init();

    elf_size = (size_t)(__aarch64_hello64_elf_end - __aarch64_hello64_elf_start);
    rc = aarch64_elf64_load(__aarch64_hello64_elf_start, elf_size, &image);
    if (rc != 0) {
        aarch64_uart_write("A5.5: hello64 ELF64 load failed\n");
        return rc;
    }

    rc = aarch64_user_process_create_from_elf(&image, &last_process);
    if (rc != 0) {
        aarch64_uart_write("A5.5: hello64 process create failed\n");
        return rc;
    }

    aarch64_elf64_print_image(&image);
    aarch64_user_enter_el0(last_process.entry, last_process.stack_top);
    aarch64_uart_write("A5.5: hello64 process staged\n");
    return 0;
}

const aarch64_user_process_t *aarch64_usermode_last_process(void)
{
    return &last_process;
}
