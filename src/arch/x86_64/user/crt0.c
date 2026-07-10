#include "openos64.h"

static const openos64_runtime_info_t openos64_runtime_info = {
    1ULL,
    1ULL,
    (uint64_t)(uintptr_t)&openos64_start
};

openos64_size_t openos64_strlen(const char *text) {
    openos64_size_t len = 0;
    if (!text) {
        return 0;
    }
    while (text[len] != '\0') {
        ++len;
    }
    return len;
}

const openos64_runtime_info_t *openos64_runtime_get_info(void) {
    return &openos64_runtime_info;
}

/*
 * M5.1d: 惰性绑定 trampoline 的 C 后端。
 * 由 dl_trampoline64.S::_dl_runtime_resolve 调用。
 * 发起 SYS_DL_RESOLVE(link_map, reloc_index)，内核回填 GOT 并返回目标地址。
 * 必须是非 inline 实体符号，供汇编 call 链接。
 */
long openos64_dl_resolve(unsigned long link_map, unsigned long reloc_index) {
    return openos64_syscall2(OPENOS64_SYS_DL_RESOLVE,
                             (uint64_t)link_map,
                             (uint64_t)reloc_index);
}

/*
 * H.5a: receive argc/argv/envp from the ABI-defined positions on the user
 * stack (set up by crt0.S which loads them from %rsp into rdi/rsi/rdx)
 * and forward them to the user main. Programs that don't care can
 * still cast (argc, argv, envp) to (void) just like Linux user-space.
 */
void openos64_start(int argc, char **argv, char **envp) {
    int rc = openos64_main(argc, argv, envp);
    openos64_exit(rc);
}
