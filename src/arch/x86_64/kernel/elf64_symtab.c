/*
 * elf64_symtab.c - M5.1c 跨模块符号解析（全局符号表）
 *
 * 提供多模块符号查找，配合 elf64_dynamic.c 的重定位引擎，实现
 * 主程序 + 共享库(.so) 之间的符号绑定。查找遵循 ELF 规则：
 *   - 遍历已注册模块，仅匹配“已定义”符号（st_shndx != SHN_UNDEF）
 *   - GLOBAL 绑定命中立即返回；WEAK 命中先记住，继续找 GLOBAL
 *   - 找不到 GLOBAL 时回退到首个 WEAK 定义
 *   - 模块注册顺序即优先级（主程序最先注册）
 */

#include "../include/elf64_symtab.h"

/* 与 libc 无关的极简 strcmp */
static int dl_streq(const char *a, const char *b) {
    if (a == b) {
        return 1;
    }
    if (a == 0 || b == 0) {
        return 0;
    }
    while (*a && (*a == *b)) {
        ++a;
        ++b;
    }
    return *(const unsigned char *)a == *(const unsigned char *)b;
}

void openos_elf64_gsymtab_init(openos_elf64_gsymtab_t *g) {
    if (g == 0) {
        return;
    }
    g->module_count = 0;
    for (int i = 0; i < OPENOS_DL_MAX_MODULES; ++i) {
        g->modules[i].info = 0;
        g->modules[i].name = 0;
    }
}

int openos_elf64_gsymtab_add(openos_elf64_gsymtab_t *g,
                             const openos_elf64_dyninfo_t *info,
                             const char *name) {
    if (g == 0 || info == 0) {
        return -1;
    }
    if (g->module_count >= OPENOS_DL_MAX_MODULES) {
        return -2;
    }
    g->modules[g->module_count].info = info;
    g->modules[g->module_count].name = name;
    ++g->module_count;
    return 0;
}

/*
 * 遍历一个模块的 symtab，寻找名字匹配且“已定义”的符号。
 * symtab 的项数无法从 .dynamic 直接得到（没有 DT_SYMTABSZ），
 * 但 strtab 紧随 symtab 之后是常见布局；这里改用一个上界扫描：
 * 以 syment 为步长遍历，遇到 st_name 越过 strsz 视为越界停止。
 * 更稳妥地：仅在 st_name 落在 [0, strsz) 范围内才比较。
 */
uint64_t openos_elf64_module_lookup(const openos_elf64_dyninfo_t *info,
                                    const char *name,
                                    uint8_t *found_bind) {
    if (info == 0 || name == 0 || info->symtab == 0 ||
        info->strtab == 0 || info->syment == 0) {
        return 0;
    }

    const uint8_t *base = (const uint8_t *)info->symtab;
    uint64_t ent = info->syment;
    uint64_t weak_hit = 0;
    uint8_t weak_bind = 0;

    /*
     * 无 DT_SYMTABSZ 时，用 GNU/SysV hash 表或 strtab 边界推断项数较复杂。
     * 这里采用保守上界：symtab 与 strtab 常相邻，符号项数 <=
     * (strtab - symtab) / syment。若二者顺序不可知则退回固定上界。
     */
    uint64_t max_syms;
    if ((const uint8_t *)info->strtab > base) {
        max_syms = ((uint64_t)((const uint8_t *)info->strtab - base)) / ent;
    } else {
        max_syms = 4096; /* 保守上界 */
    }

    for (uint64_t i = 1; i < max_syms; ++i) { /* i=0 是保留的 UNDEF 项 */
        const openos_elf64_sym_t *sym =
            (const openos_elf64_sym_t *)(const void *)(base + i * ent);

        /* 越界防护：st_name 必须落在 strtab 内 */
        if (info->strsz && sym->st_name >= info->strsz) {
            continue;
        }
        /* 只认已定义符号 */
        if (sym->st_shndx == 0) {
            continue;
        }
        const char *sname = info->strtab + sym->st_name;
        if (!dl_streq(sname, name)) {
            continue;
        }

        uint8_t bind = OPENOS_ELF64_ST_BIND(sym->st_info);
        uint64_t addr = sym->st_value + info->load_bias;
        if (bind == OPENOS_STB_GLOBAL) {
            if (found_bind) {
                *found_bind = bind;
            }
            return addr;
        }
        if (bind == OPENOS_STB_WEAK && weak_hit == 0) {
            weak_hit = addr;
            weak_bind = bind;
        }
    }

    if (weak_hit) {
        if (found_bind) {
            *found_bind = weak_bind;
        }
        return weak_hit;
    }
    return 0;
}

uint64_t openos_elf64_gsymtab_resolve(const openos_elf64_gsymtab_t *g,
                                      const char *name) {
    if (g == 0 || name == 0) {
        return 0;
    }
    uint64_t weak_hit = 0;

    for (int m = 0; m < g->module_count; ++m) {
        uint8_t bind = 0;
        uint64_t addr = openos_elf64_module_lookup(g->modules[m].info,
                                                   name, &bind);
        if (addr == 0) {
            continue;
        }
        if (bind == OPENOS_STB_GLOBAL) {
            return addr; /* GLOBAL 立即命中，模块顺序即优先级 */
        }
        if (weak_hit == 0) {
            weak_hit = addr; /* 首个 WEAK 记住，继续找 GLOBAL */
        }
    }
    return weak_hit;
}

uint64_t openos_elf64_gsymtab_resolver_cb(const char *name, void *user) {
    return openos_elf64_gsymtab_resolve((const openos_elf64_gsymtab_t *)user,
                                        name);
}
