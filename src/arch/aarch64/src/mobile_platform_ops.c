#include "platform_ops.h"
#include "mobile_platform_ops.h"

static void mobile_noop(void) {
}

static void mobile_early_console_write(const char *text) {
    (void)text;
}

static int mobile_power_unsupported(void) {
    return -1;
}

static const OpenOSPlatformOps k_mobile_platform_ops = {
    .name = "mobile",
    .early_console_init = mobile_noop,
    .early_console_write = mobile_early_console_write,
    .timer_init = mobile_noop,
    .irq_init = mobile_noop,
    .poweroff = mobile_power_unsupported,
    .reboot = mobile_power_unsupported,
};

void openos_mobile_platform_ops_init(void) {
    openos_platform_ops_register(&k_mobile_platform_ops);
}
