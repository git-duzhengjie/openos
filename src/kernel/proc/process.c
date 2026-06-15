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
static int copy_exec_args(user_spawn_args_t *args, const char *path,
                          char *const argv[], char *const envp[]);
static uint32_t setup_user_args_stack(uint32_t stack_top, const user_spawn_args_t *args);

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
            p->exit_code = 0;
            p->code_addr = 0;
            p->code_size = 0;
            p->heap_start = 0;
            p->heap_end = 0;
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
    proc->code_addr = 0;
    proc->code_size = 0;
    proc->heap_start = 0;
    proc->heap_end = 0;
    proc->exit_code = 0;
    for (int i = 0; i < MAX_FD; i++) {
        proc->fds[i] = NULL;
        proc->fd_flags[i] = 0;
    }
}

void proc_reap_zombie(process_t *proc) {
    if (!proc || proc->state != PROC_ZOMBIE) return;

    /* Current process model uses one main thread per user process.
     * thread_t.next/prev are scheduler queue links, not a private process
     * thread list, so never iterate through t->next here. */
    thread_t *t = proc->threads;
    if (t) {
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

void proc_mark_exit(uint32_t pid, int code) {
    if (pid == INIT_PID) return;

    process_t *proc = proc_find(pid);
    if (!proc) return;

    uint32_t parent_pid = proc->ppid;
    proc_reparent_children(pid, INIT_PID);

    proc->exit_code = (uint32_t)code;
    proc->state = PROC_ZOMBIE;
    if (proc->threads)
        proc->threads->state = PROC_ZOMBIE;

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

    if (load_result.entry == 0 || load_result.num_segments <= 0 || load_result.brk_start == 0) {
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
    if (!stack_top) {
        serial_write("[EXEC] user stack allocation failed\n");
        return -1;
    }

    uint32_t user_sp = setup_user_args_stack(stack_top, &exec_args);
    if (!user_sp) {
        serial_write("[EXEC] argv stack setup failed\n");
        return -1;
    }

    /* CPU switches to TSS.esp0 on every ring3 -> ring0 interrupt.
     * Keep it pointed at this thread's kernel stack before entering user mode;
     * otherwise int 0x80/IRQ from ring3 may corrupt another stack or fault. */
    tss_set_kernel_stack(cur->kernel_stack_top);

    /* 切换到用户态执行 */
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

    uint32_t stack_pages = PROC_KERNEL_STACK_SIZE / PAGE_SIZE;
    if (stack_pages == 0) stack_pages = 1;
    void *kernel_stack = pmm_alloc_pages(stack_pages);
    if (!kernel_stack) {
        pmm_free_page(args);
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
        proc_free(child);
        return -1;
    }

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

    thread_t *t = p->threads;
    while (t) {
        thread_t *next = t->next;
        sched_remove_thread(t);
        t->state = PROC_ZOMBIE;
        t = next;
    }

    p->exit_code = exit_code;
    vfs_close_fds_for_process(p);
    p->state = PROC_ZOMBIE;
    proc_wake_waiter(p->ppid);
    return 0;
}
