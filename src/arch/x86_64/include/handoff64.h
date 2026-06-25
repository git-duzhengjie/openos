#ifndef OPENOS_ARCH_X86_64_HANDOFF64_H
#define OPENOS_ARCH_X86_64_HANDOFF64_H

#include "uefi64.h"
#include "bootinfo.h"

const openos_bootinfo_t *arch_x86_64_bootinfo_from_uefi_handoff(const uefi64_handoff_info_t *handoff);
void arch_x86_64_memory_init_from_bootinfo(const openos_bootinfo_t *bootinfo);
void arch_x86_64_handoff_print_status(void);

#endif /* OPENOS_ARCH_X86_64_HANDOFF64_H */
