#include "platform_ops.h"
#include "pc_platform_ops.h"
#include "serial.h"
#include "power.h"

static void pc_early_console_init(void) {
    serial_init();
}

static void pc_early_console_write(const char *text) {
    serial_write(text);
}

static void pc_timer_init(void) {
}

static void pc_irq_init(void) {
}

static const OpenOSPlatformOps k_pc_platform_ops = {
    .name = "pc",
    .early_console_init = pc_early_console_init,
    .early_console_write = pc_early_console_write,
    .timer_init = pc_timer_init,
    .irq_init = pc_irq_init,
    .poweroff = power_shutdown,
    .reboot = power_reboot,
};

void openos_pc_platform_ops_init(void) {
    openos_platform_ops_register(&k_pc_platform_ops);
}
