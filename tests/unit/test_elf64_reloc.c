/*
 * test_elf64_reloc.c - M5.1b 重定位单元测试（宿主机）
 *
 * 造真实 PIE，模拟内核 PT_LOAD 段映射到可写缓冲，调用
 * openos_elf64_apply_relocations，核对 R_X86_64_RELATIVE 写入正确。
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

static uint8_t *g_loadbuf = NULL;
static uint64_t g_span = 0;

/* 模拟内核段映射：PT_LOAD 拷到可写缓冲，返回 load_bias */
static uint64_t simulate_load(const void *img, uint64_t sz) {
    (void)sz;
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
    g_span = max_v - min_v;
    g_loadbuf = (uint8_t *)calloc(1, (size_t)g_span);
    if (!g_loadbuf) return 0;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const openos_elf64_phdr_t *ph =
            (const openos_elf64_phdr_t *)(base + eh->e_phoff +
                                          (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != OPENOS_PT_LOAD) continue;
        memcpy(g_loadbuf + (ph->p_vaddr - min_v),
               base + ph->p_offset, (size_t)ph->p_filesz);
    }
    return (uint64_t)(uintptr_t)g_loadbuf - min_v;
}

static const char *build_pie_sample(void) {
    static const char out[] = "/tmp/openos_m51b_sample.elf";
    FILE *f = fopen("/tmp/openos_m51b_sample.c", "w");
    if (!f) return NULL;
    fputs("#include <stdio.h>\n"
          "int g_val = 42;\n"
          "int *g_ptr = &g_val;\n"
          "const char *g_msg = \"hi\";\n"
          "int main(void){ printf(\"%d %s\\n\", *g_ptr, g_msg); return 0; }\n", f);
    fclose(f);
    int rc = system("gcc -pie -fPIE -O0 /tmp/openos_m51b_sample.c "
                    "-o /tmp/openos_m51b_sample.elf 2>/dev/null");
    if (rc != 0) return NULL;
    return out;
}

/* ------------------------------------------------------------------ */

/* 用例 1：PIE 重定位后 RELATIVE 项被正确写入 */
UNIT_TEST_CASE(reloc_pie_relative)
{
    const char *path = build_pie_sample();
    if (!path) {
        printf("  [skip] gcc -pie unavailable\n");
        return;
    }
    uint64_t sz = 0;
    void *img = map_file(path, &sz);
    ASSERT_TRUE(img != NULL);

    uint64_t bias = simulate_load(img, sz);
    ASSERT_TRUE(g_loadbuf != NULL);

    openos_elf64_dyninfo_t info;
    ASSERT_EQ_INT(0, openos_elf64_parse_dynamic(img, sz, bias, &info));
    ASSERT_EQ_INT(1, info.is_pie);

    openos_elf64_reloc_stats_t st;
    int unresolved = openos_elf64_apply_relocations(&info, NULL, NULL, &st);

    ASSERT_TRUE(st.relative_count >= 1);
    ASSERT_TRUE(st.total >= st.relative_count);
    ASSERT_EQ_INT(unresolved, (int)st.unresolved_count);

    munmap(img, (size_t)sz);
    free(g_loadbuf);
    g_loadbuf = NULL;
}

/* 用例 2：手工构造 RELATIVE 表，逐字节核对写入值 = bias + addend */
UNIT_TEST_CASE(reloc_relative_value_exact)
{
    uint64_t slots[3] = { 0xdead, 0xbeef, 0xcafe };
    uint64_t bias = 0x400000ULL;

    openos_elf64_rela_t rela[3];
    memset(rela, 0, sizeof(rela));
    for (int i = 0; i < 3; i++) {
        rela[i].r_offset = (uint64_t)(uintptr_t)&slots[i] - bias;
        rela[i].r_info   = OPENOS_R_X86_64_RELATIVE;
        rela[i].r_addend = (int64_t)(0x1000 * (i + 1));
    }

    openos_elf64_dyninfo_t info;
    memset(&info, 0, sizeof(info));
    info.load_bias = bias;
    info.rela      = rela;
    info.rela_sz   = sizeof(rela);

    openos_elf64_reloc_stats_t st;
    int unresolved = openos_elf64_apply_relocations(&info, NULL, NULL, &st);

    ASSERT_EQ_INT(0, unresolved);
    ASSERT_EQ_INT(3, (int)st.relative_count);
    ASSERT_EQ_INT(3, (int)st.total);
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(slots[i] == bias + (uint64_t)(0x1000 * (i + 1)));
    }
}

/* 用例 3：NULL 参数拒绝 */
UNIT_TEST_CASE(reloc_reject_null)
{
    ASSERT_TRUE(openos_elf64_apply_relocations(NULL, NULL, NULL, NULL) < 0);
}

int main(void)
{
    UNIT_TEST_RUN(reloc_pie_relative);
    UNIT_TEST_RUN(reloc_relative_value_exact);
    UNIT_TEST_RUN(reloc_reject_null);
    return unit_test_finish();
}
