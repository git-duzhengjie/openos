#ifndef OPENOS_ARCH_X86_64_ELF64_DYNAMIC_H
#define OPENOS_ARCH_X86_64_ELF64_DYNAMIC_H

/*
 * elf64_dynamic.h - M5.1 动态链接支持（内核态重定位方案）
 *
 * 本模块在 elf64_loader.c 的静态 PT_LOAD 加载能力之上，扩展：
 *   M5.1a - 解析 PT_INTERP / PT_DYNAMIC，抽取 .dynamic 表项
 *   M5.1b - 应用基础重定位（R_X86_64_RELATIVE/GLOB_DAT/64 等）
 *   M5.1c - 跨模块符号解析 + 共享库(.so)加载
 *   M5.1d - 惰性绑定 PLT/GOT (R_X86_64_JUMP_SLOT + _dl_runtime_resolve)
 *
 * 设计原则：零依赖 elf64_loader.c 的私有类型，本文件自带 ELF64 结构定义，
 * 供动态链接路径独立使用。
 */

#include <stdint.h>

/* ============================ ELF64 基础类型 ============================ */

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) openos_elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) openos_elf64_phdr_t;

/* .dynamic 表项 */
typedef struct {
    int64_t  d_tag;
    uint64_t d_val;   /* 与 d_ptr 联合，统一用 uint64_t */
} __attribute__((packed)) openos_elf64_dyn_t;

/* 符号表项 */
typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} __attribute__((packed)) openos_elf64_sym_t;

/* 带加数的重定位项 (RELA) */
typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} __attribute__((packed)) openos_elf64_rela_t;

/* r_info 拆分宏 */
#define OPENOS_ELF64_R_SYM(i)   ((uint32_t)((i) >> 32))
#define OPENOS_ELF64_R_TYPE(i)  ((uint32_t)((i) & 0xffffffffULL))

/* st_info 拆分宏 */
#define OPENOS_ELF64_ST_BIND(i) ((uint8_t)((i) >> 4))
#define OPENOS_ELF64_ST_TYPE(i) ((uint8_t)((i) & 0xf))

/* ======================== Program header 类型 ======================== */
#define OPENOS_PT_NULL     0U
#define OPENOS_PT_LOAD     1U
#define OPENOS_PT_DYNAMIC  2U
#define OPENOS_PT_INTERP   3U
#define OPENOS_PT_NOTE     4U
#define OPENOS_PT_PHDR     6U
#define OPENOS_PT_TLS      7U
#define OPENOS_PT_GNU_RELRO 0x6474e552U

/* ELF 文件类型 */
#define OPENOS_ET_EXEC     2U
#define OPENOS_ET_DYN      3U

/* ======================== Dynamic 表项 tag ======================== */
#define OPENOS_DT_NULL      0    /* 结束标记 */
#define OPENOS_DT_NEEDED    1    /* 依赖的 .so 名字（strtab 偏移） */
#define OPENOS_DT_PLTRELSZ  2    /* PLT 重定位表字节数 */
#define OPENOS_DT_PLTGOT    3    /* GOT/PLT 地址 */
#define OPENOS_DT_HASH      4    /* 符号哈希表 */
#define OPENOS_DT_STRTAB    5    /* 字符串表地址 */
#define OPENOS_DT_SYMTAB    6    /* 符号表地址 */
#define OPENOS_DT_RELA      7    /* RELA 重定位表地址 */
#define OPENOS_DT_RELASZ    8    /* RELA 表字节数 */
#define OPENOS_DT_RELAENT   9    /* 单个 RELA 项字节数 */
#define OPENOS_DT_STRSZ     10   /* 字符串表字节数 */
#define OPENOS_DT_SYMENT    11   /* 单个符号项字节数 */
#define OPENOS_DT_INIT      12   /* 初始化函数地址 */
#define OPENOS_DT_FINI      13   /* 终结函数地址 */
#define OPENOS_DT_SONAME    14   /* 共享库名字（strtab 偏移） */
#define OPENOS_DT_RPATH     15
#define OPENOS_DT_SYMBOLIC  16
#define OPENOS_DT_REL       17
#define OPENOS_DT_RELSZ     18
#define OPENOS_DT_RELENT    19
#define OPENOS_DT_PLTREL    20   /* PLT 重定位类型：DT_RELA 或 DT_REL */
#define OPENOS_DT_DEBUG     21
#define OPENOS_DT_TEXTREL   22
#define OPENOS_DT_JMPREL    23   /* PLT 重定位表地址 */
#define OPENOS_DT_BIND_NOW  24
#define OPENOS_DT_INIT_ARRAY   25
#define OPENOS_DT_FINI_ARRAY   26
#define OPENOS_DT_INIT_ARRAYSZ 27
#define OPENOS_DT_FINI_ARRAYSZ 28
#define OPENOS_DT_FLAGS     30
#define OPENOS_DT_GNU_HASH  0x6ffffef5

/* ======================== x86_64 重定位类型 ======================== */
#define OPENOS_R_X86_64_NONE       0
#define OPENOS_R_X86_64_64         1   /* S + A（64位绝对） */
#define OPENOS_R_X86_64_PC32       2
#define OPENOS_R_X86_64_GLOB_DAT   6   /* S（GOT 项填符号绝对地址） */
#define OPENOS_R_X86_64_JUMP_SLOT  7   /* S（PLT 项，惰性/立即绑定） */
#define OPENOS_R_X86_64_RELATIVE   8   /* B + A（基址 + 加数） */
#define OPENOS_R_X86_64_32         10
#define OPENOS_R_X86_64_32S        11
#define OPENOS_R_X86_64_DTPMOD64   16
#define OPENOS_R_X86_64_DTPOFF64   17
#define OPENOS_R_X86_64_TPOFF64    18
#define OPENOS_R_X86_64_IRELATIVE  37

/* 符号绑定 / 类型 */
#define OPENOS_STB_LOCAL   0
#define OPENOS_STB_GLOBAL  1
#define OPENOS_STB_WEAK    2
#define OPENOS_STT_NOTYPE  0
#define OPENOS_STT_OBJECT  1
#define OPENOS_STT_FUNC    2

/* ==================== 解析结果：动态段视图 ==================== */
/*
 * 一个已加载模块（可执行或 .so）的动态链接元信息。
 * 所有指针均为“运行时有效虚拟地址”（已加上 load_bias）。
 */
typedef struct {
    uint64_t load_bias;      /* 模块实际加载基址 - p_vaddr(0)，ET_EXEC 通常为 0 */
    uint64_t entry;          /* 运行时入口 = e_entry + load_bias */
    int      is_pie;         /* ET_DYN=1, ET_EXEC=0 */

    const char *interp;      /* PT_INTERP 字符串（若有），指向已加载镜像内 */

    /* .dynamic 表 */
    const openos_elf64_dyn_t *dyn;
    uint64_t dyn_count;

    /* 从 .dynamic 抽取的关键指针（已加 load_bias） */
    const char *strtab;
    uint64_t    strsz;
    const openos_elf64_sym_t *symtab;
    uint64_t    syment;

    const openos_elf64_rela_t *rela;   /* DT_RELA */
    uint64_t rela_sz;
    uint64_t rela_ent;

    const openos_elf64_rela_t *jmprel; /* DT_JMPREL (PLT) */
    uint64_t pltrel_sz;
    int64_t  pltrel_type;              /* DT_RELA(7) or DT_REL(17) */

    uint64_t *pltgot;                  /* DT_PLTGOT */

    uint64_t init;                     /* DT_INIT */
    uint64_t fini;                     /* DT_FINI */
    uint64_t init_array;
    uint64_t init_arraysz;

    /* 依赖列表：DT_NEEDED 的 strtab 偏移，最多记录 16 个 */
    uint32_t needed[16];
    int      needed_count;
} openos_elf64_dyninfo_t;

/* ============================ 公共 API ============================ */

/*
 * M5.1a：从已加载到内存的 ELF 镜像中解析动态段。
 *   image      - 指向内存中完整 ELF 文件（用于读 phdr/找 PT_DYNAMIC 的文件偏移）
 *   image_size - 镜像字节数（边界检查）
 *   load_bias  - 段被加载后的基址偏移（ET_EXEC 传 0；PIE 传实际基址）
 *   out        - 输出解析结果
 * 返回 0 成功，负数失败。
 * 说明：本函数假定 PT_LOAD 段已被 elf64_loader 映射到 (p_vaddr + load_bias)，
 * 因此 .dynamic 的运行时地址可由 d_ptr + load_bias 得到。
 */
int openos_elf64_parse_dynamic(const void *image, uint64_t image_size,
                               uint64_t load_bias,
                               openos_elf64_dyninfo_t *out);

/* 调试：打印 dyninfo 概要到串口 */
void openos_elf64_dyninfo_dump(const openos_elf64_dyninfo_t *info);

#endif /* OPENOS_ARCH_X86_64_ELF64_DYNAMIC_H */
