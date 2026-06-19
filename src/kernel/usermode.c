/* ============================================================
 * openos - 用户态切换实�?
 * 修复: 用户代码/栈必须用 USER 标志映射，否�?Ring 3 无法访问
 * ============================================================ */

#include "include/usermode.h"
#include "include/gdt.h"
#include "include/pmm.h"
#include "include/vmm.h"
#include "include/serial.h"
#include <stddef.h>

/* 外部汇编函数 */
extern void switch_to_user_asm(uint32_t eip, uint32_t esp);

/* 用户态测试代码（简单的 hlt 循环�?*/
/* 这些字节会被复制到用户可访问的内存页 */
static uint8_t user_code_template[] = {
    0x90,             /* nop */
    0x90,             /* nop */
    0x90,             /* nop */
    0xEB, 0xFE,       /* jmp $ (无限循环) */
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
};

/* 用户代码页地址（必须是用户可访问的�?*/
#define USER_CODE_ADDR    0x40000000  /* 1GB 处，专用于用户代�?*/
#define USER_CODE_SIZE    4096

/* ============================================================
 * 分配用户栈（使用 USER 标志映射�?
 * ============================================================ */
uint32_t alloc_user_stack_slot(uint32_t slot)
{
    uint32_t span = USER_STACK_GUARD_SIZE + USER_STACK_SIZE;
    uint32_t guard_base = USER_STACK_ADDR - slot * span;
    uint32_t stack_base = guard_base + USER_STACK_GUARD_SIZE;
    uint32_t stack_top = stack_base + USER_STACK_SIZE;
    uint32_t stack_pages = (USER_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;

    if (stack_pages == 0 || stack_top <= stack_base || guard_base >= USER_STACK_ADDR + span) {
        serial_write("[USERMODE] ERROR: invalid user stack slot\n");
        return 0;
    }

    for (uint32_t page = 0; page < stack_pages; page++) {
        uint32_t phys = (uint32_t)pmm_alloc_page();
        if (!phys) {
            serial_write("[USERMODE] ERROR: cannot alloc user stack page\n");
            for (uint32_t done = 0; done < page; done++) {
                uint32_t done_virt = stack_base + done * PAGE_SIZE;
                uint32_t pte = vmm_get_mapping(done_virt);
                if ((pte & PTE_PRESENT) && (pte & PTE_USER)) {
                    pmm_free_page((void *)(pte & PAGE_MASK));
                    vmm_unmap_page(done_virt);
                }
            }
            return 0;
        }

        uint32_t virt = stack_base + page * PAGE_SIZE;
        vmm_map_page(virt, phys, VMM_USER);

    }


    return stack_top;
}

uint32_t alloc_user_stack(void)
{
    return alloc_user_stack_slot(0);
}

uint32_t alloc_user_stack_randomized(uint32_t slot)
{
    return alloc_user_stack_slot(slot);
}

void free_user_stack_slot(uint32_t slot)
{
    uint32_t span = USER_STACK_GUARD_SIZE + USER_STACK_SIZE;
    uint32_t guard_base = USER_STACK_ADDR - slot * span;
    uint32_t stack_base = guard_base + USER_STACK_GUARD_SIZE;
    uint32_t stack_top = stack_base + USER_STACK_SIZE;
    uint32_t stack_pages = (USER_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;

    if (stack_pages == 0 || stack_top <= stack_base || guard_base >= USER_STACK_ADDR + span) {
        return;
    }

    for (uint32_t page = 0; page < stack_pages; page++) {
        uint32_t virt = stack_base + page * PAGE_SIZE;
        uint32_t pte = vmm_get_mapping(virt);
        if ((pte & PTE_PRESENT) && (pte & PTE_USER)) {
            pmm_free_page((void *)(pte & PAGE_MASK));
            vmm_unmap_page(virt);
        }
    }
}
/* ============================================================
 * 分配并准备用户代码页（使�?USER 标志映射�?
 * ============================================================ */
static uint32_t alloc_user_code_page(void)
{
    /* 分配物理页用于用户代�?*/
    uint32_t phys = (uint32_t)pmm_alloc_page();
    if (!phys) {
        serial_write("[USERMODE] ERROR: cannot alloc user code page\n");
        return 0;
    }
    
    /* �?VMM_USER 映射，这�?Ring 3 可以执行 */
    vmm_map_page(USER_CODE_ADDR, phys, VMM_USER);
    
    /* 复制代码到用户页（通过恒等映射写入，因为我们是内核�?*/
    uint8_t *code_page = (uint8_t *)phys;  /* 恒等映射 */
    for (size_t i = 0; i < sizeof(user_code_template); i++) {
        code_page[i] = user_code_template[i];
    }
    
    serial_write("[USERMODE] Code: phys=");
    serial_write_hex(phys);
    serial_write(" virt=");
    serial_write_hex((uint32_t)USER_CODE_ADDR);
    serial_write("\n");
    
    return USER_CODE_ADDR;
}

/* ============================================================
 * 测试用户态切�?
 * ============================================================ */
void test_user_mode_switch(void)
{
    serial_write("[USERMODE] === Testing user mode switch ===\n");
    
    /* 1. 分配用户代码页（USER 标志映射�?*/
    uint32_t user_code = alloc_user_code_page();
    if (!user_code) {
        serial_write("[USERMODE] ERROR: failed to alloc user code\n");
        return;
    }
    
    /* 2. 分配用户栈（USER 标志映射�?*/
    uint32_t user_stack = alloc_user_stack();
    if (!user_stack) {
        serial_write("[USERMODE] ERROR: failed to alloc user stack\n");
        return;
    }
    
    /* 3. 打印 kernel_stack_top 地址（用于调试） */
    extern char kernel_stack_top[];
    serial_write("[USERMODE] kernel_stack_top = 0x");
    serial_write_hex((uint32_t)kernel_stack_top);
    serial_write("\n");
    
    /* 4. 切换到用户�?*/
    serial_write("[USERMODE] Calling switch_to_user_asm...\n");
    serial_write("[USERMODE] code_eip=0x");
    serial_write_hex(user_code);
    serial_write(" user_esp=0x");
    serial_write_hex(user_stack);
    serial_write("\n");
    
    /* 传入用户代码入口和用户栈�?*/
    switch_to_user_asm(user_code, user_stack);
    
    /* 不应到达这里！如果到达说�?iret 成功了然后又跳回�?*/
    serial_write("[USERMODE] Returned from user mode (iret success?)\n");
}

/* ============================================================
 * 设置 TSS 的内核栈
 * ============================================================ */
void tss_set_kernel_stack(uint32_t esp0)
{
    extern void tss_set_stack(uint32_t esp);
    tss_set_stack(esp0);
}