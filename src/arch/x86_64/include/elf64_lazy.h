#ifndef OPENOS_ARCH_X86_64_ELF64_LAZY_H
#define OPENOS_ARCH_X86_64_ELF64_LAZY_H

/*
 * elf64_lazy.h - M5.1d 惰性绑定 PLT/GOT（真·lazy binding）
 *
 * x86_64 psABI 惰性绑定机制：
 *   首次调用 foo():
 *     call foo@plt
 *       -> jmp *GOT[n]            ; GOT[n] 初值指回下一条 PLT stub 指令（未解析）
 *       -> push reloc_index       ; PLT stub 压入该符号在 JMPREL 表中的下标
 *       -> jmp PLT[0]
 *       -> push GOT[1]            ; PLT[0] 压入 link_map 指针
 *       -> jmp *GOT[2]            ; 跳到 _dl_runtime_resolve（用户态 trampoline）
 *       -> [trampoline] syscall SYS_DL_RESOLVE(link_map, reloc_index)
 *       -> [内核] openos_elf64_lazy_resolve() 解析符号 + 回填 GOT[n]，返回目标地址
 *       -> [trampoline] jmp 目标地址
 *   之后调用 foo(): jmp *GOT[n] 直达，不再进解析器。
 *
 * GOT 布局约定（psABI）：
 *   GOT[0] = &.dynamic          （由链接器/我们填）
 *   GOT[1] = link_map*          （模块标识，传给 resolver）
 *   GOT[2] = _dl_runtime_resolve（用户态 trampoline 入口）
 *   GOT[3..] = 各 PLT 项，初值 = PLT stub 地址 + load_bias（惰性）
 *
 * 设计原则：内核态方案。resolver 运行在内核，通过 SYS_DL_RESOLVE 陷入。
 * 依赖 elf64_dynamic.h 的 dyninfo/符号解析基础设施。
 */

#include <stdint.h>
#include "elf64_dynamic.h"

/* ==================== link_map：已加载模块运行时句柄 ==================== */
/*
 * 惰性解析时，用户态只持有一个 link_map* 指针（存于 GOT[1]），
 * 内核据此定位到模块的 dyninfo，进而解析第 reloc_index 项 JUMP_SLOT。
 * 为便于内核态查表，link_map 直接内嵌 dyninfo 指针 + 跨模块解析上下文。
 */
typedef struct openos_elf64_link_map {
    const openos_elf64_dyninfo_t *info;      /* 本模块动态信息 */
    openos_elf64_symresolve_fn    resolver;  /* 跨模块符号解析回调，可 NULL */
    void                         *resolver_user;
    uint64_t                      id;         /* 模块 id（调试用） */
} openos_elf64_link_map_t;

/* ==================== 惰性绑定统计（调试/自测） ==================== */
typedef struct {
    uint64_t got_slots_primed;   /* setup 阶段完成惰性初始化的 GOT 项数 */
    uint64_t resolves_done;      /* lazy_resolve 成功解析并回填的次数 */
    uint64_t resolve_failures;   /* 解析失败（未定义非弱符号）次数 */
} openos_elf64_lazy_stats_t;

/* ============================ 公共 API ============================ */

/*
 * M5.1d 步骤 1：惰性初始化 GOT。
 * 对 DT_JMPREL 表中每个 R_X86_64_JUMP_SLOT：
 *   *where += load_bias    （保留链接器写好的“指回 PLT stub”相对值，加基址）
 * 并填写 GOT[1]=link_map、GOT[2]=resolver_entry。
 * 注意：不解析符号，符号在首次调用时由 lazy_resolve 完成。
 *
 *   info          - 已 parse_dynamic 的模块信息（需含 pltgot/jmprel）
 *   lm            - 关联的 link_map（内核持有，其地址写入 GOT[1]）
 *   resolver_entry- _dl_runtime_resolve 的运行时地址（写入 GOT[2]）
 *   stats         - 输出统计，可 NULL
 * 返回 0 成功；<0 参数错误（缺 pltgot / jmprel）。
 */
int openos_elf64_lazy_setup(const openos_elf64_dyninfo_t *info,
                            openos_elf64_link_map_t *lm,
                            uint64_t resolver_entry,
                            openos_elf64_lazy_stats_t *stats);

/*
 * M5.1d 步骤 2：运行时惰性解析（由 SYS_DL_RESOLVE 调用）。
 * 解析 lm->info->jmprel[reloc_index] 指向的符号，回填对应 GOT 项，
 * 返回解析出的目标函数运行时地址（供 trampoline 跳转）。
 *
 *   lm          - GOT[1] 传入的 link_map
 *   reloc_index - PLT stub 压栈的重定位下标（JMPREL 表内 rela 项序号）
 *   stats       - 输出统计，可 NULL
 * 返回目标地址；0 表示解析失败（未定义非弱符号）。
 */
uint64_t openos_elf64_lazy_resolve(openos_elf64_link_map_t *lm,
                                   uint64_t reloc_index,
                                   openos_elf64_lazy_stats_t *stats);

/* ==================== link_map 注册表（内核安全校验） ==================== */
/*
 * GOT[1] 存放的是内核 link_map 指针，SYS_DL_RESOLVE 时由用户态原样转发回内核。
 * 为防止用户态伪造任意内核指针，内核维护一张已注册 link_map 白名单，
 * do_dl_resolve 仅接受注册过的 link_map。
 */
#define OPENOS_ELF64_LAZY_MAX_MODULES 16

/* 注册一个 link_map（lazy_setup 前调用）。返回 0 成功，<0 表满。 */
int openos_elf64_lazy_register(openos_elf64_link_map_t *lm);

/* 注销（模块卸载时）。 */
void openos_elf64_lazy_unregister(openos_elf64_link_map_t *lm);

/* 校验指针是否为已注册 link_map，是则返回该指针，否则 NULL。 */
openos_elf64_link_map_t *openos_elf64_lazy_lookup(openos_elf64_link_map_t *lm);

/*
 * SYS_DL_RESOLVE 内核入口：校验 lm 合法后调用 lazy_resolve。
 *   raw_lm      - 用户态转发来的 GOT[1] 值（待校验）
 *   reloc_index - PLT stub 压栈的重定位下标
 * 返回目标函数运行时地址；0 表示失败（非法 lm / 解析失败）。
 */
uint64_t openos_elf64_dl_resolve_entry(uint64_t raw_lm, uint64_t reloc_index);

#endif /* OPENOS_ARCH_X86_64_ELF64_LAZY_H */
