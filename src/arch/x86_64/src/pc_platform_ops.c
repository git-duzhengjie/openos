#include "platform_ops.h"
#include "pc_platform_ops.h"
#include "early_console64.h"
#include "idt64.h"

static void pc_early_console_init(void) {
    early_console64_init();
}

static void pc_early_console_write(const char *text) {
    early_console64_write(text);
}

static void pc_timer_init(void) {
}

static void pc_irq_init(void) {
    arch_x86_64_idt_init();
}

static int pc_power_unsupported(void) {
    return -1;
}

static const OpenOSPlatformOps k_pc_platform_ops = {
    .name = "pc",
    .early_console_init = pc_early_console_init,
    .early_console_write = pc_early_console_write,
    .timer_init = pc_timer_init,
    .irq_init = pc_irq_init,
    .poweroff = pc_power_unsupported,
    .reboot = pc_power_unsupported,
};

void openos_pc_platform_ops_init(void) {
    openos_platform_ops_register(&k_pc_platform_ops);
}
