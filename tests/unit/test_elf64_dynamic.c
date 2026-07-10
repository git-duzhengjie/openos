/*
 * test_elf64_dynamic.c - M5.1a 动态段解析单元测试（宿主机）
 *
 * 策略：现造一个真实的动态链接 ELF（PIE 可执行 + 依赖 libc.so），
 * mmap 进内存后调用 openos_elf64_parse_dynamic，核对解析出的
 * is_pie / interp / DT_NEEDED / strtab / symtab / rela 等字段。
 *
 * elf64_dynamic.c 的解析逻辑与内核 early_console64 打印解耦，
 * 本测试提供 early_console64_write* 桩，并直接编译 elf64_dynamic.c。
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

/* ---- early_console64 桩（被 elf64_dynamic.c 引用）---- */
void early_console64_write(const char *s) { (void)s; }
void early_console64_write_hex64(uint64_t v) { (void)v; }

/* 直接纳入被测实现（含其头文件）。相对本测试文件所在 tests/unit 目录。 */
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

/*
 * 忠实模拟内核 elf64_loader 的段映射：把每个 PT_LOAD 段拷到
 * (p_vaddr - min_vaddr) 偏移，返回 load_bias（使 vaddr+bias 指向正确内容）。
 * 这样解析函数内 .dynamic 的 vaddr+bias 寻址与内核一致。
 */
static uint8_t *g_loadbuf = NULL;
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
    uint64_t span = max_v - min_v;
    g_loadbuf = (uint8_t *)calloc(1, (size_t)span);
    if (!g_loadbuf) return 0;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const openos_elf64_phdr_t *ph =
            (const openos_elf64_phdr_t *)(base + eh->e_phoff +
                                          (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != OPENOS_PT_LOAD) continue;
        memcpy(g_loadbuf + (ph->p_vaddr - min_v),
               base + ph->p_offset, (size_t)ph->p_filesz);
    }
    /* load_bias 使得 vaddr+bias == g_loadbuf + (vaddr - min_v) */
    return (uint64_t)(uintptr_t)g_loadbuf - min_v;
}

/* 用系统 gcc 造一个 PIE 动态可执行文件；成功返回路径，失败返回 NULL */
static const char *build_pie_sample(void) {
    static const char out[] = "/tmp/openos_m51a_sample.elf";
    FILE *f = fopen("/tmp/openos_m51a_sample.c", "w");
    if (!f) return NULL;
    fputs("#include <stdio.h>\n"
          "int g_val = 42;\n"
          "int main(void){ printf(\"%d\\n\", g_val); return 0; }\n", f);
    fclose(f);
    int rc = system("gcc -pie -fPIE -O0 /tmp/openos_m51a_sample.c "
                    "-o /tmp/openos_m51a_sample.elf 2>/dev/null");
    if (rc != 0) return NULL;
    return out;
}

/* ------------------------------------------------------------------ */
UNIT_TEST_CASE(reject_null_and_tiny)
{
    openos_elf64_dyninfo_t info;
    uint8_t buf[64];
    ASSERT_TRUE(openos_elf64_parse_dynamic(NULL, 100, 0, &info) != 0);
    ASSERT_TRUE(openos_elf64_parse_dynamic(buf, 64, 0, NULL) != 0);
    ASSERT_TRUE(openos_elf64_parse_dynamic(buf, 4, 0, &info) != 0);
}

UNIT_TEST_CASE(reject_garbage)
{
    uint8_t junk[128];
    memset(junk, 0xAB, sizeof(junk));
    openos_elf64_dyninfo_t info;
    ASSERT_TRUE(openos_elf64_parse_dynamic(junk, sizeof(junk), 0, &info) != 0);
}

UNIT_TEST_CASE(parse_pie_dynamic)
{
    const char *path = build_pie_sample();
    if (!path) {
        /* gcc/-pie 环境不可用，本用例视为通过（无法构造样本）*/
        printf("    [skip] gcc -pie unavailable\n");
        return;
    }

    uint64_t sz = 0;
    void *img = map_file(path, &sz);
    ASSERT_TRUE(img != NULL);
    ASSERT_TRUE(sz > 64);

    uint64_t bias = simulate_load(img, sz);
    ASSERT_TRUE(g_loadbuf != NULL);

    openos_elf64_dyninfo_t info;
    int rc = openos_elf64_parse_dynamic(img, sz, bias, &info);
    ASSERT_EQ_INT(0, rc);

    ASSERT_EQ_INT(1, info.is_pie);
    ASSERT_TRUE(info.interp != NULL);
    ASSERT_TRUE(strstr(info.interp, "ld-") != NULL ||
                strstr(info.interp, "/lib") != NULL);

    ASSERT_TRUE(info.strtab != NULL);
    ASSERT_TRUE(info.symtab != NULL);
    ASSERT_TRUE(info.strsz > 0);
    ASSERT_TRUE(info.dyn_count > 0);
    ASSERT_TRUE(info.entry != 0);

    ASSERT_TRUE(info.needed_count >= 1);
    int found_libc = 0;
    for (int i = 0; i < info.needed_count; i++) {
        uint32_t off = info.needed[i];
        if (off < info.strsz && strstr(info.strtab + off, "libc") != NULL) {
            found_libc = 1;
        }
    }
    ASSERT_TRUE(found_libc == 1);

    munmap(img, (size_t)sz);
    free(g_loadbuf);
    g_loadbuf = NULL;
}

int main(void)
{
    UNIT_TEST_RUN(reject_null_and_tiny);
    UNIT_TEST_RUN(reject_garbage);
    UNIT_TEST_RUN(parse_pie_dynamic);
    return unit_test_finish();
}
