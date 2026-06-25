#include "platform_ops.h"
#include "qemu_aarch64_virt_platform_ops.h"

static void qemu_aarch64_virt_noop(void) {
}

static void qemu_aarch64_virt_early_console_write(const char *text) {
    (void)text;
}

static int qemu_aarch64_virt_power_unsupported(void) {
    return -1;
}

static const OpenOSPlatformOps k_qemu_aarch64_virt_platform_ops = {
    .name = "qemu-aarch64-virt",
    .early_console_init = qemu_aarch64_virt_noop,
    .early_console_write = qemu_aarch64_virt_early_console_write,
    .timer_init = qemu_aarch64_virt_noop,
    .irq_init = qemu_aarch64_virt_noop,
    .poweroff = qemu_aarch64_virt_power_unsupported,
    .reboot = qemu_aarch64_virt_power_unsupported,
};

void openos_qemu_aarch64_virt_platform_ops_init(void) {
    openos_platform_ops_register(&k_qemu_aarch64_virt_platform_ops);
}
