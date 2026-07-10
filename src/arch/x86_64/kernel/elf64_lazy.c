/*
 * elf64_lazy.c - M5.1d 惰性绑定 PLT/GOT 实现（内核态方案）
 *
 * 参见 elf64_lazy.h 头部对 x86_64 惰性绑定流程的说明。
 * 本文件实现两个核心：
 *   openos_elf64_lazy_setup()   - 加载期把 JUMP_SLOT 的 GOT 项调整为“惰性初值”，
 *                                 并写好 GOT[1]=link_map、GOT[2]=resolver_entry。
 *   openos_elf64_lazy_resolve() - 首次调用时由 SYS_DL_RESOLVE 触发，解析符号、
 *                                 回填 GOT 项，返回目标地址。
 */

#include "../include/elf64_lazy.h"

/* ---- 串口调试（宿主机单测时被 stub 掉） ---- */
#ifndef OPENOS_ELF64_LAZY_HOSTTEST
extern void early_console64_write(const char *s);
static void lz_log(const char *s) { early_console64_write(s); }
#else
#include <stdio.h>
static void lz_log(const char *s) { (void)s; }
#endif

/*
 * 与 dynamic.c::reloc_resolve_symbol 保持一致的符号解析：
 *   本模块已定义（st_shndx != 0） -> st_value + load_bias
 *   未定义 -> 跨模块 resolver 回调
 *   弱符号未解析合法（返回 0，out_is_weak_undef=1）
 */
static uint64_t lazy_resolve_symbol(const openos_elf64_dyninfo_t *info,
                                    uint32_t sym_idx,
                                    openos_elf64_symresolve_fn resolver,
                                    void *resolver_user,
                                    int *out_is_weak_undef) {
    *out_is_weak_undef = 0;
    if (sym_idx == 0 || !info->symtab) {
        return 0;
    }
    const openos_elf64_sym_t *sym = &info->symtab[sym_idx];

    /* 本模块内已定义 */
    if (sym->st_shndx != 0) {
        return sym->st_value + info->load_bias;
    }

    /* 未定义：跨模块解析 */
    const char *name = 0;
    if (info->strtab && sym->st_name) {
        name = info->strtab + sym->st_name;
    }
    if (resolver && name) {
        uint64_t r = resolver(name, resolver_user);
        if (r != 0) {
            return r;
        }
    }

    /* 弱符号未解析合法 */
    if (OPENOS_ELF64_ST_BIND(sym->st_info) == OPENOS_STB_WEAK) {
        *out_is_weak_undef = 1;
        return 0;
    }
    return 0;
}

int openos_elf64_lazy_setup(const openos_elf64_dyninfo_t *info,
                            openos_elf64_link_map_t *lm,
                            uint64_t resolver_entry,
                            openos_elf64_lazy_stats_t *stats) {
    if (!info || !lm) {
        return -1;
    }
    /* 无 PLT 表则无需惰性初始化，视为成功（纯数据模块） */
    if (!info->jmprel || info->pltrel_sz == 0) {
        /* 仍尝试写 GOT[1/2]（若有 pltgot） */
        if (info->pltgot) {
            info->pltgot[1] = (uint64_t)(uintptr_t)lm;
            info->pltgot[2] = resolver_entry;
        }
        return 0;
    }
    if (!info->pltgot) {
        lz_log("[lazy] setup: JMPREL present but no PLTGOT\n");
        return -2;
    }

    /* GOT[1] = link_map, GOT[2] = _dl_runtime_resolve */
    info->pltgot[1] = (uint64_t)(uintptr_t)lm;
    info->pltgot[2] = resolver_entry;

    /*
     * 遍历 JMPREL 表，对每个 JUMP_SLOT：
     *   链接器已在 GOT 项写好“指回 PLT stub（push idx; jmp PLT[0]）”的
     *   模块内虚地址，惰性绑定只需 += load_bias（PIE 时非零）。
     */
    uint64_t count = info->pltrel_sz / sizeof(openos_elf64_rela_t);
    uint64_t primed = 0;
    for (uint64_t i = 0; i < count; i++) {
        const openos_elf64_rela_t *r = &info->jmprel[i];
        uint32_t type = OPENOS_ELF64_R_TYPE(r->r_info);
        if (type != OPENOS_R_X86_64_JUMP_SLOT) {
            continue;
        }
        uint64_t *where = (uint64_t *)(uintptr_t)(r->r_offset + info->load_bias);
        *where += info->load_bias;   /* 惰性初值：指回 PLT stub */
        primed++;
    }

    if (stats) {
        stats->got_slots_primed += primed;
    }
    return 0;
}

uint64_t openos_elf64_lazy_resolve(openos_elf64_link_map_t *lm,
                                   uint64_t reloc_index,
                                   openos_elf64_lazy_stats_t *stats) {
    if (!lm || !lm->info) {
        return 0;
    }
    const openos_elf64_dyninfo_t *info = lm->info;
    if (!info->jmprel || info->pltrel_sz == 0) {
        return 0;
    }
    uint64_t count = info->pltrel_sz / sizeof(openos_elf64_rela_t);
    if (reloc_index >= count) {
        lz_log("[lazy] resolve: reloc_index out of range\n");
        return 0;
    }

    const openos_elf64_rela_t *r = &info->jmprel[reloc_index];
    uint32_t type = OPENOS_ELF64_R_TYPE(r->r_info);
    if (type != OPENOS_R_X86_64_JUMP_SLOT) {
        lz_log("[lazy] resolve: not a JUMP_SLOT\n");
        return 0;
    }
    uint32_t sym_idx = OPENOS_ELF64_R_SYM(r->r_info);

    int is_weak_undef = 0;
    uint64_t target = lazy_resolve_symbol(info, sym_idx,
                                          lm->resolver, lm->resolver_user,
                                          &is_weak_undef);
    if (target == 0 && !is_weak_undef) {
        if (stats) stats->resolve_failures++;
        lz_log("[lazy] resolve: unresolved non-weak symbol\n");
        return 0;
    }

    /* JUMP_SLOT 的最终值 = S + A（A 通常 0） */
    target += (uint64_t)r->r_addend;

    /* 回填 GOT 项：*where = target，后续调用直达 */
    uint64_t *where = (uint64_t *)(uintptr_t)(r->r_offset + info->load_bias);
    *where = target;

    if (stats) stats->resolves_done++;
    return target;
}

/* ==================== link_map 注册表 ==================== */
static openos_elf64_link_map_t *g_lazy_modules[OPENOS_ELF64_LAZY_MAX_MODULES];
static openos_elf64_lazy_stats_t g_lazy_global_stats;

int openos_elf64_lazy_register(openos_elf64_link_map_t *lm) {
    if (!lm) return -1;
    /* 已在表中则幂等返回成功 */
    for (int i = 0; i < OPENOS_ELF64_LAZY_MAX_MODULES; i++) {
        if (g_lazy_modules[i] == lm) return 0;
    }
    for (int i = 0; i < OPENOS_ELF64_LAZY_MAX_MODULES; i++) {
        if (g_lazy_modules[i] == 0) {
            g_lazy_modules[i] = lm;
            return 0;
        }
    }
    lz_log("[lazy] register: module table full\n");
    return -2;
}

void openos_elf64_lazy_unregister(openos_elf64_link_map_t *lm) {
    if (!lm) return;
    for (int i = 0; i < OPENOS_ELF64_LAZY_MAX_MODULES; i++) {
        if (g_lazy_modules[i] == lm) {
            g_lazy_modules[i] = 0;
            return;
        }
    }
}

openos_elf64_link_map_t *openos_elf64_lazy_lookup(openos_elf64_link_map_t *lm) {
    if (!lm) return 0;
    for (int i = 0; i < OPENOS_ELF64_LAZY_MAX_MODULES; i++) {
        if (g_lazy_modules[i] == lm) return lm;
    }
    return 0;
}

uint64_t openos_elf64_dl_resolve_entry(uint64_t raw_lm, uint64_t reloc_index) {
    /* 安全校验：raw_lm 必须是已注册的 link_map，否则拒绝（防用户态伪造） */
    openos_elf64_link_map_t *lm =
        openos_elf64_lazy_lookup((openos_elf64_link_map_t *)(uintptr_t)raw_lm);
    if (!lm) {
        lz_log("[lazy] dl_resolve: unregistered link_map, reject\n");
        return 0;
    }
    return openos_elf64_lazy_resolve(lm, reloc_index, &g_lazy_global_stats);
}
