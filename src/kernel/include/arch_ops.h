#ifndef OPENOS_KERNEL_ARCH_OPS_H
#define OPENOS_KERNEL_ARCH_OPS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*OpenOSArchVoidOp)(void);
typedef void (*OpenOSArchContextSwitchOp)(void *from_context, void *to_context);
typedef uint64_t (*OpenOSArchCycleCounterOp)(void);

typedef struct OpenOSArchOps {
    const char *name;
    OpenOSArchVoidOp early_init;
    OpenOSArchVoidOp interrupt_init;
    OpenOSArchVoidOp enable_interrupts;
    OpenOSArchVoidOp disable_interrupts;
    OpenOSArchVoidOp halt;
    OpenOSArchContextSwitchOp context_switch;
    OpenOSArchCycleCounterOp read_cycle_counter;
} OpenOSArchOps;

void openos_arch_ops_register(const OpenOSArchOps *ops);
const OpenOSArchOps *openos_arch_ops_get(void);
const char *openos_arch_ops_name(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_KERNEL_ARCH_OPS_H */
