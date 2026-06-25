#include "aarch64_bootinfo.h"

static openos_bootinfo_t g_aarch64_bootinfo;

static void zero_bootinfo(openos_bootinfo_t *bootinfo)
{
    for (uint32_t i = 0; i < sizeof(*bootinfo); ++i) {
        ((uint8_t *)bootinfo)[i] = 0;
    }
}

const openos_bootinfo_t *arch_aarch64_bootinfo_from_stub(const aarch64_boot_stub_args_t *args)
{
    openos_bootinfo_t *bootinfo = &g_aarch64_bootinfo;
    zero_bootinfo(bootinfo);

    if (args) {
        if (args->device_tree_base && args->device_tree_size) {
            bootinfo->flags |= OPENOS_BOOTINFO_FLAG_DEVICE_TREE_VALID;
            bootinfo->device_tree.base = args->device_tree_base;
            bootinfo->device_tree.size = args->device_tree_size;
        }

        if (args->initrd_base && args->initrd_size) {
            bootinfo->flags |= OPENOS_BOOTINFO_FLAG_INITRD_VALID;
            bootinfo->initrd.base = args->initrd_base;
            bootinfo->initrd.size = args->initrd_size;
        }

        if (args->cmdline && args->cmdline_size) {
            bootinfo->flags |= OPENOS_BOOTINFO_FLAG_CMDLINE_VALID;
            bootinfo->cmdline = args->cmdline;
            bootinfo->cmdline_size = args->cmdline_size;
        }

        bootinfo->kernel_phys_start = args->kernel_phys_start ?
                                      args->kernel_phys_start :
                                      OPENOS_AARCH64_BOOTINFO_DEFAULT_KERNEL_BASE;
        bootinfo->kernel_phys_end = args->kernel_phys_end;
    } else {
        bootinfo->kernel_phys_start = OPENOS_AARCH64_BOOTINFO_DEFAULT_KERNEL_BASE;
    }

    openos_bootinfo_finalize(bootinfo);
    return bootinfo;
}
