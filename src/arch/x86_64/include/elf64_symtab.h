#ifndef OPENOS_ARCH_X86_64_ELF64_SYMTAB_H
#define OPENOS_ARCH_X86_64_ELF64_SYMTAB_H

/*
 * elf64_symtab.h - M5.1c 跨模块符号解析（全局符号表）
 *
 * 在 M5.1a(解析) + M5.1b(重定位) 之上，提供多模块的全局符号查找：
 *   - 把主程序 + 各已加载 .so 的 dyninfo 注册进一张全局符号表
 *   - 按 ELF 查找规则解析符号：全局(GLOBAL) 优先于弱(WEAK)，
 *     先注册者优先（load order，主程序最先注册）
 *   - 提供一个 openos_elf64_symresolve_fn 适配器，直接喂给
 *     openos_elf64_apply_relocations() 完成跨模块重定位
 *
 * 设计：仅依赖 elf64_dynamic.h 的类型，零动态分配（固定容量数组）。
 */

#include <stdint.h>
#include "elf64_dynamic.h"

#ifndef OPENOS_DL_MAX_MODULES
#define OPENOS_DL_MAX_MODULES 16
#endif

/* 一个已注册模块：dyninfo 视图 + 可读的模块名（用于调试） */
typedef struct {
    const openos_elf64_dyninfo_t *info;
    const char *name;   /* 模块名（DT_SONAME 或调用方给定），可为 NULL */
} openos_elf64_module_t;

/* 全局符号表：记录一组已加载模块 */
typedef struct {
    openos_elf64_module_t modules[OPENOS_DL_MAX_MODULES];
    int module_count;
} openos_elf64_gsymtab_t;

/* 初始化（清空模块列表） */
void openos_elf64_gsymtab_init(openos_elf64_gsymtab_t *g);

/*
 * 注册一个模块（主程序或 .so）。注册顺序即查找优先级：
 * 主程序应最先注册。返回 0 成功，<0 失败（表满/参数错）。
 */
int openos_elf64_gsymtab_add(openos_elf64_gsymtab_t *g,
                             const openos_elf64_dyninfo_t *info,
                             const char *name);

/*
 * 在单个模块内查找“已定义”的符号（st_shndx != SHN_UNDEF）。
 *   found_bind - 若非 NULL，输出命中符号的绑定（GLOBAL/WEAK）
 * 返回运行时绝对地址（st_value + load_bias）；未命中返回 0。
 */
uint64_t openos_elf64_module_lookup(const openos_elf64_dyninfo_t *info,
                                    const char *name,
                                    uint8_t *found_bind);

/*
 * 全局解析：遍历所有已注册模块，按 GLOBAL 优先于 WEAK、先注册者优先
 * 的规则返回符号运行时地址；找不到返回 0。
 */
uint64_t openos_elf64_gsymtab_resolve(const openos_elf64_gsymtab_t *g,
                                      const char *name);

/*
 * openos_elf64_symresolve_fn 适配器：user 传 openos_elf64_gsymtab_t*，
 * 可直接作为 openos_elf64_apply_relocations() 的 resolver 参数。
 */
uint64_t openos_elf64_gsymtab_resolver_cb(const char *name, void *user);

#endif /* OPENOS_ARCH_X86_64_ELF64_SYMTAB_H */
