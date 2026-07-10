/*
 * test_elf64_lazy.c - M5.1d 惰性绑定单元测试（宿主机）
 *
 * 手工构造一个含 JUMP_SLOT 的模块（symtab/strtab/jmprel/pltgot），
 * 验证：
 *   1) lazy_setup 惰性初值：GOT 项 += load_bias，GOT[1/2] 写入正确
 *   2) lazy_resolve：本模块内定义符号解析 + 回填
 *   3) lazy_resolve：跨模块 resolver 回调解析 + 回填
 *   4) 弱符号未定义合法（返回 0，不算失败）
 *   5) 越界 reloc_index 拒绝
 */

#include "unit_test.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define OPENOS_ELF64_LAZY_HOSTTEST 1
#include "../../src/arch/x86_64/include/elf64_lazy.h"
#include "../../src/arch/x86_64/kernel/elf64_lazy.c"

/* ---- 构造模块用的静态存储 ---- */
static openos_elf64_sym_t  g_symtab[4];
static char                g_strtab[64];
static openos_elf64_rela_t g_jmprel[3];
static uint64_t            g_got[8];   /* GOT[0..2] 保留 + 3 项 PLT */

/* 跨模块解析回调：把 "ext_func" 解析到固定地址 */
static uint64_t g_ext_addr = 0;
static uint64_t test_resolver(const char *name, void *user) {
    (void)user;
    if (name && strcmp(name, "ext_func") == 0) {
        return g_ext_addr;
    }
    return 0;
}

/* 组装一个模块 dyninfo。
 * 符号布局：
 *   sym[0] = 保留(未定义)
 *   sym[1] = "local_func"  本模块定义 st_shndx=1 st_value=0x2000
 *   sym[2] = "ext_func"    未定义（跨模块）st_shndx=0
 *   sym[3] = "weak_func"   未定义弱符号  st_shndx=0 bind=WEAK
 * jmprel:
 *   [0] JUMP_SLOT sym=1 offset=GOT[3]
 *   [1] JUMP_SLOT sym=2 offset=GOT[4]
 *   [2] JUMP_SLOT sym=3 offset=GOT[5]
 */
static void build_module(openos_elf64_dyninfo_t *info, uint64_t bias) {
    memset(g_symtab, 0, sizeof(g_symtab));
    memset(g_strtab, 0, sizeof(g_strtab));
    memset(g_jmprel, 0, sizeof(g_jmprel));
    memset(g_got, 0, sizeof(g_got));

    /* strtab */
    size_t o1 = 1;  strcpy(&g_strtab[o1], "local_func");
    size_t o2 = o1 + 11; strcpy(&g_strtab[o2], "ext_func");
    size_t o3 = o2 + 9;  strcpy(&g_strtab[o3], "weak_func");

    g_symtab[1].st_name = (uint32_t)o1;
    g_symtab[1].st_shndx = 1;
    g_symtab[1].st_value = 0x2000;
    g_symtab[1].st_info = (OPENOS_STB_GLOBAL << 4);

    g_symtab[2].st_name = (uint32_t)o2;
    g_symtab[2].st_shndx = 0;
    g_symtab[2].st_info = (OPENOS_STB_GLOBAL << 4);

    g_symtab[3].st_name = (uint32_t)o3;
    g_symtab[3].st_shndx = 0;
    g_symtab[3].st_info = (OPENOS_STB_WEAK << 4);

    /* GOT 基地址（模块内虚地址空间起点用 0 表示，实际 = offset+bias） */
    /* GOT 项的“惰性初值”模拟链接器写好的“指回 PLT stub”的模块内虚地址 */
    g_got[3] = 0x100;  /* local_func PLT stub 模块内虚地址 */
    g_got[4] = 0x110;
    g_got[5] = 0x120;

    /* jmprel：r_offset 是模块内虚地址（GOT 项地址）。
     * 这里 GOT 数组本身就是内核可写缓冲，令 r_offset+bias == &g_got[n]。
     * 为简化，令 bias=0 场景下 r_offset = (uintptr_t)&g_got[n]。 */
    g_jmprel[0].r_offset = (uint64_t)(uintptr_t)&g_got[3] - bias;
    g_jmprel[0].r_info = ((uint64_t)1 << 32) | OPENOS_R_X86_64_JUMP_SLOT;
    g_jmprel[1].r_offset = (uint64_t)(uintptr_t)&g_got[4] - bias;
    g_jmprel[1].r_info = ((uint64_t)2 << 32) | OPENOS_R_X86_64_JUMP_SLOT;
    g_jmprel[2].r_offset = (uint64_t)(uintptr_t)&g_got[5] - bias;
    g_jmprel[2].r_info = ((uint64_t)3 << 32) | OPENOS_R_X86_64_JUMP_SLOT;

    memset(info, 0, sizeof(*info));
    info->load_bias = bias;
    info->symtab = g_symtab;
    info->strtab = g_strtab;
    info->jmprel = g_jmprel;
    info->pltrel_sz = sizeof(g_jmprel);
    info->pltgot = g_got;
}

/* 用例 1：lazy_setup 惰性初值 + GOT[1/2] 写入 */
UNIT_TEST_CASE(lazy_setup_primes_got)
{
    openos_elf64_dyninfo_t info;
    uint64_t bias = 0x40000;
    build_module(&info, bias);

    openos_elf64_link_map_t lm;
    memset(&lm, 0, sizeof(lm));
    lm.info = &info;

    openos_elf64_lazy_stats_t st;
    memset(&st, 0, sizeof(st));
    uint64_t resolver_entry = 0xDEAD1000;

    int rc = openos_elf64_lazy_setup(&info, &lm, resolver_entry, &st);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(3, (int)st.got_slots_primed);

    /* GOT[1]=link_map, GOT[2]=resolver_entry */
    ASSERT_TRUE(g_got[1] == (uint64_t)(uintptr_t)&lm);
    ASSERT_TRUE(g_got[2] == resolver_entry);

    /* 惰性初值 = 原值(指回 PLT stub 模块内虚地址) + bias */
    ASSERT_TRUE(g_got[3] == 0x100 + bias);
    ASSERT_TRUE(g_got[4] == 0x110 + bias);
    ASSERT_TRUE(g_got[5] == 0x120 + bias);
}

/* 用例 2：lazy_resolve 本模块内定义符号，回填 GOT */
UNIT_TEST_CASE(lazy_resolve_local)
{
    openos_elf64_dyninfo_t info;
    uint64_t bias = 0x40000;
    build_module(&info, bias);

    openos_elf64_link_map_t lm;
    memset(&lm, 0, sizeof(lm));
    lm.info = &info;

    openos_elf64_lazy_stats_t st;
    memset(&st, 0, sizeof(st));
    openos_elf64_lazy_setup(&info, &lm, 0xDEAD1000, &st);

    /* reloc_index=0 -> sym[1] local_func, S = 0x2000 + bias */
    uint64_t target = openos_elf64_lazy_resolve(&lm, 0, &st);
    ASSERT_TRUE(target == 0x2000 + bias);
    ASSERT_TRUE(g_got[3] == 0x2000 + bias);   /* 回填成功 */
    ASSERT_EQ_INT(1, (int)st.resolves_done);
    ASSERT_EQ_INT(0, (int)st.resolve_failures);
}

/* 用例 3：lazy_resolve 跨模块 resolver 回调 */
UNIT_TEST_CASE(lazy_resolve_cross_module)
{
    openos_elf64_dyninfo_t info;
    uint64_t bias = 0x40000;
    build_module(&info, bias);

    openos_elf64_link_map_t lm;
    memset(&lm, 0, sizeof(lm));
    lm.info = &info;
    lm.resolver = test_resolver;
    lm.resolver_user = NULL;
    g_ext_addr = 0xCAFE9000;

    openos_elf64_lazy_stats_t st;
    memset(&st, 0, sizeof(st));
    openos_elf64_lazy_setup(&info, &lm, 0xDEAD1000, &st);

    /* reloc_index=1 -> sym[2] ext_func -> resolver -> g_ext_addr */
    uint64_t target = openos_elf64_lazy_resolve(&lm, 1, &st);
    ASSERT_TRUE(target == 0xCAFE9000);
    ASSERT_TRUE(g_got[4] == 0xCAFE9000);
    ASSERT_EQ_INT(1, (int)st.resolves_done);
}

/* 用例 4：弱符号未定义合法，返回 0 且不计失败 */
UNIT_TEST_CASE(lazy_resolve_weak_undef)
{
    openos_elf64_dyninfo_t info;
    uint64_t bias = 0;
    build_module(&info, bias);

    openos_elf64_link_map_t lm;
    memset(&lm, 0, sizeof(lm));
    lm.info = &info;
    /* 无 resolver，weak_func 无法解析 */

    openos_elf64_lazy_stats_t st;
    memset(&st, 0, sizeof(st));
    openos_elf64_lazy_setup(&info, &lm, 0xDEAD1000, &st);

    /* reloc_index=2 -> sym[3] weak_func 未定义弱符号 */
    uint64_t target = openos_elf64_lazy_resolve(&lm, 2, &st);
    ASSERT_TRUE(target == 0);
    ASSERT_EQ_INT(0, (int)st.resolve_failures);
}

/* 用例 5：越界 reloc_index / NULL 拒绝 */
UNIT_TEST_CASE(lazy_resolve_reject)
{
    openos_elf64_dyninfo_t info;
    build_module(&info, 0);
    openos_elf64_link_map_t lm;
    memset(&lm, 0, sizeof(lm));
    lm.info = &info;
    openos_elf64_lazy_stats_t st;
    memset(&st, 0, sizeof(st));
    openos_elf64_lazy_setup(&info, &lm, 0xDEAD1000, &st);

    ASSERT_TRUE(openos_elf64_lazy_resolve(&lm, 99, &st) == 0);
    ASSERT_TRUE(openos_elf64_lazy_resolve(NULL, 0, &st) == 0);
}

/* 用例 6：link_map 注册表白名单校验 */
UNIT_TEST_CASE(lazy_registry_whitelist)
{
    openos_elf64_dyninfo_t info;
    build_module(&info, 0);
    openos_elf64_link_map_t lm;
    memset(&lm, 0, sizeof(lm));
    lm.info = &info;
    lm.resolver = test_resolver;
    g_ext_addr = 0xCAFE9000;
    openos_elf64_lazy_setup(&info, &lm, 0xDEAD1000, NULL);

    /* 未注册：dl_resolve_entry 拒绝 */
    ASSERT_TRUE(openos_elf64_dl_resolve_entry((uint64_t)(uintptr_t)&lm, 1) == 0);

    /* 注册后：能解析 */
    ASSERT_EQ_INT(0, openos_elf64_lazy_register(&lm));
    ASSERT_TRUE(openos_elf64_lazy_lookup(&lm) == &lm);
    ASSERT_TRUE(openos_elf64_dl_resolve_entry((uint64_t)(uintptr_t)&lm, 1) == 0xCAFE9000);

    /* 伪造指针：拒绝 */
    uint64_t fake = (uint64_t)(uintptr_t)&lm + 0x1000;
    ASSERT_TRUE(openos_elf64_dl_resolve_entry(fake, 1) == 0);

    /* 注销后：再次拒绝 */
    openos_elf64_lazy_unregister(&lm);
    ASSERT_TRUE(openos_elf64_lazy_lookup(&lm) == NULL);
    ASSERT_TRUE(openos_elf64_dl_resolve_entry((uint64_t)(uintptr_t)&lm, 1) == 0);
}

int main(void)
{
    UNIT_TEST_RUN(lazy_setup_primes_got);
    UNIT_TEST_RUN(lazy_resolve_local);
    UNIT_TEST_RUN(lazy_resolve_cross_module);
    UNIT_TEST_RUN(lazy_resolve_weak_undef);
    UNIT_TEST_RUN(lazy_resolve_reject);
    UNIT_TEST_RUN(lazy_registry_whitelist);
    return unit_test_finish();
}
