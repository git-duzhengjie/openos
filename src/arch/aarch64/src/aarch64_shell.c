#include "aarch64_shell.h"

#include <stddef.h>
#include <stdint.h>

#include "aarch64_uart.h"
#include "aarch64_vfs.h"

static uint8_t aarch64_shell_ready;
static uint8_t aarch64_shell_init_ran;

static void aarch64_shell_write_file(const char *path) {
    const uint8_t *data = 0;
    size_t size = 0;
    if (aarch64_vfs_read_all(path, &data, &size) != 0 || !data) {
        aarch64_uart_write("[aarch64][shell] missing ");
        aarch64_uart_write(path);
        aarch64_uart_write("\n");
        return;
    }

    for (size_t i = 0; i < size; ++i) {
        aarch64_uart_putc((char)data[i]);
    }
    if (size == 0 || data[size - 1u] != '\n') {
        aarch64_uart_putc('\n');
    }
}

void aarch64_shell_init(void) {
    aarch64_shell_ready = 1;
    aarch64_shell_init_ran = 0;
}

void aarch64_shell_run_init(void) {
    if (!aarch64_shell_ready) {
        return;
    }

    aarch64_uart_write("[aarch64][init] starting /bin/init\n");
    aarch64_shell_write_file("/bin/init");
    aarch64_uart_write("[aarch64][init] launching /bin/sh\n");
    aarch64_shell_write_file("/etc/motd");
    aarch64_shell_write_file("/bin/sh");
    aarch64_uart_write("openos-aarch64$ ");
    aarch64_shell_init_ran = 1;
}

void aarch64_shell_print_status(void) {
    aarch64_uart_write("[aarch64][shell] ready=");
    aarch64_uart_putc(aarch64_shell_ready ? '1' : '0');
    aarch64_uart_write(" init_ran=");
    aarch64_uart_putc(aarch64_shell_init_ran ? '1' : '0');
    aarch64_uart_write("\n");
}
