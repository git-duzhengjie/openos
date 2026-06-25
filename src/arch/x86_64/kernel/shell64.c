#include "../include/shell64.h"

#include <stddef.h>

#include "../include/early_console64.h"
#include "../include/vfs64.h"

static uint8_t shell_ready;
static uint8_t init_script_ran;
static uint64_t command_count;
static uint64_t cat_count;

static int shell_starts_with(const char *line, const char *prefix) {
    if (line == NULL || prefix == NULL) {
        return 0;
    }
    while (*prefix) {
        if (*line != *prefix) {
            return 0;
        }
        ++line;
        ++prefix;
    }
    return 1;
}

static void shell_write_bytes(const uint8_t *data, x86_64_size_t size) {
    x86_64_size_t i;
    for (i = 0; i < size; ++i) {
        early_console64_putc((char)data[i]);
    }
}

static void shell_cat(const char *path) {
    const uint8_t *data = NULL;
    x86_64_size_t size = 0;
    ++cat_count;
    if (arch_x86_64_vfs_read_all(path, &data, &size) == 0 && data != NULL) {
        shell_write_bytes(data, size);
        return;
    }
    early_console64_write("[x86_64][shell] cat: missing ");
    early_console64_write(path);
    early_console64_write("\n");
}

static void shell_exec_line(const char *line, x86_64_size_t len) {
    char buffer[96];
    x86_64_size_t i;
    if (line == NULL || len == 0) {
        return;
    }
    if (len >= sizeof(buffer)) {
        len = sizeof(buffer) - 1u;
    }
    for (i = 0; i < len; ++i) {
        buffer[i] = line[i];
    }
    buffer[len] = '\0';
    ++command_count;

    if (shell_starts_with(buffer, "echo ")) {
        early_console64_write(buffer + 5);
        early_console64_write("\n");
    } else if (shell_starts_with(buffer, "cat ")) {
        shell_cat(buffer + 4);
    } else if (buffer[0] != '\0') {
        early_console64_write("[x86_64][shell] unsupported command: ");
        early_console64_write(buffer);
        early_console64_write("\n");
    }
}

void arch_x86_64_shell_init(void) {
    shell_ready = 1u;
    init_script_ran = 0;
    command_count = 0;
    cat_count = 0;
}

void arch_x86_64_shell_run_init(void) {
    const uint8_t *script = NULL;
    x86_64_size_t size = 0;
    x86_64_size_t start = 0;
    x86_64_size_t i;

    if (!shell_ready) {
        return;
    }
    if (arch_x86_64_vfs_read_all("/init", &script, &size) != 0 || script == NULL) {
        early_console64_write("[x86_64][shell] /init not found\n");
        return;
    }

    early_console64_write("[x86_64][shell] running /init\n");
    for (i = 0; i <= size; ++i) {
        if (i == size || script[i] == '\n') {
            shell_exec_line((const char *)(script + start), i - start);
            start = i + 1u;
        }
    }
    init_script_ran = 1u;
}

void arch_x86_64_shell_print_status(void) {
    early_console64_write("[x86_64][shell] ready=");
    early_console64_write_hex64(shell_ready);
    early_console64_write(" init_ran=");
    early_console64_write_hex64(init_script_ran);
    early_console64_write(" commands=");
    early_console64_write_hex64(command_count);
    early_console64_write(" cats=");
    early_console64_write_hex64(cat_count);
    early_console64_write("\n");
}
