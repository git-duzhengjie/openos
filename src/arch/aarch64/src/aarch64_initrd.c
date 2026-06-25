#include "aarch64_initrd.h"

#include "aarch64_uart.h"

static const uint8_t aarch64_init_program[] =
    "#!/bin/openos-init\n"
    "mount /\n"
    "exec /bin/sh\n";

static const uint8_t aarch64_shell_program[] =
    "#!/bin/openos-sh\n"
    "echo OpenOS aarch64 shell ready\n"
    "help\n";

static const uint8_t aarch64_motd[] =
    "Welcome to OpenOS aarch64 minimal rootfs\n";

static const aarch64_initrd_file_t aarch64_initrd_files[] = {
    { "/bin/init", aarch64_init_program, sizeof(aarch64_init_program) - 1u, 0755u },
    { "/bin/sh", aarch64_shell_program, sizeof(aarch64_shell_program) - 1u, 0755u },
    { "/etc/motd", aarch64_motd, sizeof(aarch64_motd) - 1u, 0644u },
};

static aarch64_initrd_image_t aarch64_initrd_image;

static int aarch64_streq(const char *lhs, const char *rhs) {
    if (!lhs || !rhs) {
        return 0;
    }

    while (*lhs && *rhs) {
        if (*lhs != *rhs) {
            return 0;
        }
        ++lhs;
        ++rhs;
    }

    return *lhs == *rhs;
}

void aarch64_initrd_init(void) {
    aarch64_initrd_image.magic = OPENOS_AARCH64_INITRD_MAGIC;
    aarch64_initrd_image.file_count = (uint32_t)(sizeof(aarch64_initrd_files) / sizeof(aarch64_initrd_files[0]));
    aarch64_initrd_image.files = aarch64_initrd_files;
}

const aarch64_initrd_image_t *aarch64_initrd_get_image(void) {
    return &aarch64_initrd_image;
}

const aarch64_initrd_file_t *aarch64_initrd_find(const char *path) {
    const aarch64_initrd_image_t *image = aarch64_initrd_get_image();
    if (!image || image->magic != OPENOS_AARCH64_INITRD_MAGIC || !path) {
        return 0;
    }

    for (uint32_t i = 0; i < image->file_count; ++i) {
        if (aarch64_streq(image->files[i].name, path)) {
            return &image->files[i];
        }
    }

    return 0;
}

void aarch64_initrd_print_status(void) {
    const aarch64_initrd_image_t *image = aarch64_initrd_get_image();
    aarch64_uart_write("[aarch64][initrd] files=");
    char count = (char)('0' + (image ? image->file_count : 0u));
    aarch64_uart_putc(count);
    aarch64_uart_write("\n");
}
