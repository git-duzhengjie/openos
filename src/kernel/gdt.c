/* ============================================================
 * openos - GDT 实现
 * ============================================================ */

#include "include/gdt.h"

/* 外部汇编函数 */
extern void gdt_flush(uint32_t gdt_ptr);

/* GDT 表和 TSS */
static gdt_entry_t gdt[GDT_MAX];
static gdtr_t gdtr;
static tss_t tss;

/* ============================================================
 * 设置单个 GDT 条目
 * ============================================================ */
void gdt_set_gate(int num, uint32_t base, uint32_t limit,
                  uint8_t access, uint8_t flags)
{
    if (num >= GDT_MAX)
        return;

    /* 基地址分解为三部分 */
    gdt[num].base_low  = (uint16_t)(base & 0xFFFF);
    gdt[num].base_mid  = (uint8_t)((base >> 16) & 0xFF);
    gdt[num].base_high = (uint8_t)((base >> 24) & 0xFF);

    /* 界限分解为两部分 */
    gdt[num].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[num].flags_limit = (uint8_t)((flags << 4) | ((limit >> 16) & 0x0F));

    /* 访问权限字节 */
    gdt[num].access = access;
}

/* ============================================================
 * 初始化 GDT
 * ============================================================ */
void gdt_init(void)
{
    /* 清空 TSS */
    for (int i = 0; i < sizeof(tss_t); i++)
        ((uint8_t *)&tss)[i] = 0;

    /* 设置 TSS 描述符 (base=0, limit=sizeof(tss_t)-1) */
    uint32_t tss_base = (uint32_t)&tss;
    uint32_t tss_limit = sizeof(tss_t) - 1;

    /* TSS 描述符: 存在、DPL=0、32位、可执行、ring0 */
    /* Access: P=1, DPL=00, S=0, Type=1001 (32bit TSS busy) */
    gdt_set_gate(5, tss_base, tss_limit, 0x89, 0x40);

    /* 内核代码段: base=0, limit=4GB, 粒度4KB, 32位 */
    gdt_set_gate(1, 0x00000000, 0xFFFFF, GDT_ACCESS_CODE, GDT_FLAG_4K | GDT_FLAG_32);
    /* 内核数据段: base=0, limit=4GB */
    gdt_set_gate(2, 0x00000000, 0xFFFFF, GDT_ACCESS_DATA, GDT_FLAG_4K | GDT_FLAG_32);
    /* 用户代码段: base=0, limit=4GB */
    gdt_set_gate(3, 0x00000000, 0xFFFFF, GDT_ACCESS_USER_CODE, GDT_FLAG_4K | GDT_FLAG_32);
    /* 用户数据段: base=0, limit=4GB */
    gdt_set_gate(4, 0x00000000, 0xFFFFF, GDT_ACCESS_USER_DATA, GDT_FLAG_4K | GDT_FLAG_32);

    /* 加载 GDTR */
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint32_t)&gdt;

    gdt_flush((uint32_t)&gdtr);
}

/* ============================================================
 * 初始化 TSS
 * ============================================================ */
void tss_init(uint32_t esp0)
{
    tss.esp0 = esp0;
    tss.ss0  = GDT_KERNEL_DATA;  /* 0x10 */
}

/* ============================================================
 * 设置内核栈 (系统调用时使用)
 * ============================================================ */
void tss_set_stack(uint32_t esp0)
{
    tss.esp0 = esp0;
}