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
extern uint32_t alloc_user_stack_slot(uint32_t slot);
extern void switch_to_user_asm(uint32_t eip, uint32_t esp);

/* ---- 进程表 ---- */
static process_t proc_table[MAX_PROCS];
static uint32_t next_pid = 2;  /* PID 0 = idle, PID 1 = init/reaper */

#define PROC_ARG_MAX     8
#define PROC_ENV_MAX     8
#define PROC_ARG_STR_MAX 64
#define PROC_ENV_STR_MAX 96

typedef struct user_spawn_args {
    char path[128];
    int argc;
    int envc;
    char argv[PROC_ARG_MAX][PROC_ARG_STR_MAX];
    char envp[PROC_ENV_MAX][PROC_ENV_STR_MAX];
} user_spawn_args_t;

static void user_process_trampoline(void *arg);
static void user_thread_trampoline(void *arg);
static void free_thread_kernel_resources(thread_t *t);
static int copy_exec_args(user_spawn_args_t *args, const char *path,
                          char *const argv[], char *const envp[]);
static uint32_t setup_user_args_stack(uint32_t stack_top, const user_spawn_args_t *args);
static void proc_free_cloned_address_space(uint32_t *pgd);

#define WAITPID_SUPPORTED_OPTIONS WAITPID_WNOHANG

static int copy_string_vector(char *dst, int max_count, int slot_size,
                              char *const srcv[], int *out_count)
{
    int count = 0;

    if (!dst || !out_count || slot_size <= 0) return -1;

    if (srcv) {
        for (; count < max_count; count++) {
            const char *src = srcv[count];
            char *slot = dst + count * slot_size;
            if (!src) break;

            int j = 0;
            for (; j < slot_size - 1 && src[j]; j++)
                slot[j] = src[j];
            slot[j] = '\0';

            if (src[j] != '\0')
                return -1;
        }

        if (count == max_count && srcv[count])
            return -1;
    }

    *out_count = count;
    return 0;
}

static int copy_exec_args(user_spawn_args_t *args, const char *path,
                          char *const argv[], char *const envp[]) {
    int argc = 0;
    int envc = 0;

    if (!args || !path || !path[0]) return -1;

    memset(args, 0, sizeof(*args));

    int i = 0;
    for (; i < 127 && path[i]; i++)
        args->path[i] = path[i];
    args->path[i] = '\0';

    if (copy_string_vector((char *)args->argv, PROC_ARG_MAX, PROC_ARG_STR_MAX, argv, &argc) != 0)
        return -1;
    if (copy_string_vector((char *)args->envp, PROC_ENV_MAX, PROC_ENV_STR_MAX, envp, &envc) != 0)
        return -1;

    if (argc == 0) {
        int j = 0;
        for (; j < PROC_ARG_STR_MAX - 1 && path[j]; j++)
            args->argv[0][j] = path[j];
        args->argv[0][j] = '\0';
        argc = 1;
    }

    args->argc = argc;
    args->envc = envc;
    return 0;
}

static uint32_t setup_user_args_stack(uint32_t stack_top, const user_spawn_args_t *args) {
    uint32_t sp = stack_top;
    uint32_t argv_ptrs[PROC_ARG_MAX + 1];
    uint32_t envp_ptrs[PROC_ENV_MAX + 1];
    int argc;
    int envc;

    if (!args) return 0;
    argc = args->argc;
    envc = args->envc;
    if (argc < 0 || argc > PROC_ARG_MAX) return 0;
    if (envc < 0 || envc > PROC_ENV_MAX) return 0;

    for (int i = envc - 1; i >= 0; i--) {
        uint32_t len = (uint32_t)strlen(args->envp[i]) + 1u;
        sp -= len;
        memcpy((void *)sp, args->envp[i], len);
        envp_ptrs[i] = sp;
    }
    envp_ptrs[envc] = 0;

    for (int i = argc - 1; i >= 0; i--) {
        uint32_t len = (uint32_t)strlen(args->argv[i]) + 1u;
        sp -= len;
        memcpy((void *)sp, args->argv[i], len);
        argv_ptrs[i] = sp;
    }
    argv_ptrs[argc] = 0;

    sp &= ~0x3u;

    sp -= sizeof(uint32_t) * (uint32_t)(envc + 1);
    memcpy((void *)sp, envp_ptrs, sizeof(uint32_t) * (uint32_t)(envc + 1));
    uint32_t user_envp = sp;

    sp -= sizeof(uint32_t) * (uint32_t)(argc + 1);
    memcpy((void *)sp, argv_ptrs, sizeof(uint32_t) * (uint32_t)(argc + 1));
    uint32_t user_argv = sp;

    sp -= sizeof(uint32_t);
    *(uint32_t *)sp = user_envp;

    sp -= sizeof(uint32_t);
    *(uint32_t *)sp = user_argv;

    sp -= sizeof(uint32_t);
    *(uint32_t *)sp = (uint32_t)argc;

    sp -= sizeof(uint32_t);
    *(uint32_t *)sp = 0;

    return sp;
}

static void proc_free_cloned_address_space(uint32_t *pgd) {
    if (!pgd) return;

    for (uint32_t pdi = 0; pdi < 1023; pdi++) {
        if ((pgd[pdi] & PTE_PRESENT) == 0) continue;
        if ((pgd[pdi] & PTE_USER) == 0) continue;

        uint32_t *pt = (uint32_t *)(pgd[pdi] & PAGE_MASK);
        for (uint32_t pti = 0; pti < 1024; pti++) {
            if ((pt[pti] & PTE_PRESENT) == 0) continue;
            if ((pt[pti] & PTE_USER) == 0) continue;
            pmm_free_page((void *)(pt[pti] & PAGE_MASK));
            pt[pti] = 0;
        }

        pmm_free_page(pt);
        pgd[pdi] = 0;
    }

    pmm_free_page(pgd);
}

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

    /* Reserve PID 1 as the long-lived init/reaper process. Kernel UI
     * threads are also created with PID 1, so this PCB anchors orphan
     * reparenting and prevents children from being left attached to a
     * dead parent. */
    process_t *init = &proc_table[0];
    init->pid = INIT_PID;
    init->state = PROC_RUNNING;
    init->ppid = 0;
    init->threads = NULL;
    init->thread_count = 0;
    init->cr3 = 0;
    init->exit_code = 0;
    init->code_addr = 0;
    init->code_size = 0;
    init->heap_start = 0;
    init->heap_end = 0;
    init->mmap_base = 0;
    init->mmap_end = 0;
    for (int i = 0; i < 31; i++) init->name[i] = 0;
    init->name[0] = 'i';
    init->name[1] = 'n';
    init->name[2] = 'i';
    init->name[3] = 't';
    init->name[4] = '\0';
    for (int i = 0; i < MAX_FD; i++) {
        init->fds[i] = NULL;
        init->fd_flags[i] = 0;
    }
    init->cwd[0] = '/';
    init->cwd[1] = '\0';
    init->uid = 0;
    init->gid = 0;
    init->pending_signals = 0;
    init->total_ticks = 0;
    next_pid = INIT_PID + 1;
}

/* 分配空闲 PCB */
process_t *proc_alloc(void) {
    /* Opportunistically let init/reaper collect orphan zombies before
     * allocating a new PCB. This keeps orphan cleanup automatic even when
     * PID 1 is represented by kernel UI threads rather than a user init. */
    proc_reap_zombies_for_parent(INIT_PID);

    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state == PROC_DEAD) {
            process_t *p = &proc_table[i];
            p->pid = next_pid++;
            p->state = PROC_READY;
            p->ppid = 0;
            p->threads = NULL;
            p->thread_count = 0;
            p->cr3 = 0;
            p->owns_address_space = 0;
            p->exit_code = 0;
            p->code_addr = 0;
            p->code_size = 0;
            p->heap_start = 0;
            p->heap_end = 0;
            p->mmap_base = 0;
            p->mmap_end = 0;
            for (int j = 0; j < 31; j++) p->name[j] = 0;
            p->name[0] = '\0';
            /* 初始化文件描述符表 */
            for (int j = 0; j < MAX_FD; j++) {
                p->fds[j] = NULL;
                p->fd_flags[j] = 0;
            }
            /* 初始化当前工作目录为 / */
            p->cwd[0] = '/';
            p->cwd[1] = '\0';
            p->uid = 0;
            p->gid = 0;
            p->pending_signals = 0;
            p->alarm_deadline_ms = 0;
            p->alarm_active = 0;
            return p;
        }
    }
    return NULL;
}

void proc_free(process_t *proc) {
    if (!proc) return;
    vfs_close_fds_for_process(proc);
    proc->pid = 0;
    proc->state = PROC_DEAD;
    proc->ppid = 0;
    proc->threads = NULL;
    proc->thread_count = 0;
    proc->cr3 = 0;
    proc->owns_address_space = 0;
    proc->code_addr = 0;
    proc->code_size = 0;
    proc->heap_start = 0;
    proc->heap_end = 0;
    proc->mmap_base = 0;
    proc->mmap_end = 0;
    proc->exit_code = 0;
    proc->uid = 0;
    proc->gid = 0;
    proc->pending_signals = 0;
    proc->alarm_deadline_ms = 0;
    proc->alarm_active = 0;
    proc->next_user_stack_slot = 0;
    for (int i = 0; i < MAX_FD; i++) {
        proc->fds[i] = NULL;
        proc->fd_flags[i] = 0;
    }
}

static void free_thread_kernel_resources(thread_t *t) {
    if (!t) return;

    sched_remove_thread(t);

    if (t->kernel_stack && t->kernel_stack_top > t->kernel_stack) {
        uint32_t stack_size = t->kernel_stack_top - t->kernel_stack;
        uint32_t stack_pages = (stack_size + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint32_t page = 0; page < stack_pages; page++) {
            pmm_free_page((void *)(t->kernel_stack + page * PAGE_SIZE));
        }
        t->kernel_stack = 0;
        t->kernel_stack_top = 0;
    }

    pmm_free_page(t);
}

void proc_reap_zombie(process_t *proc) {
    if (!proc || proc->state != PROC_ZOMBIE) return;

    if (proc->owns_address_space && proc->cr3) {
        sched_ensure_not_running_cr3(proc->cr3);
        proc_free_cloned_address_space((uint32_t *)proc->cr3);
        proc->cr3 = 0;
        proc->owns_address_space = 0;
    }

    thread_t *t = proc->threads;
    while (t) {
        thread_t *next = t->proc_next;
        free_thread_kernel_resources(t);
        t = next;
    }

    proc_free(proc);
}

static uint32_t proc_reap_zombies_for_parent_except(uint32_t ppid, uint32_t except_pid) {
    uint32_t count = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        process_t *p = &proc_table[i];
        if (p->state == PROC_ZOMBIE && p->ppid == ppid && p->pid != except_pid) {
            proc_reap_zombie(p);
            count++;
        }
    }
    return count;
}

uint32_t proc_reap_zombies_for_parent(uint32_t ppid) {
    return proc_reap_zombies_for_parent_except(ppid, 0);
}

uint32_t proc_reparent_children(uint32_t old_ppid, uint32_t new_ppid) {
    if (old_ppid == 0 || old_ppid == new_ppid) return 0;
    if (!proc_find(new_ppid)) return 0;

    uint32_t count = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        process_t *child = &proc_table[i];
        if (child->state != PROC_DEAD && child->pid != 0 && child->ppid == old_ppid) {
            child->ppid = new_ppid;
            count++;
        }
    }
    return count;
}

static void proc_wake_waiter(uint32_t pid)
{
    process_t *parent = proc_find(pid);
    if (!parent || !parent->threads)
        return;

    thread_wake(parent->threads);
}

void proc_wake_sleepers(uint32_t now_ms)
{
    for (int i = 0; i < MAX_PROCS; i++) {
        process_t *p = &proc_table[i];
        if (p->state == PROC_DEAD || p->state == PROC_ZOMBIE || !p->threads)
            continue;

        thread_t *t = p->threads;
        if (t->state == PROC_SLEEPING && t->wake_time <= now_ms)
            thread_wake(t);
    }
}

int proc_set_alarm(uint32_t pid, uint32_t seconds, uint32_t now_ms)
{
    process_t *p = proc_find(pid);
    uint32_t remaining = 0;

    if (!p || p->state == PROC_DEAD || p->state == PROC_ZOMBIE)
        return -1;

    if (p->alarm_active && p->alarm_deadline_ms > now_ms)
        remaining = (p->alarm_deadline_ms - now_ms + 999u) / 1000u;

    if (seconds == 0) {
        p->alarm_active = 0;
        p->alarm_deadline_ms = 0;
    } else {
        p->alarm_active = 1;
        p->alarm_deadline_ms = now_ms + seconds * 1000u;
    }

    return (int)remaining;
}

void proc_check_alarms(uint32_t now_ms)
{
    for (int i = 0; i < MAX_PROCS; i++) {
        process_t *p = &proc_table[i];
        if (!p->alarm_active || p->pid == 0 || p->state == PROC_DEAD || p->state == PROC_ZOMBIE)
            continue;
        if (p->alarm_deadline_ms <= now_ms) {
            p->alarm_active = 0;
            p->alarm_deadline_ms = 0;
            proc_send_signal(p->pid, OPENOS_SIGALRM);
        }
    }
}

void proc_mark_exit(uint32_t pid, int code) {
    if (pid == INIT_PID) return;

    process_t *proc = proc_find(pid);
    if (!proc) return;

    uint32_t parent_pid = proc->ppid;
    proc_reparent_children(pid, INIT_PID);

    proc->exit_code = (uint32_t)code;
    proc->state = PROC_ZOMBIE;
    for (thread_t *t = proc->threads; t; t = t->proc_next)
        t->state = PROC_ZOMBIE;

    proc_wake_waiter(parent_pid);

    /* Reap previously exited orphan children owned by init, but never reap
     * this process here: sys_exit() is still running on its kernel stack until
     * the scheduler switches away. */
    proc_reap_zombies_for_parent_except(INIT_PID, pid);
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

uint32_t proc_current_pid(void) {
    thread_t *cur = sched_get_current();
    return cur ? cur->pid : 0;
}

uint32_t proc_current_uid(void) {
    thread_t *cur = sched_get_current();
    process_t *p = cur ? proc_find(cur->pid) : NULL;
    return p ? p->uid : 0;
}

uint32_t proc_current_gid(void) {
    thread_t *cur = sched_get_current();
    process_t *p = cur ? proc_find(cur->pid) : NULL;
    return p ? p->gid : 0;
}

int proc_set_current_uid(uint32_t uid) {
    thread_t *cur = sched_get_current();
    process_t *p = cur ? proc_find(cur->pid) : NULL;
    if (!p) return -1;
    if (p->uid != 0 && p->uid != uid) return -1;
    p->uid = uid;
    return 0;
}

int proc_set_current_gid(uint32_t gid) {
    thread_t *cur = sched_get_current();
    process_t *p = cur ? proc_find(cur->pid) : NULL;
    if (!p) return -1;
    if (p->uid != 0 && p->gid != gid) return -1;
    p->gid = gid;
    return 0;
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
    for (int i = 0; i < 1024; i++) {
        child_pgd[i] = 0;
    }

    /*
     * Clone user pages only. Kernel/non-user mappings are shared so the child
     * can still run kernel code after CR3 switch without duplicating kernel
     * identity mappings. Recursive PDE points to the child page directory.
     */
    for (int i = 0; i < 1023; i++) {
        if ((parent_pgd[i] & PTE_PRESENT) == 0) {
            child_pgd[i] = 0;
            continue;
        }

        if ((parent_pgd[i] & PTE_USER) == 0) {
            child_pgd[i] = parent_pgd[i];
            continue;
        }

        uint32_t *parent_pt = (uint32_t *)(parent_pgd[i] & PAGE_MASK);
        uint32_t pt_phys = (uint32_t)pmm_alloc_page();
        if (!pt_phys) {
            proc_free_cloned_address_space(child_pgd);
            proc_free(child);
            return (uint32_t)-1;
        }

        uint32_t *child_pt = (uint32_t *)pt_phys;
        for (int j = 0; j < 1024; j++) {
            child_pt[j] = 0;
        }
        child_pgd[i] = pt_phys | (parent_pgd[i] & 0xFFF);

        for (int j = 0; j < 1024; j++) {
            if ((parent_pt[j] & PTE_PRESENT) == 0) {
                child_pt[j] = 0;
                continue;
            }

            if ((parent_pt[j] & PTE_USER) == 0) {
                child_pt[j] = parent_pt[j];
                continue;
            }

            uint32_t page_phys = parent_pt[j] & PAGE_MASK;
            uint32_t flags = parent_pt[j] & 0xFFFu;

            if (flags & PTE_RW) {
                flags &= ~PTE_RW;
                flags |= PTE_COW;
                parent_pt[j] = page_phys | flags;
            }

            child_pt[j] = page_phys | flags;
            pmm_ref_page((void *)page_phys);
        }
    }
    child_pgd[1023] = child_pd_phys | (parent_pgd[1023] & 0xFFF);
    __asm__ volatile ("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax", "memory");

    child->cr3 = child_pd_phys;
    child->owns_address_space = 1;
    child->uid = parent->uid;
    child->gid = parent->gid;

    /* 创建子线程,复制父线程的栈帧 */
    void *child_stack_page = pmm_alloc_page();
    if (!child_stack_page) {
        proc_free_cloned_address_space(child_pgd);
        proc_free(child);
        return (uint32_t)-1;
    }
    uint32_t child_stack = (uint32_t)child_stack_page + 4096;

    /* 复制父线程的内核栈 */
    uint8_t *src_stack = (uint8_t *)cur->kernel_stack;
    uint8_t *dst_stack = (uint8_t *)(child_stack - 4096);
    for (int i = 0; i < 4096; i++)
        dst_stack[i] = src_stack[i];

    thread_t *child_thread = thread_create(child->pid, child->name,
                                           cur->kernel_eip, child_stack);
    if (!child_thread) {
        pmm_free_page(child_stack_page);
        proc_free_cloned_address_space(child_pgd);
        proc_free(child);
        return (uint32_t)-1;
    }

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
    child->code_addr = parent->code_addr;
    child->heap_start = parent->heap_start;
    child->heap_end = parent->heap_end;
    child->mmap_base = parent->mmap_base;
    child->mmap_end = parent->mmap_end;
    child->owns_address_space = 1;
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
int sys_exec_env(const char *path, char *const argv[], char *const envp[]) {
    user_spawn_args_t exec_args;
    if (!path || !path[0]) return -1;
    if (copy_exec_args(&exec_args, path, argv, envp) != 0) return -1;

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

    /* Open the executable before switching address spaces so ordinary errors
     * leave the current process image untouched, as exec should. */
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

    uint32_t old_cr3 = proc->cr3 ? proc->cr3 : vmm_get_cr3();
    uint32_t old_owned = proc->owns_address_space;
    uint32_t new_cr3 = vmm_create_user_address_space();
    if (!new_cr3) {
        vfs_close(fd);
        return -1;
    }

    vmm_load_cr3(new_cr3);

    elf_load_result_t load_result = elf_load(fd);
    vfs_close(fd);

    if (load_result.entry == 0 || load_result.num_segments <= 0 || load_result.brk_start == 0) {
        serial_write("[EXEC] ELF load failed\n");
        vmm_load_cr3(old_cr3);
        proc_free_cloned_address_space((uint32_t *)new_cr3);
        return -1;
    }

    uint32_t stack_top = alloc_user_stack();
    if (!stack_top) {
        serial_write("[EXEC] user stack allocation failed\n");
        vmm_load_cr3(old_cr3);
        proc_free_cloned_address_space((uint32_t *)new_cr3);
        return -1;
    }

    uint32_t user_sp = setup_user_args_stack(stack_top, &exec_args);
    if (!user_sp) {
        serial_write("[EXEC] argv stack setup failed\n");
        vmm_load_cr3(old_cr3);
        proc_free_cloned_address_space((uint32_t *)new_cr3);
        return -1;
    }

    /* Commit: from here the new image is current. Keep the existing process,
     * pid, parent relationship, cwd/fd table, and kernel stack; replace only
     * the user address space and executable metadata. */
    proc->cr3 = new_cr3;
    proc->owns_address_space = 1;

    proc->code_addr = load_result.brk_start;  /* actually brk start */
    proc->heap_start = load_result.brk_start;
    proc->heap_end = load_result.brk_start;
    proc->mmap_base = 0x50000000u;
    proc->mmap_end = proc->mmap_base;

    int name_i = 0;
    for (; name_i < 31 && path[name_i]; name_i++)
        proc->name[name_i] = path[name_i];
    proc->name[name_i] = '\0';

    for (int i = 0; i < MAX_FD; i++) {
        if ((proc->fd_flags[i] & FD_CLOEXEC) && proc->fds[i]) {
            vfs_close_fd_for_process(proc, i);
        }
    }

    if (old_owned && old_cr3 && old_cr3 != new_cr3) {
        proc_free_cloned_address_space((uint32_t *)old_cr3);
    }

    serial_write("[EXEC] Replaced image, entry=0x");
    serial_write_hex(load_result.entry);
    serial_write("\n");

    /* CPU switches to TSS.esp0 on every ring3 -> ring0 interrupt. */
    tss_set_kernel_stack(cur->kernel_stack_top);
    switch_to_user_asm(load_result.entry, user_sp);

    /* 不会到达这里 */
    return 0;
}

int sys_exec(const char *path, char *const argv[]) {
    return sys_exec_env(path, argv, NULL);
}

int spawn_user_process_env(const char *path, char *const argv[], char *const envp[]) {
    if (!path || !path[0]) return -1;

    /* Fail early when the executable path is invalid. Without this check,
     * spawn would return a child pid even though the trampoline will fail
     * exec immediately, which makes user-space error handling ambiguous. */
    int fd = vfs_open(path, 0, 0);
    if (fd < 0) return -1;
    vfs_close(fd);

    process_t *child = proc_alloc();
    if (!child) return -1;

    thread_t *cur = sched_get_current();
    child->ppid = cur ? cur->pid : 0;
    child->cr3 = vmm_get_cr3();

    process_t *parent = cur ? proc_find(cur->pid) : NULL;
    if (parent)
        vfs_clone_fds_for_process(child, parent);

    int i = 0;
    for (; i < 31 && path[i]; i++)
        child->name[i] = path[i];
    child->name[i] = '\0';

    user_spawn_args_t *args = (user_spawn_args_t *)pmm_alloc_page();
    if (!args) {
        proc_free(child);
        return -1;
    }

    if (copy_exec_args(args, path, argv, envp) != 0) {
        pmm_free_page(args);
        proc_free(child);
        return -1;
    }

    uint32_t child_cr3 = vmm_create_user_address_space();
    if (!child_cr3) {
        pmm_free_page(args);
        proc_free(child);
        return -1;
    }
    child->cr3 = child_cr3;
    child->owns_address_space = 1;

    uint32_t stack_pages = PROC_KERNEL_STACK_SIZE / PAGE_SIZE;
    if (stack_pages == 0) stack_pages = 1;
    void *kernel_stack = pmm_alloc_pages(stack_pages);
    if (!kernel_stack) {
        pmm_free_page(args);
        proc_free_cloned_address_space((uint32_t *)child_cr3);
        child->cr3 = 0;
        child->owns_address_space = 0;
        proc_free(child);
        return -1;
    }

    /* thread_create_sized() cannot pass a C argument to entry directly.
     * Temporarily store spawn args in an unused PCB field; the trampoline
     * retrieves it through the current pid after the scheduler starts it. */
    child->code_addr = (uint32_t)args;

    thread_t *thread = thread_create_sized(child->pid, child->name,
                                           (uint32_t)user_process_trampoline,
                                           (uint32_t)kernel_stack + PROC_KERNEL_STACK_SIZE,
                                           PROC_KERNEL_STACK_SIZE);
    if (!thread) {
        pmm_free_page(args);
        pmm_free_page(kernel_stack);
        proc_free_cloned_address_space((uint32_t *)child_cr3);
        child->cr3 = 0;
        child->owns_address_space = 0;
        proc_free(child);
        return -1;
    }

    thread->proc_next = NULL;
    child->threads = thread;
    child->thread_count = 1;
    child->state = PROC_READY;
    sched_add_thread(thread);

    serial_write("[SPAWN] user process pid=");
    serial_write_hex(child->pid);
    serial_write(" path=");
    serial_write(args->path);
    serial_write("\n");

    return (int)child->pid;
}

int spawn_user_process(const char *path, char *const argv[]) {
    return spawn_user_process_env(path, argv, NULL);
}

static void user_process_trampoline(void *arg) {
    (void)arg;
    thread_t *cur = sched_get_current();
    process_t *proc = cur ? proc_find(cur->pid) : NULL;
    user_spawn_args_t *args = proc ? (user_spawn_args_t *)proc->code_addr : NULL;
    if (!args) {
        sys_exit(-1);
    }

    user_spawn_args_t exec_args;
    memcpy(&exec_args, args, sizeof(exec_args));
    if (proc) proc->code_addr = 0;
    pmm_free_page(args);

    char *argv[PROC_ARG_MAX + 1];
    char *envp[PROC_ENV_MAX + 1];
    for (int i = 0; i < exec_args.argc && i < PROC_ARG_MAX; i++)
        argv[i] = exec_args.argv[i];
    argv[exec_args.argc] = NULL;
    for (int i = 0; i < exec_args.envc && i < PROC_ENV_MAX; i++)
        envp[i] = exec_args.envp[i];
    envp[exec_args.envc] = NULL;

    int ret = sys_exec_env(exec_args.path, argv, envp);
    if (ret < 0) {
        serial_write("[SPAWN] exec failed: ");
        serial_write(exec_args.path);
        serial_write("\n");
        sys_exit(-1);
    }

    sys_exit(0);
}

static void user_thread_trampoline(void *arg) {
    (void)arg;
    thread_t *cur = sched_get_current();
    if (!cur || !cur->is_user_thread || !cur->user_entry || !cur->user_stack_top) {
        sys_thread_exit(-1);
    }

    tss_set_kernel_stack(cur->kernel_stack_top);
    switch_to_user_asm(cur->user_entry, cur->user_stack_top);
    sys_thread_exit(0);
}

int sys_thread_create(uint32_t entry, uint32_t arg, uint32_t return_entry) {
    thread_t *cur = sched_get_current();
    if (!cur || !entry) return -1;

    process_t *proc = proc_find(cur->pid);
    if (!proc || proc->state == PROC_DEAD || proc->state == PROC_ZOMBIE)
        return -1;
    if (proc->thread_count >= MAX_THREADS)
        return -1;

    uint32_t slot = ++proc->next_user_stack_slot;
    uint32_t user_stack_top = alloc_user_stack_slot(slot);
    if (!user_stack_top)
        return -1;

    uint32_t *usp = (uint32_t *)user_stack_top;
    *(--usp) = arg;
    *(--usp) = return_entry;
    user_stack_top = (uint32_t)usp;

    uint32_t stack_pages = PROC_KERNEL_STACK_SIZE / PAGE_SIZE;
    if (stack_pages == 0) stack_pages = 1;
    void *kernel_stack = pmm_alloc_pages(stack_pages);
    if (!kernel_stack) {
        free_user_stack_slot(slot);
        return -1;
    }

    thread_t *thread = thread_create_sized(proc->pid, proc->name,
                                           (uint32_t)user_thread_trampoline,
                                           (uint32_t)kernel_stack + PROC_KERNEL_STACK_SIZE,
                                           PROC_KERNEL_STACK_SIZE);
    if (!thread) {
        for (uint32_t page = 0; page < stack_pages; page++)
            pmm_free_page((void *)((uint32_t)kernel_stack + page * PAGE_SIZE));
        free_user_stack_slot(slot);
        return -1;
    }

    thread->user_entry = entry;
    thread->user_arg = arg;
    thread->user_stack_top = user_stack_top;
    thread->user_stack_slot = slot;
    thread->user_return_entry = return_entry;
    thread->is_user_thread = 1;
    thread->proc_next = proc->threads;
    proc->threads = thread;
    proc->thread_count++;

    sched_add_thread(thread);
    return (int)thread->id;
}

void sys_thread_exit(int code) {
    thread_t *cur = sched_get_current();
    if (!cur) return;

    process_t *proc = proc_find(cur->pid);
    if (!proc) {
        cur->state = PROC_ZOMBIE;
        sched_yield();
        for (;;) { __asm__ volatile("pause"); }
    }

    (void)code;
    cur->state = PROC_ZOMBIE;
    sched_remove_thread(cur);

    if (cur->is_user_thread && cur->user_stack_slot != 0) {
        free_user_stack_slot(cur->user_stack_slot);
        cur->user_stack_slot = 0;
        cur->user_stack_top = 0;
    }

    if (proc->thread_count > 0)
        proc->thread_count--;

    if (proc->thread_count == 0) {
        proc_mark_exit(proc->pid, code);
    }

    sched_yield();
    for (;;) { __asm__ volatile("pause"); }
}

/* ============================================================
 * sys_wait / sys_waitpid - 等待子进程退出
 * ============================================================ */
uint32_t sys_wait(int *status) {
    return sys_waitpid(-1, status, 0);
}

uint32_t sys_waitpid(int pid, int *status, int options) {
    thread_t *cur = sched_get_current();
    if (!cur) return (uint32_t)-1;
    if (options & ~WAITPID_SUPPORTED_OPTIONS) return (uint32_t)-1;
    if (pid < -1) return (uint32_t)-1;

    if (pid > 0) {
        process_t *target = proc_find((uint32_t)pid);
        if (!target || target->ppid != cur->pid) {
            return (uint32_t)-1;
        }
    }

    for (;;) {
        int has_child = 0;

        for (int i = 0; i < MAX_PROCS; i++) {
            process_t *p = &proc_table[i];
            if (p->state == PROC_DEAD) continue;
            if (p->ppid != cur->pid) continue;
            if (pid > 0 && p->pid != (uint32_t)pid) continue;

            has_child = 1;

            if (p->state == PROC_ZOMBIE) {
                if (status) *status = ((int)p->exit_code & 0xff) << 8;
                uint32_t cpid = p->pid;
                proc_reap_zombie(p);
                return cpid;
            }
        }

        if (!has_child)
            return (uint32_t)-1;

        if (options & WAITPID_WNOHANG)
            return 0;

        cur->state = PROC_BLOCKED;
        sched_yield();
    }
}

uint32_t sys_getppid(void) {
    thread_t *cur = sched_get_current();
    if (!cur) return 0;
    process_t *p = proc_find(cur->pid);
    if (!p) return 0;
    return p->ppid;
}

int proc_terminate(uint32_t pid, int exit_code)
{
    process_t *p = proc_find(pid);
    if (!p || p->state == PROC_DEAD || p->state == PROC_ZOMBIE)
        return -1;

    if (pid == INIT_PID)
        return -1;

    thread_t *t = p->threads;
    while (t) {
        thread_t *next = t->proc_next;
        sched_remove_thread(t);
        t->state = PROC_ZOMBIE;
        t = next;
    }

    p->exit_code = exit_code;
    vfs_close_fds_for_process(p);
    proc_reparent_children(pid, INIT_PID);
    p->state = PROC_ZOMBIE;
    proc_wake_waiter(p->ppid);
    return 0;
}

int proc_send_signal(uint32_t pid, int sig)
{
    process_t *p;

    if (pid == 0 || sig < 0 || sig > OPENOS_SIGNAL_MAX)
        return -1;

    p = proc_find(pid);
    if (!p || p->state == PROC_DEAD || p->state == PROC_ZOMBIE)
        return -1;

    if (sig == 0)
        return 0;
    if (sig != OPENOS_SIGKILL && sig != OPENOS_SIGTERM && sig != OPENOS_SIGALRM)
        return -1;

    p->pending_signals |= (1u << sig);
    if (p->threads && (p->threads->state == PROC_BLOCKED || p->threads->state == PROC_SLEEPING))
        thread_wake(p->threads);

    if (sig == OPENOS_SIGKILL)
        return proc_handle_pending_signals(pid);
    return 0;
}

int proc_handle_pending_signals(uint32_t pid)
{
    process_t *p = proc_find(pid);
    int sig = 0;

    if (!p || p->state == PROC_DEAD || p->state == PROC_ZOMBIE)
        return 0;
    if (pid == INIT_PID)
        return 0;

    if (p->pending_signals & (1u << OPENOS_SIGKILL))
        sig = OPENOS_SIGKILL;
    else if (p->pending_signals & (1u << OPENOS_SIGTERM))
        sig = OPENOS_SIGTERM;
    else if (p->pending_signals & (1u << OPENOS_SIGALRM))
        sig = OPENOS_SIGALRM;

    if (!sig)
        return 0;

    p->pending_signals &= ~(1u << sig);
    proc_terminate(pid, 128 + sig);
    return sig;
}

int sys_kill(int pid, int sig)
{
    if (pid <= 0)
        return -1;

    return proc_send_signal((uint32_t)pid, sig);
}


int sys_alarm(unsigned int seconds)
{
    uint32_t pid = proc_current_pid();
    if (!pid)
        return -1;
    return proc_set_alarm(pid, seconds, sched_time_ms());
}
