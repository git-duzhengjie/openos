#ifndef OPENOS_KERNEL_PLATFORM_OPS_H
#define OPENOS_KERNEL_PLATFORM_OPS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*OpenOSPlatformVoidOp)(void);
typedef void (*OpenOSPlatformConsoleWriteOp)(const char *text);
typedef int (*OpenOSPlatformPowerOp)(void);

typedef struct OpenOSPlatformOps {
    const char *name;
    OpenOSPlatformVoidOp early_console_init;
    OpenOSPlatformConsoleWriteOp early_console_write;
    OpenOSPlatformVoidOp timer_init;
    OpenOSPlatformVoidOp irq_init;
    OpenOSPlatformPowerOp poweroff;
    OpenOSPlatformPowerOp reboot;
} OpenOSPlatformOps;

void openos_platform_ops_register(const OpenOSPlatformOps *ops);
const OpenOSPlatformOps *openos_platform_ops_get(void);
const char *openos_platform_ops_name(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_KERNEL_PLATFORM_OPS_H */
