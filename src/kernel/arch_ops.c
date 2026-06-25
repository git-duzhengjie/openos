#include "arch_ops.h"

static const OpenOSArchOps *g_openos_arch_ops;

void openos_arch_ops_register(const OpenOSArchOps *ops) {
    g_openos_arch_ops = ops;
}

const OpenOSArchOps *openos_arch_ops_get(void) {
    return g_openos_arch_ops;
}

const char *openos_arch_ops_name(void) {
    return g_openos_arch_ops && g_openos_arch_ops->name ? g_openos_arch_ops->name : "unknown";
}
