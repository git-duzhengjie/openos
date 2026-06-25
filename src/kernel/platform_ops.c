#include "platform_ops.h"

static const OpenOSPlatformOps *g_openos_platform_ops;

void openos_platform_ops_register(const OpenOSPlatformOps *ops) {
    g_openos_platform_ops = ops;
}

const OpenOSPlatformOps *openos_platform_ops_get(void) {
    return g_openos_platform_ops;
}

const char *openos_platform_ops_name(void) {
    return g_openos_platform_ops && g_openos_platform_ops->name ? g_openos_platform_ops->name : "unknown";
}
