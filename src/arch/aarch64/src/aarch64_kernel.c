#include "aarch64_kernel.h"
#include "aarch64_exception.h"
#include "aarch64_initrd.h"
#include "aarch64_memory.h"
#include "aarch64_platform.h"
#include "aarch64_selftest.h"
#include "aarch64_shell.h"
#include "aarch64_uart.h"
#include "aarch64_usermode.h"
#include "aarch64_vfs.h"

static void aarch64_uart_write_hex(unsigned long value) {
    int shift;
    aarch64_uart_write("0x");
    for (shift = (int)(sizeof(unsigned long) * 8u) - 4; shift >= 0; shift -= 4) {
        unsigned int digit = (unsigned int)((value >> shift) & 0xFu);
        aarch64_uart_putc((char)(digit < 10u ? ('0' + digit) : ('A' + digit - 10u)));
    }
}

void aarch64_kernel_main(unsigned long dtb) {
    aarch64_uart_init();
    aarch64_exception_init();
    aarch64_uart_write("OpenOS aarch64 QEMU virt minimal boot\n");
    aarch64_uart_write("DTB: ");
    aarch64_uart_write_hex(dtb);
    aarch64_uart_write("\n");
    aarch64_uart_write("A5.2: _start -> EL1 stack/BSS -> PL011 log OK\n");
    aarch64_uart_write("A5.3: EL1 exception vector installed\n");
    aarch64_memory_init();
    aarch64_memory_print_status();
    aarch64_platform_init(dtb);
    const aarch64_platform_state_t *platform = aarch64_platform_get_state();
    aarch64_uart_write("A5.4: generic timer frequency: ");
    aarch64_uart_write_hex(platform->timer_frequency_hz);
    aarch64_uart_write(" Hz\n");
    if (aarch64_run_embedded_hello64() == 0) {
        aarch64_uart_write("A5.5: hello64 ELF staged for EL0\n");
    }

    aarch64_initrd_init();
    aarch64_initrd_print_status();
    aarch64_vfs_init();
    if (aarch64_vfs_mount_initrd(aarch64_initrd_get_image()) == 0) {
        aarch64_vfs_print_status();
        aarch64_shell_init();
        aarch64_shell_run_init();
        aarch64_shell_print_status();
        aarch64_uart_write("A5.6: aarch64 initrd -> VFS -> /bin/init -> /bin/sh staged\n");
    } else {
        aarch64_uart_write("A5.6: failed to mount aarch64 initrd\n");
    }

    /* M11-Z: aarch64 platform selftest (DTB / GICv3 / Timer IRQ / I2C / GT911). */
    (void)aarch64_platform_selftest_run();

    for (;;) {
        __asm__ volatile ("wfe");
    }
}
