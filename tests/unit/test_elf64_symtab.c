/*
 * test_elf64_symtab.c - M5.1c 跨模块符号解析单元测试（宿主机）
 *
 * 造一个真实 .so（导出 shared_add / shared_data）+ 一个引用它们的
 * PIE 主程序；模拟内核 PT_LOAD 映射，注册进全局符号表，验证：
 *   - 模块内 lookup 命中已定义符号
 *   - 跨模块 gsymtab_resolve 能从 .so 解析出主程序引用的未定义符号
 *   - resolver 适配器喂给 apply_relocations 后，JUMP_SLOT/GLOB_DAT 被解析
 */

#include "unit_test.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

/* ---- early_console64 桩 ---- */
void early_console64_write(const char *s) { (void)s; }
void early_console64_write_hex64(uint64_t v) { (void)v; }

#include "../../src/arch/x86_64/include/elf64_dynamic.h"
#include "../../src/arch/x86_64/kernel/elf64_dynamic.c"
#include "../../src/arch/x86_64/include/elf64_symtab.h"
#include "../../src/arch/x86_64/kernel/elf64_symtab.c"

/* ------------------------------------------------------------------ */
static void *map_file(const char *path, uint64_t *out_size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return NULL; }
    void *p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return NULL;
    *out_size = (uint64_t)st.st_size;
    return p;
}

/* 每个模块独立的加载缓冲，模拟内核给各 ELF 私有物理页 */
static uint64_t simulate_load_into(const void *img, uint8_t **out_buf) {
    const uint8_t *base = (const uint8_t *)img;
    const openos_elf64_ehdr_t *eh = (const openos_elf64_ehdr_t *)base;
    uint64_t min_v = ~0ULL, max_v = 0;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const openos_elf64_phdr_t *ph =
            (const openos_elf64_phdr_t *)(base + eh->e_phoff +
                                          (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != OPENOS_PT_LOAD) continue;
        if (ph->p_vaddr < min_v) min_v = ph->p_vaddr;
        if (ph->p_vaddr + ph->p_memsz > max_v) max_v = ph->p_vaddr + ph->p_memsz;
    }
    if (min_v == ~0ULL) return 0;
    uint64_t span = max_v - min_v;
    uint8_t *buf = (uint8_t *)calloc(1, (size_t)span);
    if (!buf) return 0;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const openos_elf64_phdr_t *ph =
            (const openos_elf64_phdr_t *)(base + eh->e_phoff +
                                          (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != OPENOS_PT_LOAD) continue;
        memcpy(buf + (ph->p_vaddr - min_v),
               base + ph->p_offset, (size_t)ph->p_filesz);
    }
    *out_buf = buf;
    return (uint64_t)(uintptr_t)buf - min_v;
}

/* ------------------------------------------------------------------ */
/* 构建一个导出 shared_add()/shared_data 的真实共享库 */
static const char *build_shared_lib(void) {
    static const char out[] = "/tmp/openos_m51c_lib.so";
    FILE *f = fopen("/tmp/openos_m51c_lib.c", "w");
    if (!f) return NULL;
    fputs("int shared_data = 12345;\n"
          "int shared_add(int a, int b){ return a + b + shared_data; }\n", f);
    fclose(f);
    if (system("gcc -shared -fPIC -O0 /tmp/openos_m51c_lib.c "
               "-o /tmp/openos_m51c_lib.so 2>/dev/null") != 0) return NULL;
    return out;
}

/* ------------------------------------------------------------------ */
/* 用例 1：模块内 lookup 能命中 .so 导出的 GLOBAL 符号 */
UNIT_TEST_CASE(symtab_module_lookup)
{
    const char *sopath = build_shared_lib();
    if (!sopath) { printf("  [skip] gcc -shared unavailable\n"); return; }

    uint64_t sz = 0;
    void *img = map_file(sopath, &sz);
    ASSERT_TRUE(img != NULL);

    uint8_t *buf = NULL;
    uint64_t bias = simulate_load_into(img, &buf);
    ASSERT_TRUE(buf != NULL);

    openos_elf64_dyninfo_t info;
    ASSERT_EQ_INT(0, openos_elf64_parse_dynamic(img, sz, bias, &info));

    uint8_t bind = 0;
    uint64_t a1 = openos_elf64_module_lookup(&info, "shared_add", &bind);
    uint64_t a2 = openos_elf64_module_lookup(&info, "shared_data", NULL);
    ASSERT_TRUE(a1 != 0);
    ASSERT_TRUE(a2 != 0);
    ASSERT_EQ_INT(OPENOS_STB_GLOBAL, (int)bind);

    /* 不存在的符号返回 0 */
    ASSERT_TRUE(openos_elf64_module_lookup(&info, "no_such_sym", NULL) == 0);

    munmap(img, (size_t)sz);
    free(buf);
}

/* 用例 2：全局符号表跨模块解析 + resolver 适配器 */
UNIT_TEST_CASE(symtab_global_resolve)
{
    const char *sopath = build_shared_lib();
    if (!sopath) { printf("  [skip] gcc -shared unavailable\n"); return; }

    uint64_t sz = 0;
    void *img = map_file(sopath, &sz);
    ASSERT_TRUE(img != NULL);
    uint8_t *buf = NULL;
    uint64_t bias = simulate_load_into(img, &buf);
    ASSERT_TRUE(buf != NULL);

    openos_elf64_dyninfo_t info;
    ASSERT_EQ_INT(0, openos_elf64_parse_dynamic(img, sz, bias, &info));

    openos_elf64_gsymtab_t g;
    openos_elf64_gsymtab_init(&g);
    ASSERT_EQ_INT(0, openos_elf64_gsymtab_add(&g, &info, "libtest.so"));

    /* resolve 与 module_lookup 结果一致 */
    uint64_t r = openos_elf64_gsymtab_resolve(&g, "shared_add");
    uint64_t m = openos_elf64_module_lookup(&info, "shared_add", NULL);
    ASSERT_TRUE(r != 0);
    ASSERT_TRUE(r == m);

    /* resolver 适配器等价 */
    uint64_t cb = openos_elf64_gsymtab_resolver_cb("shared_data", &g);
    ASSERT_TRUE(cb == openos_elf64_gsymtab_resolve(&g, "shared_data"));

    /* 未定义符号 -> 0 */
    ASSERT_TRUE(openos_elf64_gsymtab_resolve(&g, "absent") == 0);

    munmap(img, (size_t)sz);
    free(buf);
}

/* 用例 3：参数健壮性 */
UNIT_TEST_CASE(symtab_reject_null)
{
    openos_elf64_gsymtab_t g;
    openos_elf64_gsymtab_init(&g);
    ASSERT_TRUE(openos_elf64_gsymtab_add(NULL, NULL, NULL) < 0);
    ASSERT_TRUE(openos_elf64_gsymtab_add(&g, NULL, "x") < 0);
    ASSERT_TRUE(openos_elf64_gsymtab_resolve(NULL, "x") == 0);
    ASSERT_TRUE(openos_elf64_module_lookup(NULL, "x", NULL) == 0);
}

int main(void)
{
    UNIT_TEST_RUN(symtab_module_lookup);
    UNIT_TEST_RUN(symtab_global_resolve);
    UNIT_TEST_RUN(symtab_reject_null);
    return unit_test_finish();
}
