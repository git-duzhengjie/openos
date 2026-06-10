/* ============================================================
 * openos - 进程管理实现 (Phase 3)
 * ============================================================ */

#include "process.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"
#include "string.h"
#include "elf_loader.h"
#include "vfs.h"
#include "usermode.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

/* 外部函数 (usermode.c) */
extern uint32_t alloc_user_stack(void);
extern void switch_to_user_asm(uint32_t eip, uint32_t esp);

/* ---- 进程表 ---- */
static process_t proc_table[MAX_PROCS];
static uint32_t next_pid = 1;  /* PID 0 = idle */

void proc_table_init(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        proc_table[i].pid = 0;
        proc_table[i].state = PROC_DEAD;
        proc_table[i].threads = NULL;
        proc_table[i].thread_count = 0;
        proc_table[i].ppid = 0;
        proc_table[i].cr3 = 0;
        proc_table[i].exit_code = 0;
    }
}

/* 分配空闲 PCB */
process_t *proc_alloc(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state == PROC_DEAD) {
            process_t *p = &proc_table[i];
            p->pid = next_pid++;
            p->state = PROC_READY;
            p->ppid = 0;
            p->threads = NULL;
            p->thread_count = 0;
            p->cr3 = 0;
            p->exit_code = 0;
            p->code_addr = 0;
            p->code_size = 0;
            p->heap_start = 0;
            p->heap_end = 0;
            for (int j = 0; j < 31; j++) p->name[j] = 0;
            p->name[0] = '\0';
            /* 初始化文件描述符表 */
            for (int j = 0; j < 16; j++) p->fds[j] = NULL;
            /* 初始化当前工作目录为 / */
            p->cwd[0] = '/';
            p->cwd[1] = '\0';
            return p;
        }
    }
    return NULL;
}

void proc_free(process_t *proc) {
    if (!proc) return;
    proc->pid = 0;
    proc->state = PROC_DEAD;
}

process_t *proc_find(uint32_t pid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].pid == pid && proc_table[i].state != PROC_DEAD)
            return &proc_table[i];
    }
    return NULL;
}

process_t *proc_find_by_state(process_state_t state) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state == state)
            return &proc_table[i];
    }
    return NULL;
}

process_t *proc_get_parent(uint32_t pid) {
    process_t *p = proc_find(pid);
    if (!p) return NULL;
    return proc_find(p->ppid);
}

void proc_add_child(process_t *parent, process_t *child) {
    if (!parent || !child) return;
    child->ppid = parent->pid;
}

/* ============================================================
 * sys_fork - 复制当前进程
 *
 * 1. 分配新 PCB
 * 2. 复制地址空间 (COW 暂不实现,直接复制物理页)
 * 3. 复制内核栈帧到新线程
 * 4. 子进程返回 0,父进程返回子 PID
 * ============================================================ */
uint32_t sys_fork(void) {
    thread_t *cur = sched_get_current();
    if (!cur) return (uint32_t)-1;

    process_t *parent = proc_find(cur->pid);
    if (!parent) {
        /* 如果当前线程没有关联进程,创建一个 */
        parent = proc_alloc();
        if (!parent) return (uint32_t)-1;
        parent->cr3 = vmm_get_cr3();
        cur->pid = parent->pid;
        for (int i = 0; i < 31 && "init"[i]; i++)
            parent->name[i] = "init"[i];
    }

    /* 分配子进程 PCB */
    process_t *child = proc_alloc();
    if (!child) return (uint32_t)-1;
    child->ppid = parent->pid;
    for (int i = 0; i < 31 && parent->name[i]; i++)
        child->name[i] = parent->name[i];

    /* 复制地址空间:分配新页目录,复制所有映射 */
    uint32_t parent_cr3 = vmm_get_cr3();
    uint32_t *parent_pgd = (uint32_t *)(parent_cr3 & ~0xFFF);

    /* 分配新页目录 */
    uint32_t child_pd_phys = (uint32_t)pmm_alloc_page();
    if (!child_pd_phys) { proc_free(child); return (uint32_t)-1; }
    uint32_t *child_pgd = (uint32_t *)child_pd_phys;

    /* 复制内核映射 (高地址部分,恒等映射下全部复制) */
    for (int i = 0; i < 1024; i++) {
        if (parent_pgd[i] & PTE_PRESENT) {
            /* 分配新页表 */
            uint32_t pt_phys = (uint32_t)pmm_alloc_page();
            if (!pt_phys) {
                /* TODO: 清理已分配的页 */
                proc_free(child);
                return (uint32_t)-1;
            }
            uint32_t *parent_pt = (uint32_t *)(parent_pgd[i] & ~0xFFF);
            uint32_t *child_pt = (uint32_t *)pt_phys;

            /* 复制页表条目 */
            for (int j = 0; j < 1024; j++) {
                if (parent_pt[j] & PTE_PRESENT) {
                    /* 分配新物理页,复制内容 */
                    uint32_t page_phys = (uint32_t)pmm_alloc_page();
                    if (!page_phys) break;

                    /* 复制页面内容 */
                    uint8_t *src = (uint8_t *)(parent_pt[j] & ~0xFFF);
                    uint8_t *dst = (uint8_t *)page_phys;
                    for (int k = 0; k < 4096; k++)
                        dst[k] = src[k];

                    child_pt[j] = page_phys | (parent_pt[j] & 0xFFF);
                } else {
                    child_pt[j] = 0;
                }
            }
            child_pgd[i] = pt_phys | (parent_pgd[i] & 0xFFF);
        } else {
            child_pgd[i] = 0;
        }
    }

    child->cr3 = child_pd_phys;

    /* 创建子线程,复制父线程的栈帧 */
    uint32_t child_stack = (uint32_t)pmm_alloc_page() + 4096;
    if (!child_stack) { proc_free(child); return (uint32_t)-1; }

    /* 复制父线程的内核栈 */
    uint8_t *src_stack = (uint8_t *)cur->kernel_stack;
    uint8_t *dst_stack = (uint8_t *)(child_stack - 4096);
    for (int i = 0; i < 4096; i++)
        dst_stack[i] = src_stack[i];

    thread_t *child_thread = thread_create(child->pid, child->name,
                                           cur->kernel_eip, child_stack);
    if (!child_thread) { proc_free(child); return (uint32_t)-1; }

    /* 子线程的栈帧从父线程复制,但 EAX=0 (fork 返回 0) */
    /* 栈帧偏移: GS FS ES DS EDI ESI EBP ESP_skip EBX EDX ECX EAX EIP CS EFLAGS */
    /* EAX 是第 12 个 DWORD (从栈顶往下) */
    uint32_t *child_sp = (uint32_t *)child_thread->kernel_esp;
    /* 从 kernel_esp 往下找 EAX 的位置:
     * push GS, FS, ES, DS = 4
     * pusha: EDI, ESI, EBP, ESP(skip), EBX, EDX, ECX, EAX = 8
     * 共 12 个 DWORD,EAX 在 kernel_esp[8] 位置 (0-indexed: GS=0,FS=1,ES=2,DS=3,EDI=4,ESI=5,EBP=6,ESP=7,EBX=8,EDX=9,ECX=10,EAX=11)
     */
    child_sp[8] = 0;  /* EBX */
    child_sp[10] = 0; /* ECX */
    child_sp[11] = 0; /* EAX = 0 (子进程 fork 返回 0) */

    child_thread->pid = child->pid;
    child->threads = child_thread;
    child->thread_count = 1;
    child->state = PROC_READY;

    /* 将子线程加入调度队列 */
    sched_add_thread(child_thread);

    serial_write("[FORK] child pid=");
    serial_write_hex(child->pid);
    serial_write("\n");

    return child->pid;  /* 父进程返回子 PID */
}

/* ============================================================
 * sys_exec - 替换当前进程的内存映像
 *
 * Phase 3 简化版:加载新的代码页到 0x40000000
 * TODO: 从文件系统加载 ELF
 * ============================================================ */
int sys_exec(const char *path, char *const argv[]) {
    (void)argv;
    thread_t *cur = sched_get_current();
    if (!cur) return -1;

    process_t *proc = proc_find(cur->pid);
    if (!proc) {
        serial_write("[EXEC] No process for pid=");
        serial_write_hex(cur->pid);
        serial_write(", allocating...\n");
        proc = proc_alloc();
        if (!proc) return -1;
        proc->cr3 = vmm_get_cr3();
        cur->pid = proc->pid;  /* 关联线程到新进程 */
    }

    /* 打开 ELF 文件 */
    int fd = vfs_open(path, 0, 0);
    serial_write("[EXEC] vfs_open returned fd=");
    serial_write_hex(fd);
    serial_write("\n");
    if (fd < 0) {
        serial_write("[EXEC] Cannot open: ");
        serial_write((char *)path);
        serial_write("\n");
        return -1;
    }

    /* 加载 ELF */
    elf_load_result_t load_result = elf_load(fd);
    vfs_close(fd);  /* 关闭文件 */

    if (load_result.entry == 0) {
        serial_write("[EXEC] ELF load failed\n");
        return -1;
    }

    /* 更新进程信息 */
    proc->code_addr = load_result.brk_start;  /* 实际上是 brk 起点 */
    proc->heap_start = load_result.brk_start;
    proc->heap_end = load_result.brk_start;

    serial_write("[EXEC] Loaded, entry=0x");
    serial_write_hex(load_result.entry);
    serial_write("\n");

    /* 分配用户栈 */
    uint32_t stack_top = alloc_user_stack();
    if (!stack_top) return -1;

    /* 切换到用户态执行 */
    switch_to_user_asm(load_result.entry, stack_top);

    /* 不会到达这里 */
    return 0;
}

/* ============================================================
 * sys_wait / sys_waitpid - 等待子进程退出
 * ============================================================ */
uint32_t sys_wait(int *status) {
    return sys_waitpid(0, status, 0);
}

uint32_t sys_waitpid(uint32_t pid, int *status, int options) {
    (void)options;
    thread_t *cur = sched_get_current();
    if (!cur) return (uint32_t)-1;

    /* 查找子进程 */
    for (int i = 0; i < MAX_PROCS; i++) {
        process_t *p = &proc_table[i];
        if (p->state == PROC_DEAD) continue;
        if (p->ppid != cur->pid) continue;

        if (pid != 0 && p->pid != pid) continue;

        /* 找到僵尸子进程 */
        if (p->state == PROC_ZOMBIE) {
            if (status) *status = (int)p->exit_code;
            uint32_t cpid = p->pid;
            proc_free(p);
            return cpid;
        }
    }

    /* 没有僵尸子进程,阻塞等待 */
    /* TODO: 实现等待队列,目前简单返回 0 */
    return 0;
}

uint32_t sys_getppid(void) {
    thread_t *cur = sched_get_current();
    if (!cur) return 0;
    process_t *p = proc_find(cur->pid);
    if (!p) return 0;
    return p->ppid;
}
