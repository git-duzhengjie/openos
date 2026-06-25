/* ============================================================
 * openos - ELF32 加载器 (Phase 3)
 * ============================================================ */

#ifndef KERNEL_PROC_ELF_LOADER_H
#define KERNEL_PROC_ELF_LOADER_H

#include <stdint.h>

/* ELF32 魔数 */
#define ELF_MAGIC 0x464C457F  /* "\x7FELF" */

/* ELF 类型 */
#define ET_NONE 0
#define ET_REL  1   /* 可重定位 */
#define ET_EXEC 2   /* 可执行 */
#define ET_DYN  3   /* 共享对象 */

/* ELF 机器类型 */
#define EM_386  3   /* i386 */

/* 程序头类型 */
#define PT_NULL    0
#define PT_LOAD    1   /* 可加载段 */
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6

/* 程序头标志 */
#define PF_X 0x1  /* 可执行 */
#define PF_W 0x2  /* 可写 */
#define PF_R 0x4  /* 可读 */

/* ============================================================
 * ELF32 头结构
 * ============================================================ */
typedef struct {
    uint8_t  e_ident[16];    /* 魔数 + 其他信息 */
    uint16_t e_type;         /* 文件类型 */
    uint16_t e_machine;      /* 机器类型 */
    uint32_t e_version;      /* ELF 版本 */
    uint32_t e_entry;        /* 入口点 */
    uint32_t e_phoff;        /* 程序头表偏移 */
    uint32_t e_shoff;        /* 节头表偏移 */
    uint32_t e_flags;        /* 处理器特定标志 */
    uint16_t e_ehsize;       /* ELF 头大小 */
    uint16_t e_phentsize;    /* 程序头条目大小 */
    uint16_t e_phnum;        /* 程序头条目数 */
    uint16_t e_shentsize;    /* 节头条目大小 */
    uint16_t e_shnum;        /* 节头条目数 */
    uint16_t e_shstrndx;     /* 节名字字符串表索引 */
} Elf32_Ehdr;

/* ============================================================
 * ELF32 程序头结构
 * ============================================================ */
typedef struct {
    uint32_t p_type;         /* 段类型 */
    uint32_t p_offset;       /* 文件偏移 */
    uint32_t p_vaddr;        /* 虚拟地址 */
    uint32_t p_paddr;        /* 物理地址 */
    uint32_t p_filesz;       /* 文件大小 */
    uint32_t p_memsz;        /* 内存大小 */
    uint32_t p_flags;        /* 标志 */
    uint32_t p_align;        /* 对齐 */
} Elf32_Phdr;

/* ============================================================
 * 加载结果
 * ============================================================ */
typedef struct {
    uint32_t entry;          /* 入口点 */
    uint32_t brk_start;      /* brk 起始地址 (用于 malloc) */
    int      num_segments;   /* 加载的段数 */
} elf_load_result_t;

/* ============================================================
 * 函数声明
 * ============================================================ */

/**
 * 从文件描述符加载 ELF 可执行文件
 * @param fd 文件描述符 (已打开的 ELF 文件)
 * @return 加载结果，entry=0 表示失败
 */
elf_load_result_t elf_load(int fd);

/**
 * 验证 ELF 头是否为有效的 32 位可执行文件
 * @param ehdr ELF 头指针
 * @return 1 有效, 0 无效
 */
int elf_validate_header(Elf32_Ehdr *ehdr);

#endif /* KERNEL_PROC_ELF_LOADER_H */