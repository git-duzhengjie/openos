/* ============================================================
 * openos - GDT 定义与初始化
 * ============================================================ */

#ifndef KERNEL_GDT_H
#define KERNEL_GDT_H

#include <stdint.h>

/* 段描述符特权级 */
#define GDT_PL0    0x00    /* 特权级0 (内核) */
#define GDT_PL3    0x60    /* 特权级3 (用户) */

/* 描述符类型 */
#define GDT_ACCESS_CODE  0x9A    /* 存在、ring0、代码、可执行、已读取 */
#define GDT_ACCESS_DATA  0x92    /* 存在、ring0、数据、可写 */
#define GDT_ACCESS_USER_CODE 0xFA /* 存在、ring3、代码、可执行、已读取 */
#define GDT_ACCESS_USER_DATA 0xF2 /* 存在、ring3、数据、可写 */

/* 标志 (高4位，作为4-bit值传入 gdt_set_gate 的 flags 参数) */
#define GDT_GRAN_4K    0x8    /* 4KB粒度 (G=1) */
#define GDT_DB_32BIT   0x4    /* 32位代码/数据 (D=1, 必须设1) */
#define GDT_LONG_MODE  0x2    /* 长模式 (L=1) - 32位模式设0 */
#define GDT_AVL        0x1    /* 可用位 (AVL) - 通常设0 */

/* 向后兼容的旧宏名 */
#define GDT_FLAG_4K    GDT_GRAN_4K
#define GDT_FLAG_32    GDT_DB_32BIT
#define GDT_FLAG_LIM   GDT_LONG_MODE

/* 段选择子索引 */
#define GDT_KERNEL_CODE 0x08   /* 内核代码段 (index=1, RPL=0) */
#define GDT_KERNEL_DATA 0x10   /* 内核数据段 (index=2, RPL=0) */
#define GDT_USER_CODE   0x18   /* 用户代码段 (index=3, RPL=3) */
#define GDT_USER_DATA   0x20   /* 用户数据段 (index=4, RPL=3) */

/* TSS 段选择子 */
#define GDT_TSS 0x28           /* index=5 */

/* GDT 最大条目数 */
#define GDT_MAX 16

/* ============================================================
 * GDT 条目结构 (8字节)
 * ============================================================ */
typedef struct {
    uint16_t limit_low;       /* 段界限低16位 */
    uint16_t base_low;        /* 基地址低16位 */
    uint8_t  base_mid;        /* 基地址中8位 */
    uint8_t  access;          /* 访问权限字节 */
    uint8_t  flags_limit;     /* 高4位flags + 低4位段界限高4位 */
    uint8_t  base_high;       /* 基地址高8位 */
} __attribute__((packed)) gdt_entry_t;

/* ============================================================
 * GDTR 寄存器结构 (6字节)
 * ============================================================ */
typedef struct {
    uint16_t limit;           /* GDT大小-1 */
    uint32_t base;            /* GDT基地址 */
} __attribute__((packed)) gdtr_t;

/* ============================================================
 * 任务状态段 (TSS) - 用于硬件上下文切换
 * ============================================================ */
typedef struct {
    uint32_t prev_tss;        /* 上一个TSS */
    uint32_t esp0;            /* 内核栈指针 (ring0) */
    uint32_t ss0;             /* 内核栈段 (ring0) */
    uint32_t esp1;            /* ring1 栈指针 */
    uint32_t ss1;
    uint32_t esp2;            /* ring2 栈指针 */
    uint32_t ss2;
    uint32_t cr3;             /* 页目录寄存器 */
    uint32_t eip;             /* 指令指针 */
    uint32_t eflags;          /* 标志寄存器 */
    uint32_t eax, ecx, edx, ebx; /* 通用寄存器 */
    uint32_t esp, ebp, esi, edi; /* 栈/基址/索引寄存器 */
    uint32_t es, cs, ss, ds, fs, gs; /* 段寄存器 */
    uint32_t ldt;             /* 本地描述符表 */
    uint16_t trap;            /* 调试陷阱标志 */
    uint16_t iomap_base;      /* I/O许可位图基址 */
} __attribute__((packed)) tss_t;

/* ============================================================
 * 函数声明
 * ============================================================ */
void gdt_init(void);                    /* 初始化GDT */
void gdt_set_gate(int num, uint32_t base, uint32_t limit,
                 uint8_t access, uint8_t flags);  /* 设置单个GDT条目 */
void gdt_flush(uint32_t gdt_ptr);               /* 加载GDTR (参数: gdtr_t*) */
void tss_flush(void);                    /* 加载TSS */
void tss_init(uint32_t esp0);           /* 初始化TSS */
void tss_set_stack(uint32_t esp0);     /* 设置内核栈 */

#endif /* KERNEL_GDT_H */