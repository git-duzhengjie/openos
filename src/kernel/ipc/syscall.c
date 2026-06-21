/* ============================================================
 * openos - 系统调用实现
 * ============================================================ */

#include "../include/syscall.h"
#include "../include/process.h"
#include "../include/pmm.h"
#include "../include/serial.h"
#include "../include/vga.h"
#include "../include/gui.h"
#include "../include/gui_user.h"
#include "../include/font.h"
#include "../include/heap.h"
#include "../include/input_buffer.h"
#include "../include/usermem.h"
#include "../include/vmm.h"
#include "../include/aslr.h"
#include "../proc/process.h"
#include "../fs/vfs.h"
#include "../include/string.h"
#include "../include/blockdev.h"
#include "../include/ai.h"
#include "../net/socket.h"
#include "../net/net.h"
#include "../net/dns.h"
#include "../net/dhcp.h"
#include <stddef.h>  /* NULL */

/* VGA */
#define VGA ((volatile uint16_t *)0xB8000)

#define SYSCALL_IO_CHUNK 256u
#define SYSCALL_SEND_MAX 1472u
#define SYSCALL_RECV_MAX 1472u

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define OPENOS_USER_NAME_MAX 32u
#define OPENOS_USER_HOME_MAX 64u
#define OPENOS_USER_SHELL_MAX 64u
#define OPENOS_F_GETFL 3
#define OPENOS_F_SETFL 4
#define OPENOS_FCNTL_SETFL_MASK O_NONBLOCK
#define SYSCALL_CLIPBOARD_MAX 512u

static char syscall_clipboard[SYSCALL_CLIPBOARD_MAX];
static uint32_t syscall_clipboard_len;

static int syscall_require_cap(uint32_t cap);

typedef struct kernel_user_record {
    uint32_t uid;
    uint32_t gid;
    const char *name;
    const char *home;
    const char *shell;
} kernel_user_record_t;

typedef struct kernel_group_record {
    uint32_t gid;
    const char *name;
} kernel_group_record_t;

static const kernel_user_record_t g_user_table[] = {
    {0u, 0u, "root", "/root", "/bin/sh"},
    {1000u, 1000u, "user", "/home/user", "/bin/sh"},
};

static const kernel_group_record_t g_group_table[] = {
    {0u, "root"},
    {1000u, "users"},
};

static void syscall_copy_fixed_string(char *dst, uint32_t dst_len, const char *src)
{
    uint32_t i;

    if (!dst || dst_len == 0) return;
    for (i = 0; i + 1 < dst_len && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int syscall_getpwuid(uint32_t uid, openos_user_t *user)
{
    openos_user_t out;
    uint32_t i;

    if (!user) return -1;
    for (i = 0; i < sizeof(g_user_table) / sizeof(g_user_table[0]); i++) {
        if (g_user_table[i].uid != uid)
            continue;
        memset(&out, 0, sizeof(out));
        out.uid = g_user_table[i].uid;
        out.gid = g_user_table[i].gid;
        syscall_copy_fixed_string(out.name, sizeof(out.name), g_user_table[i].name);
        syscall_copy_fixed_string(out.home, sizeof(out.home), g_user_table[i].home);
        syscall_copy_fixed_string(out.shell, sizeof(out.shell), g_user_table[i].shell);
        return copy_to_user(user, &out, sizeof(out));
    }
    return -1;
}

static int syscall_getgrgid(uint32_t gid, openos_group_t *group)
{
    openos_group_t out;
    uint32_t i;

    if (!group) return -1;
    for (i = 0; i < sizeof(g_group_table) / sizeof(g_group_table[0]); i++) {
        if (g_group_table[i].gid != gid)
            continue;
        memset(&out, 0, sizeof(out));
        out.gid = g_group_table[i].gid;
        syscall_copy_fixed_string(out.name, sizeof(out.name), g_group_table[i].name);
        return copy_to_user(group, &out, sizeof(out));
    }
    return -1;
}

static int syscall_clock_gettime(uint32_t clock_id, openos_timespec_t *user_ts)
{
    openos_timespec_t ts;
    uint32_t ms;

    if (!user_ts || clock_id != OPENOS_CLOCK_MONOTONIC)
        return -1;

    ms = sched_time_ms();
    ts.tv_sec = (int64_t)(ms / 1000u);
    ts.tv_nsec = (int64_t)(ms % 1000u) * 1000000ll;
    return copy_to_user(user_ts, &ts, sizeof(ts));
}

static int syscall_nanosleep(const openos_timespec_t *user_req, openos_timespec_t *user_rem)
{
    openos_timespec_t req;
    openos_timespec_t rem;
    uint64_t sec_ms;
    uint64_t nsec_ms;
    uint64_t total_ms;

    if (!user_req)
        return -1;
    if (copy_from_user(&req, user_req, sizeof(req)) < 0)
        return -1;
    if (req.tv_sec < 0 || req.tv_nsec < 0 || req.tv_nsec >= 1000000000ll)
        return -1;

    sec_ms = (uint64_t)req.tv_sec * 1000ull;
    nsec_ms = ((uint64_t)req.tv_nsec + 999999ull) / 1000000ull;
    total_ms = sec_ms + nsec_ms;
    if (sec_ms / 1000ull != (uint64_t)req.tv_sec || total_ms < sec_ms || total_ms > 0xffffffffull)
        return -1;

    if (total_ms > 0ull)
        thread_sleep((uint32_t)total_ms);

    if (user_rem) {
        rem.tv_sec = 0;
        rem.tv_nsec = 0;
        if (copy_to_user(user_rem, &rem, sizeof(rem)) < 0)
            return -1;
    }
    return 0;
}

static int syscall_clipboard_set(const char *text)
{
    uint32_t len = 0;

    if (!text) return -1;
    while (len + 1u < SYSCALL_CLIPBOARD_MAX) {
        char ch;
        if (copy_from_user(&ch, text + len, 1) < 0)
            return -1;
        syscall_clipboard[len] = ch;
        if (ch == '\0') {
            syscall_clipboard_len = len;
            return 0;
        }
        len++;
    }
    syscall_clipboard[SYSCALL_CLIPBOARD_MAX - 1u] = '\0';
    syscall_clipboard_len = SYSCALL_CLIPBOARD_MAX - 1u;
    return 0;
}

static int syscall_clipboard_get(char *buf, uint32_t len)
{
    uint32_t copy_len;

    if (!buf || len == 0) return -1;
    copy_len = syscall_clipboard_len + 1u;
    if (copy_len > len) copy_len = len;
    if (copy_to_user(buf, syscall_clipboard, copy_len) < 0)
        return -1;
    if (copy_len == len) {
        char nul = '\0';
        if (copy_to_user(buf + len - 1u, &nul, 1) < 0)
            return -1;
    }
    return (int)syscall_clipboard_len;
}

#define SYSCALL_MAX_MUTEXES 64u
#define SYSCALL_MAX_SEMAPHORES 64u
#define SYSCALL_MAX_CONDS 64u
#define SYSCALL_MAX_FUTEX_WAITERS 128u
#define SYSCALL_MAX_MQUEUES 4u
#define MQ_MAX_MESSAGES 4u
#define MQ_MAX_MESSAGE_SIZE 64u
#define SYSCALL_MAX_SHM_SEGMENTS 8u
#define SYSCALL_MAX_EVENTFDS 32u

#define MUTEX_UNUSED 0u
#define MUTEX_READY  1u
#define SEM_UNUSED   0u
#define SEM_READY    1u
#define COND_UNUSED  0u
#define COND_READY   1u
#define FUTEX_WAIT_ACTIVE 1u
#define MQ_UNUSED 0u
#define MQ_READY  1u
#define SHM_UNUSED 0u
#define SHM_READY  1u
#define EVENTFD_UNUSED 0u
#define EVENTFD_READY  1u

#define MUTEX_WAIT_QUEUE_LIMIT 32u
#define SEM_WAIT_QUEUE_LIMIT   32u
#define COND_WAIT_QUEUE_LIMIT  32u

typedef struct syscall_mutex {
    uint8_t used;
    uint32_t owner_tid;
    thread_t *waiters[MUTEX_WAIT_QUEUE_LIMIT];
    uint32_t wait_head;
    uint32_t wait_count;
} syscall_mutex_t;

typedef struct syscall_sem {
    uint8_t used;
    uint32_t value;
    thread_t *waiters[SEM_WAIT_QUEUE_LIMIT];
    uint32_t wait_head;
    uint32_t wait_count;
} syscall_sem_t;

typedef struct syscall_cond {
    uint8_t used;
    thread_t *waiters[COND_WAIT_QUEUE_LIMIT];
    uint32_t wait_head;
    uint32_t wait_count;
} syscall_cond_t;

typedef struct syscall_futex_waiter {
    uint8_t used;
    uint32_t pid;
    uint32_t uaddr;
    thread_t *thread;
} syscall_futex_waiter_t;

typedef struct syscall_mq_message {
    uint32_t len;
    uint8_t data[MQ_MAX_MESSAGE_SIZE];
} syscall_mq_message_t;

typedef struct syscall_mq {
    uint8_t used;
    syscall_mq_message_t messages[MQ_MAX_MESSAGES];
    uint32_t head;
    uint32_t count;
} syscall_mq_t;

typedef struct syscall_shm_segment {
    uint8_t used;
    uint32_t phys;
    uint32_t refcount;
    uint32_t flags;
} syscall_shm_segment_t;

typedef struct syscall_eventfd {
    uint8_t used;
    uint32_t counter;
} syscall_eventfd_t;

static syscall_mutex_t syscall_mutexes[SYSCALL_MAX_MUTEXES];
static syscall_sem_t syscall_sems[SYSCALL_MAX_SEMAPHORES];
static syscall_cond_t syscall_conds[SYSCALL_MAX_CONDS];
static syscall_futex_waiter_t syscall_futex_waiters[SYSCALL_MAX_FUTEX_WAITERS];
static syscall_mq_t syscall_mqueues[SYSCALL_MAX_MQUEUES];
static syscall_shm_segment_t syscall_shm_segments[SYSCALL_MAX_SHM_SEGMENTS];
static syscall_eventfd_t syscall_eventfds[SYSCALL_MAX_EVENTFDS];

#define NICE_MIN (-20)
#define NICE_MAX 19
#define NICE_ERROR (-1000)

static int priority_to_nice(uint32_t priority)
{
    if (priority >= PRIORITY_IDLE) return NICE_MAX;
    if (priority == PRIORITY_REALTIME) return NICE_MIN;
    return (int)((priority * 39u) / PRIORITY_IDLE) + NICE_MIN;
}

static uint32_t nice_to_priority(int nice_value)
{
    int clamped = nice_value;
    if (clamped < NICE_MIN) clamped = NICE_MIN;
    if (clamped > NICE_MAX) clamped = NICE_MAX;
    return (uint32_t)(((clamped - NICE_MIN) * PRIORITY_IDLE + 19) / 39);
}

static thread_t *syscall_find_thread(uint32_t pid)
{
    thread_t *current = sched_get_current();
    process_t *proc;

    if (pid == 0 || (current && pid == current->pid))
        return current;

    proc = proc_find(pid);
    if (!proc || !proc->threads)
        return NULL;
    return proc->threads;
}

static uint32_t syscall_getpriority(uint32_t pid)
{
    thread_t *thread = syscall_find_thread(pid);
    if (!thread)
        return (uint32_t)NICE_ERROR;
    return (uint32_t)priority_to_nice(thread->priority);
}

static uint32_t syscall_setpriority(uint32_t pid, int nice_value)
{
    thread_t *thread = syscall_find_thread(pid);
    uint32_t current_pid = proc_current_pid();
    int current_nice;
    if (!thread)
        return (uint32_t)-1;

    current_nice = priority_to_nice(thread->priority);
    if ((pid != 0 && pid != current_pid) || nice_value < current_nice) {
        if (syscall_require_cap(OPENOS_CAP_SYS_ADMIN) < 0)
            return (uint32_t)-1;
    }

    return (sched_set_thread_priority(thread, nice_to_priority(nice_value)) == 0) ? 0u : (uint32_t)-1;
}

static uint32_t syscall_nice(int inc)
{
    thread_t *current = sched_get_current();
    int current_nice;
    int next_nice;
    if (!current)
        return (uint32_t)NICE_ERROR;

    current_nice = priority_to_nice(current->priority);
    next_nice = current_nice + inc;
    if (next_nice < NICE_MIN) next_nice = NICE_MIN;
    if (next_nice > NICE_MAX) next_nice = NICE_MAX;
    if (next_nice < current_nice && syscall_require_cap(OPENOS_CAP_SYS_ADMIN) < 0)
        return (uint32_t)-1;
    if (sched_set_thread_priority(current, nice_to_priority(next_nice)) < 0)
        return (uint32_t)-1;
    return (uint32_t)next_nice;
}

static syscall_mutex_t *syscall_mutex_from_handle(uint32_t handle)
{
    if (handle == 0 || handle > SYSCALL_MAX_MUTEXES)
        return NULL;
    if (syscall_mutexes[handle - 1].used != MUTEX_READY)
        return NULL;
    return &syscall_mutexes[handle - 1];
}

static int syscall_mutex_waiter_push(syscall_mutex_t *mutex, thread_t *thread)
{
    uint32_t pos;

    if (!mutex || !thread || mutex->wait_count >= MUTEX_WAIT_QUEUE_LIMIT)
        return -1;

    pos = (mutex->wait_head + mutex->wait_count) % MUTEX_WAIT_QUEUE_LIMIT;
    mutex->waiters[pos] = thread;
    mutex->wait_count++;
    return 0;
}

static thread_t *syscall_mutex_waiter_pop(syscall_mutex_t *mutex)
{
    thread_t *thread;

    if (!mutex || mutex->wait_count == 0)
        return NULL;

    thread = mutex->waiters[mutex->wait_head];
    mutex->waiters[mutex->wait_head] = NULL;
    mutex->wait_head = (mutex->wait_head + 1) % MUTEX_WAIT_QUEUE_LIMIT;
    mutex->wait_count--;
    return thread;
}

static uint32_t syscall_mutex_create(void)
{
    for (uint32_t i = 0; i < SYSCALL_MAX_MUTEXES; i++) {
        if (syscall_mutexes[i].used != MUTEX_READY) {
            memset(&syscall_mutexes[i], 0, sizeof(syscall_mutexes[i]));
            syscall_mutexes[i].used = MUTEX_READY;
            return i + 1;
        }
    }
    return (uint32_t)-1;
}

static uint32_t syscall_mutex_lock(uint32_t handle)
{
    syscall_mutex_t *mutex = syscall_mutex_from_handle(handle);
    thread_t *current = sched_get_current();

    if (!mutex || !current)
        return (uint32_t)-1;
    if (mutex->owner_tid == current->id)
        return (uint32_t)-1;

    while (mutex->owner_tid != 0) {
        if (mutex->owner_tid == current->id)
            return 0;
        if (syscall_mutex_waiter_push(mutex, current) < 0)
            return (uint32_t)-1;
        current->state = PROC_BLOCKED;
        sched_yield();
        if (mutex->used != MUTEX_READY)
            return (uint32_t)-1;
        if (mutex->owner_tid == current->id)
            return 0;
    }

    mutex->owner_tid = current->id;
    return 0;
}

static uint32_t syscall_mutex_unlock(uint32_t handle)
{
    syscall_mutex_t *mutex = syscall_mutex_from_handle(handle);
    thread_t *current = sched_get_current();
    thread_t *next;

    if (!mutex || !current || mutex->owner_tid != current->id)
        return (uint32_t)-1;

    next = syscall_mutex_waiter_pop(mutex);
    if (next) {
        mutex->owner_tid = next->id;
        thread_wake(next);
    } else {
        mutex->owner_tid = 0;
    }
    return 0;
}

static uint32_t syscall_mutex_destroy(uint32_t handle)
{
    syscall_mutex_t *mutex = syscall_mutex_from_handle(handle);
    thread_t *current = sched_get_current();
    thread_t *waiter;

    if (!mutex || !current)
        return (uint32_t)-1;
    if (mutex->owner_tid != 0 && mutex->owner_tid != current->id)
        return (uint32_t)-1;

    while ((waiter = syscall_mutex_waiter_pop(mutex)) != NULL)
        thread_wake(waiter);
    memset(mutex, 0, sizeof(*mutex));
    return 0;
}

static syscall_sem_t *syscall_sem_from_handle(uint32_t handle)
{
    if (handle == 0 || handle > SYSCALL_MAX_SEMAPHORES)
        return NULL;
    if (syscall_sems[handle - 1].used != SEM_READY)
        return NULL;
    return &syscall_sems[handle - 1];
}

static int syscall_sem_waiter_push(syscall_sem_t *sem, thread_t *thread)
{
    uint32_t pos;

    if (!sem || !thread || sem->wait_count >= SEM_WAIT_QUEUE_LIMIT)
        return -1;

    pos = (sem->wait_head + sem->wait_count) % SEM_WAIT_QUEUE_LIMIT;
    sem->waiters[pos] = thread;
    sem->wait_count++;
    return 0;
}

static thread_t *syscall_sem_waiter_pop(syscall_sem_t *sem)
{
    thread_t *thread;

    if (!sem || sem->wait_count == 0)
        return NULL;

    thread = sem->waiters[sem->wait_head];
    sem->waiters[sem->wait_head] = NULL;
    sem->wait_head = (sem->wait_head + 1) % SEM_WAIT_QUEUE_LIMIT;
    sem->wait_count--;
    return thread;
}

static uint32_t syscall_sem_create(uint32_t initial)
{
    for (uint32_t i = 0; i < SYSCALL_MAX_SEMAPHORES; i++) {
        if (syscall_sems[i].used != SEM_READY) {
            memset(&syscall_sems[i], 0, sizeof(syscall_sems[i]));
            syscall_sems[i].used = SEM_READY;
            syscall_sems[i].value = initial;
            return i + 1;
        }
    }
    return (uint32_t)-1;
}

static uint32_t syscall_sem_wait(uint32_t handle)
{
    syscall_sem_t *sem = syscall_sem_from_handle(handle);
    thread_t *current = sched_get_current();

    if (!sem || !current)
        return (uint32_t)-1;

    while (sem->value == 0) {
        if (syscall_sem_waiter_push(sem, current) < 0)
            return (uint32_t)-1;
        current->state = PROC_BLOCKED;
        sched_yield();
        if (sem->used != SEM_READY)
            return (uint32_t)-1;
    }

    sem->value--;
    return 0;
}

static uint32_t syscall_sem_post(uint32_t handle)
{
    syscall_sem_t *sem = syscall_sem_from_handle(handle);
    thread_t *waiter;

    if (!sem)
        return (uint32_t)-1;

    waiter = syscall_sem_waiter_pop(sem);
    if (waiter) {
        sem->value++;
        thread_wake(waiter);
    } else {
        sem->value++;
    }
    return 0;
}

static uint32_t syscall_sem_destroy(uint32_t handle)
{
    syscall_sem_t *sem = syscall_sem_from_handle(handle);
    thread_t *waiter;

    if (!sem)
        return (uint32_t)-1;

    while ((waiter = syscall_sem_waiter_pop(sem)) != NULL)
        thread_wake(waiter);
    memset(sem, 0, sizeof(*sem));
    return 0;
}

static syscall_cond_t *syscall_cond_from_handle(uint32_t handle)
{
    if (handle == 0 || handle > SYSCALL_MAX_CONDS)
        return NULL;
    if (syscall_conds[handle - 1].used != COND_READY)
        return NULL;
    return &syscall_conds[handle - 1];
}

static int syscall_cond_waiter_push(syscall_cond_t *cond, thread_t *thread)
{
    uint32_t pos;

    if (!cond || !thread || cond->wait_count >= COND_WAIT_QUEUE_LIMIT)
        return -1;

    pos = (cond->wait_head + cond->wait_count) % COND_WAIT_QUEUE_LIMIT;
    cond->waiters[pos] = thread;
    cond->wait_count++;
    return 0;
}

static thread_t *syscall_cond_waiter_pop(syscall_cond_t *cond)
{
    thread_t *thread;

    if (!cond || cond->wait_count == 0)
        return NULL;

    thread = cond->waiters[cond->wait_head];
    cond->waiters[cond->wait_head] = NULL;
    cond->wait_head = (cond->wait_head + 1) % COND_WAIT_QUEUE_LIMIT;
    cond->wait_count--;
    return thread;
}

static uint32_t syscall_cond_create(void)
{
    for (uint32_t i = 0; i < SYSCALL_MAX_CONDS; i++) {
        if (syscall_conds[i].used != COND_READY) {
            memset(&syscall_conds[i], 0, sizeof(syscall_conds[i]));
            syscall_conds[i].used = COND_READY;
            return i + 1;
        }
    }
    return (uint32_t)-1;
}

static uint32_t syscall_cond_wait(uint32_t cond_handle, uint32_t mutex_handle)
{
    syscall_cond_t *cond = syscall_cond_from_handle(cond_handle);
    syscall_mutex_t *mutex = syscall_mutex_from_handle(mutex_handle);
    thread_t *current = sched_get_current();

    if (!cond || !mutex || !current || mutex->owner_tid != current->id)
        return (uint32_t)-1;
    if (syscall_cond_waiter_push(cond, current) < 0)
        return (uint32_t)-1;
    if (syscall_mutex_unlock(mutex_handle) != 0)
        return (uint32_t)-1;

    current->state = PROC_BLOCKED;
    sched_yield();

    if (cond->used != COND_READY)
        return (uint32_t)-1;
    return syscall_mutex_lock(mutex_handle);
}

static uint32_t syscall_cond_signal(uint32_t handle)
{
    syscall_cond_t *cond = syscall_cond_from_handle(handle);
    thread_t *waiter;

    if (!cond)
        return (uint32_t)-1;

    waiter = syscall_cond_waiter_pop(cond);
    if (waiter)
        thread_wake(waiter);
    return 0;
}

static uint32_t syscall_cond_broadcast(uint32_t handle)
{
    syscall_cond_t *cond = syscall_cond_from_handle(handle);
    thread_t *waiter;

    if (!cond)
        return (uint32_t)-1;

    while ((waiter = syscall_cond_waiter_pop(cond)) != NULL)
        thread_wake(waiter);
    return 0;
}

static uint32_t syscall_cond_destroy(uint32_t handle)
{
    syscall_cond_t *cond = syscall_cond_from_handle(handle);
    thread_t *waiter;

    if (!cond)
        return (uint32_t)-1;

    while ((waiter = syscall_cond_waiter_pop(cond)) != NULL)
        thread_wake(waiter);
    memset(cond, 0, sizeof(*cond));
    return 0;
}

static void syscall_futex_remove_waiter(thread_t *thread)
{
    if (!thread)
        return;

    for (uint32_t i = 0; i < SYSCALL_MAX_FUTEX_WAITERS; i++) {
        if (syscall_futex_waiters[i].used == FUTEX_WAIT_ACTIVE &&
            syscall_futex_waiters[i].thread == thread) {
            memset(&syscall_futex_waiters[i], 0, sizeof(syscall_futex_waiters[i]));
            return;
        }
    }
}

static syscall_futex_waiter_t *syscall_futex_alloc_waiter(thread_t *current, uint32_t pid, uint32_t uaddr)
{
    syscall_futex_waiter_t *slot = NULL;

    for (uint32_t i = 0; i < SYSCALL_MAX_FUTEX_WAITERS; i++) {
        if (syscall_futex_waiters[i].used != FUTEX_WAIT_ACTIVE) {
            slot = &syscall_futex_waiters[i];
            break;
        }
    }
    if (!slot)
        return NULL;

    slot->used = FUTEX_WAIT_ACTIVE;
    slot->pid = pid;
    slot->uaddr = uaddr;
    slot->thread = current;
    return slot;
}

static uint32_t syscall_futex_wait_common(uint32_t uaddr, uint32_t expected,
                                          uint32_t timeout_ms, uint32_t has_timeout)
{
    uint32_t current_value = 0;
    thread_t *current = sched_get_current();
    uint32_t pid = proc_current_pid();
    syscall_futex_waiter_t *slot;

    if (!current || uaddr == 0 || (uaddr & 0x3u) != 0)
        return (uint32_t)-1;
    if (copy_from_user(&current_value, (const void *)uaddr, sizeof(current_value)) < 0)
        return (uint32_t)-1;
    if (current_value != expected)
        return 1;
    if (has_timeout && timeout_ms == 0)
        return 2;

    slot = syscall_futex_alloc_waiter(current, pid, uaddr);
    if (!slot)
        return (uint32_t)-1;

    if (has_timeout) {
        current->wake_time = sched_time_ms() + timeout_ms;
        current->state = PROC_SLEEPING;
    } else {
        current->state = PROC_BLOCKED;
    }
    sched_yield();

    if (has_timeout && slot->used == FUTEX_WAIT_ACTIVE && slot->thread == current) {
        memset(slot, 0, sizeof(*slot));
        return 2;
    }

    syscall_futex_remove_waiter(current);
    return 0;
}

static uint32_t syscall_futex_wait(uint32_t uaddr, uint32_t expected)
{
    return syscall_futex_wait_common(uaddr, expected, 0, 0);
}

static uint32_t syscall_futex_wait_timeout(uint32_t uaddr, uint32_t expected, uint32_t timeout_ms)
{
    return syscall_futex_wait_common(uaddr, expected, timeout_ms, 1);
}

static uint32_t syscall_futex_wake(uint32_t uaddr, uint32_t max_wake)
{
    uint32_t pid = proc_current_pid();
    uint32_t woke = 0;

    if (uaddr == 0 || (uaddr & 0x3u) != 0)
        return (uint32_t)-1;
    if (!user_ptr_valid((const void *)uaddr, sizeof(uint32_t), USERMEM_READ))
        return (uint32_t)-1;
    if (max_wake == 0)
        return 0;

    for (uint32_t i = 0; i < SYSCALL_MAX_FUTEX_WAITERS && woke < max_wake; i++) {
        syscall_futex_waiter_t *waiter = &syscall_futex_waiters[i];
        if (waiter->used == FUTEX_WAIT_ACTIVE && waiter->pid == pid &&
            waiter->uaddr == uaddr && waiter->thread) {
            thread_t *thread = waiter->thread;
            memset(waiter, 0, sizeof(*waiter));
            thread_wake(thread);
            woke++;
        }
    }

    return woke;
}

static syscall_mq_t *syscall_mq_from_handle(uint32_t handle)
{
    if (handle == 0 || handle > SYSCALL_MAX_MQUEUES)
        return NULL;
    if (syscall_mqueues[handle - 1].used != MQ_READY)
        return NULL;
    return &syscall_mqueues[handle - 1];
}

static uint32_t syscall_mq_create(void)
{
    for (uint32_t i = 0; i < SYSCALL_MAX_MQUEUES; i++) {
        if (syscall_mqueues[i].used != MQ_READY) {
            memset(&syscall_mqueues[i], 0, sizeof(syscall_mqueues[i]));
            syscall_mqueues[i].used = MQ_READY;
            return i + 1;
        }
    }
    return (uint32_t)-1;
}

static uint32_t syscall_mq_send(uint32_t handle, const void *buf, uint32_t len)
{
    syscall_mq_t *mq = syscall_mq_from_handle(handle);
    syscall_mq_message_t *msg;
    uint32_t tail;

    if (!mq || !buf || len == 0 || len > MQ_MAX_MESSAGE_SIZE || mq->count >= MQ_MAX_MESSAGES)
        return (uint32_t)-1;

    tail = (mq->head + mq->count) % MQ_MAX_MESSAGES;
    msg = &mq->messages[tail];
    if (copy_from_user(msg->data, buf, len) < 0)
        return (uint32_t)-1;
    msg->len = len;
    mq->count++;
    return len;
}

static uint32_t syscall_mq_recv(uint32_t handle, void *buf, uint32_t len)
{
    syscall_mq_t *mq = syscall_mq_from_handle(handle);
    syscall_mq_message_t *msg;
    uint32_t copy_len;

    if (!mq || !buf || len == 0 || mq->count == 0)
        return (uint32_t)-1;

    msg = &mq->messages[mq->head];
    copy_len = msg->len;
    if (copy_len > len)
        copy_len = len;
    if (copy_to_user(buf, msg->data, copy_len) < 0)
        return (uint32_t)-1;

    memset(msg, 0, sizeof(*msg));
    mq->head = (mq->head + 1) % MQ_MAX_MESSAGES;
    mq->count--;
    return copy_len;
}

static uint32_t syscall_mq_destroy(uint32_t handle)
{
    syscall_mq_t *mq = syscall_mq_from_handle(handle);

    if (!mq)
        return (uint32_t)-1;
    memset(mq, 0, sizeof(*mq));
    return 0;
}

static void vga_write_str(const char *s, uint8_t color) {
    int row = 1;
    static int col = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '\n') {
            col = 0;
            row++;
        } else {
            VGA[row * 80 + col] = (uint16_t)((color << 8) | s[i]);
            col++;
            if (col >= 80) { col = 0; row++; }
        }
    }
}

static uint32_t syscall_write_user_buffer(int fd, const void *user_buf, uint32_t count)
{
    char chunk[SYSCALL_IO_CHUNK];
    uint32_t done = 0;

    if (count == 0)
        return 0;
    if (!user_ptr_valid(user_buf, count, USERMEM_READ))
        return (uint32_t)-1;

    while (done < count) {
        uint32_t n = count - done;
        int written;

        if (n > SYSCALL_IO_CHUNK)
            n = SYSCALL_IO_CHUNK;
        if (copy_from_user(chunk, (const char *)user_buf + done, n) < 0)
            return done ? done : (uint32_t)-1;

        if ((fd == STDOUT_FILENO || fd == STDERR_FILENO) && !vfs_get_file(fd)) {
            for (uint32_t i = 0; i < n; i++) {
                serial_putc(chunk[i]);
                vga_putc(chunk[i]);
                gui_terminal_putc(chunk[i]);
            }
            written = (int)n;
        } else {
            written = vfs_write(fd, chunk, n);
        }

        if (written < 0)
            return done ? done : (uint32_t)-1;
        if (written == 0)
            break;

        done += (uint32_t)written;
        if ((uint32_t)written < n)
            break;
    }

    return done;
}

static uint32_t syscall_read_user_buffer(int fd, void *user_buf, uint32_t count)
{
    char chunk[SYSCALL_IO_CHUNK];
    uint32_t done = 0;

    if (count == 0)
        return 0;
    if (!user_ptr_valid(user_buf, count, USERMEM_WRITE))
        return (uint32_t)-1;

    while (done < count) {
        uint32_t n = count - done;
        int got;

        if (n > SYSCALL_IO_CHUNK)
            n = SYSCALL_IO_CHUNK;

        if (fd == STDIN_FILENO && !vfs_get_file(fd)) {
            got = 0;
            while (got == 0) {
                while ((uint32_t)got < n && input_has_data()) {
                    chunk[got++] = input_getc();
                }
                if (got == 0 && input_consume_eof())
                    break;
                if (got == 0) {
                    sched_yield();
                }
            }
        } else {
            got = vfs_read(fd, chunk, n);
        }
        if (got < 0)
            return done ? done : (uint32_t)-1;
        if (got == 0)
            break;

        if (copy_to_user((char *)user_buf + done, chunk, (uint32_t)got) < 0)
            return done ? done : (uint32_t)-1;

        done += (uint32_t)got;
        if ((uint32_t)got < n)
            break;
    }

    return done;
}

static int syscall_copy_user_path(char *dst, const char *user_path)
{
    return strncpy_from_user(dst, user_path, USERMEM_CSTR_MAX);
}

static uint32_t sys_ai_request(uint32_t user_req_ptr)
{
    openos_ai_request_t user_req;
    ai_request_t request;
    ai_response_t response;
    char prompt[AI_PROMPT_MAX];
    uint32_t n;
    int status;

    if (!user_req_ptr)
        return (uint32_t)AI_STATUS_INVALID_ARGUMENT;
    if (copy_from_user(&user_req, (const void *)user_req_ptr, sizeof(user_req)) < 0)
        return (uint32_t)AI_STATUS_INVALID_ARGUMENT;
    if (!user_req.prompt || !user_req.response || user_req.response_len == 0)
        return (uint32_t)AI_STATUS_INVALID_ARGUMENT;
    if (strncpy_from_user(prompt, user_req.prompt, sizeof(prompt)) < 0)
        return (uint32_t)AI_STATUS_INVALID_ARGUMENT;

    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    request.task_type = AI_TASK_CHAT;
    request.backend_preference = ai_get_default_backend();
    request.prompt = prompt;
    request.max_tokens = user_req.response_len;
    request.flags = user_req.flags;

    status = ai_generate(&request, &response);
    if (status < 0)
        return (uint32_t)status;

    n = (uint32_t)strlen(response.text) + 1;
    if (n > user_req.response_len)
        return (uint32_t)AI_STATUS_BUFFER_TOO_SMALL;
    if (copy_to_user(user_req.response, response.text, n) < 0)
        return (uint32_t)AI_STATUS_INVALID_ARGUMENT;

    return n - 1;
}

static uint32_t syscall_poll(openos_pollfd_t *user_fds, uint32_t nfds, uint32_t timeout_ms)
{
    uint32_t start_ms;

    if (nfds > 64)
        return (uint32_t)-1;
    if (nfds > 0 && (!user_fds || !user_ptr_valid(user_fds, nfds * sizeof(openos_pollfd_t), USERMEM_READ | USERMEM_WRITE)))
        return (uint32_t)-1;

    start_ms = sched_time_ms();
    for (;;) {
        uint32_t ready = 0;

        for (uint32_t i = 0; i < nfds; i++) {
            openos_pollfd_t pfd;
            if (copy_from_user(&pfd, &user_fds[i], sizeof(pfd)) < 0)
                return (uint32_t)-1;
            pfd.revents = 0;
            if (pfd.fd >= 0 && pfd.events != 0)
                pfd.revents = (short)vfs_poll_fd(pfd.fd, (uint32_t)pfd.events);
            if (pfd.revents != 0)
                ready++;
            if (copy_to_user(&user_fds[i], &pfd, sizeof(pfd)) < 0)
                return (uint32_t)-1;
        }

        if (ready != 0 || timeout_ms == 0)
            return ready;
        if ((uint32_t)(sched_time_ms() - start_ms) >= timeout_ms)
            return 0;

        thread_sleep(1);
        sched_yield();
    }
}

static uint32_t syscall_select(uint32_t nfds, uint32_t *readfds, uint32_t *writefds, uint32_t *exceptfds, uint32_t timeout_ms)
{
    uint32_t in_read = 0;
    uint32_t in_write = 0;
    uint32_t out_read = 0;
    uint32_t out_write = 0;
    uint32_t out_except = 0;
    uint32_t ready = 0;

    (void)timeout_ms;
    if (nfds > 32)
        return (uint32_t)-1;
    if (readfds) {
        if (!user_ptr_valid(readfds, sizeof(uint32_t), USERMEM_READ | USERMEM_WRITE) || copy_from_user(&in_read, readfds, sizeof(in_read)) < 0)
            return (uint32_t)-1;
    }
    if (writefds) {
        if (!user_ptr_valid(writefds, sizeof(uint32_t), USERMEM_READ | USERMEM_WRITE) || copy_from_user(&in_write, writefds, sizeof(in_write)) < 0)
            return (uint32_t)-1;
    }
    if (exceptfds && !user_ptr_valid(exceptfds, sizeof(uint32_t), USERMEM_WRITE))
        return (uint32_t)-1;

    for (uint32_t fd = 0; fd < nfds; fd++) {
        uint32_t bit = 1u << fd;
        if ((in_read & bit) && (vfs_poll_fd((int)fd, VFS_POLLIN) & (VFS_POLLIN | VFS_POLLHUP | VFS_POLLERR))) {
            out_read |= bit;
            ready++;
        }
        if ((in_write & bit) && (vfs_poll_fd((int)fd, VFS_POLLOUT) & (VFS_POLLOUT | VFS_POLLERR))) {
            out_write |= bit;
            ready++;
        }
    }

    if (readfds && copy_to_user(readfds, &out_read, sizeof(out_read)) < 0)
        return (uint32_t)-1;
    if (writefds && copy_to_user(writefds, &out_write, sizeof(out_write)) < 0)
        return (uint32_t)-1;
    if (exceptfds && copy_to_user(exceptfds, &out_except, sizeof(out_except)) < 0)
        return (uint32_t)-1;
    return ready;
}

#define SYSCALL_ARG_MAX 8
#define SYSCALL_ARG_LEN 64
#define SYSCALL_ENV_MAX 8
#define SYSCALL_ENV_LEN 96

typedef struct syscall_strvec {
    char storage[SYSCALL_ARG_MAX + 1][SYSCALL_ENV_LEN];
    char *ptrs[SYSCALL_ARG_MAX + 1];
} syscall_strvec_t;

static int syscall_copy_user_strvec(syscall_strvec_t *out,
                                    char *const *user_vec,
                                    int max_items,
                                    int max_len)
{
    int i;

    if (!out)
        return -1;

    for (i = 0; i <= SYSCALL_ARG_MAX; i++) {
        out->ptrs[i] = 0;
        out->storage[i][0] = '\0';
    }

    if (!user_vec)
        return 0;

    for (i = 0; i < max_items; i++) {
        char *user_item = 0;

        if (copy_from_user(&user_item, user_vec + i, sizeof(user_item)) < 0)
            return -1;
        if (!user_item) {
            out->ptrs[i] = 0;
            return 0;
        }
        if (strncpy_from_user(out->storage[i], user_item, (uint32_t)max_len) < 0)
            return -1;
        out->ptrs[i] = out->storage[i];
    }

    {
        char *extra = 0;
        if (copy_from_user(&extra, user_vec + max_items, sizeof(extra)) < 0)
            return -1;
        if (extra)
            return -1;
    }

    out->ptrs[max_items] = 0;
    return 0;
}

static uint64_t syscall_pack_vfs_time(const vfs_time_t *time)
{
    if (!time || time->year == 0) {
        return 0;
    }
    return ((uint64_t)time->year * 10000000000ull) +
           ((uint64_t)time->month * 100000000u) +
           ((uint64_t)time->day * 1000000u) +
           ((uint64_t)time->hour * 10000u) +
           ((uint64_t)time->minute * 100u) +
           (uint64_t)time->second;
}

static void syscall_fill_user_stat(openos_stat_t *user_st, const inode_t *st)
{
    user_st->ino = st->ino;
    user_st->mode = st->mode;
    user_st->size = st->size;
    user_st->nlinks = st->nlinks;
    user_st->fs_type = st->fs_type;
    user_st->uid = st->uid;
    user_st->gid = st->gid;
    user_st->ctime_utc = syscall_pack_vfs_time(&st->ctime);
    user_st->mtime_utc = syscall_pack_vfs_time(&st->mtime);
    user_st->atime_utc = syscall_pack_vfs_time(&st->atime);
}

static void syscall_fill_user_statfs(openos_statfs_t *user_st, const inode_t *st)
{
    uint32_t fs_type = st ? st->fs_type : 0x4f534653u;

    memset(user_st, 0, sizeof(*user_st));
    user_st->f_type = fs_type ? fs_type : 0x4f534653u;
    user_st->f_bsize = 4096u;
    user_st->f_blocks = 16384u;
    user_st->f_bfree = 8192u;
    user_st->f_bavail = 8192u;
    user_st->f_files = 4096u;
    user_st->f_ffree = 2048u;
    user_st->f_namelen = MAX_NAME - 1u;
    user_st->f_flags = 0u;
}

static uint32_t syscall_getdents(uint32_t fd, uint32_t user_buf, uint32_t count)
{
    file_t *f;
    dentry_t *de;
    openos_dirent_t user_de;
    uint32_t copied = 0;

    if (!user_buf || count < sizeof(openos_dirent_t))
        return (uint32_t)-1;
    if (!user_ptr_valid((void *)user_buf, count, USERMEM_WRITE))
        return (uint32_t)-1;

    f = vfs_get_file((int)fd);
    if (!f || !f->inode || !f->dentry || (f->inode->mode & FS_DIR) != FS_DIR)
        return (uint32_t)-1;

    while (copied + sizeof(openos_dirent_t) <= count) {
        uint32_t idx = 0;
        de = f->dentry->child;
        while (de && idx < f->offset) {
            de = de->sibling;
            idx++;
        }
        if (!de || !de->inode)
            break;
        f->offset++;

        memset(&user_de, 0, sizeof(user_de));
        user_de.ino = de->inode->ino;
        user_de.mode = de->inode->mode;
        user_de.size = de->inode->size;
        strncpy(user_de.name, de->name, sizeof(user_de.name) - 1);
        user_de.name[sizeof(user_de.name) - 1] = 0;

        if (copy_to_user((void *)(user_buf + copied), &user_de, sizeof(user_de)) < 0)
            return (uint32_t)-1;
        copied += sizeof(openos_dirent_t);
    }

    return copied;
}

#define SYS_MMAP_BASE  ASLR_MMAP_BASE_MIN
#define SYS_MMAP_LIMIT ASLR_MMAP_LIMIT
#define SYS_MMAP_MAX_REQUEST (16u * 1024u * 1024u)

static uint32_t page_align_up_u32(uint32_t value)
{
    return (value + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
}

static syscall_shm_segment_t *syscall_shm_from_handle(uint32_t handle)
{
    if (handle == 0 || handle > SYSCALL_MAX_SHM_SEGMENTS)
        return NULL;
    if (syscall_shm_segments[handle - 1].used != SHM_READY)
        return NULL;
    return &syscall_shm_segments[handle - 1];
}

static uint32_t syscall_shm_create(void)
{
    void *phys;

    for (uint32_t i = 0; i < SYSCALL_MAX_SHM_SEGMENTS; i++) {
        if (syscall_shm_segments[i].used == SHM_READY)
            continue;
        phys = pmm_alloc_page();
        if (!phys)
            return (uint32_t)-1;
        memset((void *)phys, 0, PAGE_SIZE);
        syscall_shm_segments[i].used = SHM_READY;
        syscall_shm_segments[i].phys = (uint32_t)phys;
        syscall_shm_segments[i].refcount = 0;
        syscall_shm_segments[i].flags = 0;
        return i + 1;
    }

    return (uint32_t)-1;
}

static uint32_t syscall_shm_map(uint32_t handle)
{
    syscall_shm_segment_t *seg = syscall_shm_from_handle(handle);
    process_t *proc;
    uint32_t start;

    if (!seg || !seg->phys)
        return (uint32_t)-1;

    proc = proc_find(proc_current_pid());
    if (!proc)
        return (uint32_t)-1;

    if (proc->mmap_base == 0 || proc->mmap_end < SYS_MMAP_BASE || proc->mmap_end >= SYS_MMAP_LIMIT) {
        proc->mmap_base = aslr_pick_mmap_base(proc->pid);
        proc->mmap_end = proc->mmap_base;
    }

    start = page_align_up_u32(proc->mmap_end);
    if (start < SYS_MMAP_BASE || start > SYS_MMAP_LIMIT || PAGE_SIZE > (SYS_MMAP_LIMIT - start))
        return (uint32_t)-1;

    pmm_ref_page((void *)seg->phys);
    vmm_map_page(start, seg->phys, VMM_USER);
    seg->refcount++;
    proc->mmap_end = start + PAGE_SIZE;
    return start;
}

static uint32_t syscall_shm_destroy(uint32_t handle)
{
    syscall_shm_segment_t *seg = syscall_shm_from_handle(handle);

    if (!seg || !seg->phys)
        return (uint32_t)-1;
    if (seg->refcount != 0)
        return (uint32_t)-1;
    pmm_free_page((void *)seg->phys);
    memset(seg, 0, sizeof(*seg));
    return 0;
}

static uint32_t syscall_shm_info(uint32_t handle, uint32_t user_info)
{
    syscall_shm_segment_t *seg = syscall_shm_from_handle(handle);
    uint32_t info[4];

    if (!seg || !seg->phys || !user_info)
        return (uint32_t)-1;
    if (!user_ptr_valid((void *)user_info, sizeof(info), USERMEM_WRITE))
        return (uint32_t)-1;

    info[0] = handle;
    info[1] = PAGE_SIZE;
    info[2] = seg->refcount;
    info[3] = seg->flags;
    if (copy_to_user((void *)user_info, info, sizeof(info)) < 0)
        return (uint32_t)-1;
    return 0;
}

static syscall_eventfd_t *syscall_eventfd_from_handle(uint32_t handle)
{
    if (handle == 0 || handle > SYSCALL_MAX_EVENTFDS)
        return NULL;
    if (syscall_eventfds[handle - 1].used != EVENTFD_READY)
        return NULL;
    return &syscall_eventfds[handle - 1];
}

static uint32_t syscall_eventfd_create(uint32_t initval)
{
    for (uint32_t i = 0; i < SYSCALL_MAX_EVENTFDS; i++) {
        if (syscall_eventfds[i].used == EVENTFD_READY)
            continue;
        memset(&syscall_eventfds[i], 0, sizeof(syscall_eventfds[i]));
        syscall_eventfds[i].used = EVENTFD_READY;
        syscall_eventfds[i].counter = initval;
        return i + 1;
    }
    return (uint32_t)-1;
}

static uint32_t syscall_eventfd_write(uint32_t handle, uint32_t value)
{
    syscall_eventfd_t *efd = syscall_eventfd_from_handle(handle);

    if (!efd)
        return (uint32_t)-1;
    if (value > UINT32_MAX - efd->counter)
        efd->counter = UINT32_MAX;
    else
        efd->counter += value;
    return 0;
}

static uint32_t syscall_eventfd_read(uint32_t handle, uint32_t user_value)
{
    syscall_eventfd_t *efd = syscall_eventfd_from_handle(handle);
    uint32_t value;

    if (!efd || user_value == 0)
        return (uint32_t)-1;
    value = efd->counter;
    efd->counter = 0;
    if (copy_to_user((void *)user_value, &value, sizeof(value)) < 0) {
        efd->counter = value;
        return (uint32_t)-1;
    }
    return 0;
}

static uint32_t syscall_eventfd_destroy(uint32_t handle)
{
    syscall_eventfd_t *efd = syscall_eventfd_from_handle(handle);

    if (!efd)
        return (uint32_t)-1;
    memset(efd, 0, sizeof(*efd));
    return 0;
}

static process_mmap_vma_t *sys_mmap_find_vma(process_t *proc, uint32_t addr, uint32_t len)
{
    uint32_t end;

    if (!proc || len == 0)
        return 0;
    end = addr + len;
    if (end <= addr)
        return 0;

    for (int i = 0; i < PROCESS_MMAP_VMA_MAX; i++) {
        process_mmap_vma_t *vma = &proc->mmap_vmas[i];
        if (vma->start && addr >= vma->start && end <= vma->end)
            return vma;
    }
    return 0;
}

static void sys_mmap_clear_vma(process_mmap_vma_t *vma)
{
    vma->start = 0;
    vma->end = 0;
    vma->prot = 0;
    vma->flags = 0;
    vma->file_fd = -1;
    vma->file_length = 0;
}

static int sys_mmap_vma_can_merge(const process_mmap_vma_t *left, const process_mmap_vma_t *right)
{
    if (!left || !right || !left->start || !right->start)
        return 0;
    if (left->end != right->start)
        return 0;
    if (left->prot != right->prot || left->flags != right->flags)
        return 0;
    if ((left->flags & PROCESS_MMAP_FLAG_FILE) != 0)
        return 0;
    return 1;
}

static void sys_mmap_merge_adjacent_vmas(process_t *proc)
{
    int merged;

    if (!proc)
        return;

    do {
        merged = 0;
        for (int i = 0; i < PROCESS_MMAP_VMA_MAX && !merged; i++) {
            process_mmap_vma_t *a = &proc->mmap_vmas[i];
            if (!a->start)
                continue;
            for (int j = 0; j < PROCESS_MMAP_VMA_MAX; j++) {
                process_mmap_vma_t *b = &proc->mmap_vmas[j];
                if (i == j || !b->start)
                    continue;
                if (a->start > b->start) {
                    process_mmap_vma_t *tmp = a;
                    a = b;
                    b = tmp;
                }
                if (sys_mmap_vma_can_merge(a, b)) {
                    a->end = b->end;
                    sys_mmap_clear_vma(b);
                    merged = 1;
                    break;
                }
            }
        }
    } while (merged);
}

static int sys_mmap_range_is_mapped(process_t *proc, uint32_t addr, uint32_t len)
{
    uint32_t end = addr + len;
    uint32_t cursor = addr;

    if (!proc || len == 0 || end <= addr)
        return 0;

    while (cursor < end) {
        process_mmap_vma_t *cover = sys_mmap_find_vma(proc, cursor, 1);
        if (!cover || cover->start > cursor || cover->end <= cursor)
            return 0;
        cursor = cover->end;
    }

    return cursor >= end;
}

static int sys_mmap_add_vma(process_t *proc, uint32_t start, uint32_t len, uint32_t prot, uint32_t flags)
{
    uint32_t end = start + len;

    if (!proc || end <= start)
        return -1;

    for (int i = 0; i < PROCESS_MMAP_VMA_MAX; i++) {
        process_mmap_vma_t *vma = &proc->mmap_vmas[i];
        if (!vma->start)
            continue;
        if (start < vma->end && end > vma->start)
            return -1;
    }

    for (int i = 0; i < PROCESS_MMAP_VMA_MAX; i++) {
        process_mmap_vma_t *vma = &proc->mmap_vmas[i];
        if (vma->start)
            continue;
        vma->start = start;
        vma->end = end;
        vma->prot = prot;
        vma->flags = flags;
        vma->file_fd = -1;
        vma->file_length = 0;
        sys_mmap_merge_adjacent_vmas(proc);
        return 0;
    }
    return -1;
}

static int sys_mmap_remove_vma(process_t *proc, uint32_t addr, uint32_t len)
{
    uint32_t end = addr + len;

    if (!proc || end <= addr)
        return -1;

    for (int i = 0; i < PROCESS_MMAP_VMA_MAX; i++) {
        process_mmap_vma_t *vma = &proc->mmap_vmas[i];
        uint32_t old_end;
        int free_slot = -1;

        if (!vma->start || addr >= vma->end || end <= vma->start)
            continue;

        if (addr <= vma->start && end >= vma->end) {
            sys_mmap_clear_vma(vma);
            continue;
        }

        if (addr <= vma->start) {
            vma->start = end;
            continue;
        }

        if (end >= vma->end) {
            vma->end = addr;
            continue;
        }

        for (int j = 0; j < PROCESS_MMAP_VMA_MAX; j++) {
            if (!proc->mmap_vmas[j].start) {
                free_slot = j;
                break;
            }
        }
        if (free_slot < 0)
            return -1;

        old_end = vma->end;
        vma->end = addr;
        proc->mmap_vmas[free_slot].start = end;
        proc->mmap_vmas[free_slot].end = old_end;
        proc->mmap_vmas[free_slot].prot = vma->prot;
        proc->mmap_vmas[free_slot].flags = vma->flags;
        proc->mmap_vmas[free_slot].file_fd = vma->file_fd;
        proc->mmap_vmas[free_slot].file_length = vma->file_length;
    }

    sys_mmap_merge_adjacent_vmas(proc);
    return 0;
}

static void sys_munmap_range(uint32_t addr, uint32_t len)
{
    uint32_t end = addr + len;
    for (uint32_t va = addr; va < end; va += PAGE_SIZE) {
        uint32_t pte = vmm_get_mapping(va);
        if ((pte & (PTE_PRESENT | PTE_USER)) == (PTE_PRESENT | PTE_USER)) {
            uint32_t pa = pte & PAGE_MASK;
            vmm_unmap_page(va);
            pmm_free_page((void *)pa);
        }
    }
}

static int sys_munmap_sync_shared_file_vma(process_mmap_vma_t *vma,
                                             uint32_t addr,
                                             uint32_t len)
{
    uint32_t sync_start;
    uint32_t sync_end;
    uint32_t sync_len;
    uint32_t vma_offset;
    uint32_t written = 0;
    int saved_pos;

    if (!vma || (vma->flags & PROCESS_MMAP_FLAG_FILE) == 0 ||
        (vma->flags & PROCESS_MMAP_FLAG_SHARED) == 0 ||
        (vma->prot & PROCESS_MMAP_PROT_WRITE) == 0 ||
        vma->file_fd < 0 || vma->file_length == 0)
        return 0;

    sync_start = addr > vma->start ? addr : vma->start;
    sync_end = (addr + len) < vma->end ? (addr + len) : vma->end;
    if (sync_start >= sync_end || sync_start < vma->start)
        return 0;
    vma_offset = sync_start - vma->start;
    if (vma_offset >= vma->file_length)
        return 0;

    sync_len = sync_end - sync_start;
    if (sync_len > vma->file_length - vma_offset)
        sync_len = vma->file_length - vma_offset;
    if (sync_len == 0)
        return 0;

    saved_pos = vfs_seek(vma->file_fd, 0, 1);
    if (saved_pos < 0)
        return -1;
    if (vfs_seek(vma->file_fd, (int)vma_offset, 0) < 0) {
        vfs_seek(vma->file_fd, saved_pos, 0);
        return -1;
    }

    while (written < sync_len) {
        uint8_t chunk[512];
        uint32_t want = sync_len - written;
        int n;
        if (want > sizeof(chunk))
            want = sizeof(chunk);
        if (copy_from_user(chunk, (const void *)(uintptr_t)(sync_start + written), want) < 0) {
            vfs_seek(vma->file_fd, saved_pos, 0);
            return -1;
        }
        n = vfs_write(vma->file_fd, chunk, want);
        if (n <= 0) {
            vfs_seek(vma->file_fd, saved_pos, 0);
            return -1;
        }
        written += (uint32_t)n;
    }

    vfs_seek(vma->file_fd, saved_pos, 0);
    return 0;
}

static int sys_munmap_sync_shared_file_range(process_t *proc, uint32_t addr, uint32_t len)
{
    uint32_t end = addr + len;

    if (!proc || end <= addr)
        return -1;

    for (int i = 0; i < PROCESS_MMAP_VMA_MAX; i++) {
        process_mmap_vma_t *vma = &proc->mmap_vmas[i];
        if (!vma->start || addr >= vma->end || end <= vma->start)
            continue;
        if (sys_munmap_sync_shared_file_vma(vma, addr, len) < 0)
            return -1;
    }
    return 0;
}

static uint32_t sys_mmap_file_page_flags(uint32_t prot, uint32_t flags)
{
    uint32_t page_flags = PTE_PRESENT | PTE_USER;

    if ((prot & PROCESS_MMAP_PROT_WRITE) != 0) {
        if ((flags & PROCESS_MMAP_FLAG_PRIVATE) != 0)
            page_flags |= PTE_COW;
        else
            page_flags |= PTE_RW;
    }

    return page_flags;
}

static void sys_mmap_apply_file_page_flags(uint32_t base,
                                           uint32_t pages,
                                           uint32_t prot,
                                           uint32_t flags)
{
    uint32_t page_flags = sys_mmap_file_page_flags(prot, flags);

    for (uint32_t page = 0; page < pages; ++page)
        vmm_update_page_flags(base + page * PAGE_SIZE, page_flags);
}

static uint32_t sys_mmap_anonymous(uint32_t addr, uint32_t len, uint32_t prot, uint32_t flags)
{
    process_t *proc;
    uint32_t start;

    if (len == 0 || len > SYS_MMAP_MAX_REQUEST)
        return (uint32_t)-1;
    if ((prot & ~(uint32_t)0x7u) != 0)
        return (uint32_t)-1;
    if ((prot & PROCESS_MMAP_PROT_EXEC) != 0)
        return (uint32_t)-1;
    if ((flags & ~(PROCESS_MMAP_FLAG_ANON | PROCESS_MMAP_FLAG_PRIVATE | PROCESS_MMAP_FLAG_FIXED)) != 0)
        return (uint32_t)-1;
    if ((flags & PROCESS_MMAP_FLAG_FIXED) && (addr & (PAGE_SIZE - 1u)) != 0)
        return (uint32_t)-1;
    if (!(flags & PROCESS_MMAP_FLAG_FIXED) && addr != 0)
        return (uint32_t)-1;

    len = page_align_up_u32(len);
    if (len == 0)
        return (uint32_t)-1;

    proc = proc_find(proc_current_pid());
    if (!proc)
        return (uint32_t)-1;

    if (proc->mmap_base == 0 || proc->mmap_end < SYS_MMAP_BASE || proc->mmap_end >= SYS_MMAP_LIMIT) {
        proc->mmap_base = aslr_pick_mmap_base(proc->pid);
        proc->mmap_end = proc->mmap_base;
    }

    start = (flags & PROCESS_MMAP_FLAG_FIXED) ? addr : page_align_up_u32(proc->mmap_end);
    if (start < SYS_MMAP_BASE || start > SYS_MMAP_LIMIT || len > (SYS_MMAP_LIMIT - start))
        return (uint32_t)-1;

    if (sys_mmap_add_vma(proc, start, len, prot, flags | PROCESS_MMAP_FLAG_ANON) < 0)
        return (uint32_t)-1;

    /* Demand paging: reserve virtual range only. Physical pages are allocated on #PF. */
    if (start + len > proc->mmap_end)
        proc->mmap_end = start + len;
    return start;
}

static uint32_t sys_mmap_file(uint32_t fd_raw, uint32_t len_raw, uint32_t prot_raw, uint32_t flags_raw)
{
    int fd = (int)fd_raw;
    uint32_t length = len_raw;
    uint32_t prot = prot_raw;
    uint32_t flags = flags_raw;

    if (fd < 0 || length == 0 || length > SYS_MMAP_MAX_REQUEST)
        return (uint32_t)-1;
    if ((prot & ~(PROCESS_MMAP_PROT_READ | PROCESS_MMAP_PROT_WRITE)) != 0)
        return (uint32_t)-1;
    if ((prot & PROCESS_MMAP_PROT_EXEC) != 0)
        return (uint32_t)-1;
    if ((flags & ~(PROCESS_MMAP_FLAG_FILE | PROCESS_MMAP_FLAG_PRIVATE | PROCESS_MMAP_FLAG_SHARED)) != 0)
        return (uint32_t)-1;
    if ((flags & PROCESS_MMAP_FLAG_FILE) == 0)
        return (uint32_t)-1;
    if (((flags & PROCESS_MMAP_FLAG_PRIVATE) != 0) == ((flags & PROCESS_MMAP_FLAG_SHARED) != 0))
        return (uint32_t)-1;
    if ((flags & PROCESS_MMAP_FLAG_SHARED) && ((prot & PROCESS_MMAP_PROT_WRITE) == 0))
        return (uint32_t)-1;

    process_t *proc = proc_find(proc_current_pid());
    if (!proc)
        return (uint32_t)-1;

    if (proc->mmap_base == 0 || proc->mmap_end < SYS_MMAP_BASE || proc->mmap_end >= SYS_MMAP_LIMIT) {
        proc->mmap_base = aslr_pick_mmap_base(proc->pid);
        proc->mmap_end = proc->mmap_base;
    }

    uint32_t pages = (length + PAGE_SIZE - 1u) / PAGE_SIZE;
    uint32_t mapped_len = pages * PAGE_SIZE;
    uint32_t base = page_align_up_u32(proc->mmap_end);
    if (base < SYS_MMAP_BASE || base > SYS_MMAP_LIMIT || mapped_len > (SYS_MMAP_LIMIT - base))
        return (uint32_t)-1;
    if (sys_mmap_add_vma(proc,
                         base,
                         mapped_len,
                         prot,
                         PROCESS_MMAP_FLAG_FILE |
                             (flags & (PROCESS_MMAP_FLAG_PRIVATE | PROCESS_MMAP_FLAG_SHARED))) < 0)
        return (uint32_t)-1;
    process_mmap_vma_t *file_vma = sys_mmap_find_vma(proc, base, mapped_len);
    if (!file_vma) {
        sys_mmap_remove_vma(proc, base, mapped_len);
        return (uint32_t)-1;
    }
    file_vma->file_fd = fd;
    file_vma->file_length = length;

    for (uint32_t page = 0; page < pages; ++page) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            sys_mmap_remove_vma(proc, base, mapped_len);
            sys_munmap_range(base, page * PAGE_SIZE);
            return (uint32_t)-1;
        }
        memset(phys, 0, PAGE_SIZE);
        vmm_map_page(base + page * PAGE_SIZE, (uint32_t)phys, VMM_USER);
    }

    int saved_pos = vfs_seek(fd, 0, 1);
    if (saved_pos < 0) {
        sys_mmap_remove_vma(proc, base, mapped_len);
        sys_munmap_range(base, mapped_len);
        return (uint32_t)-1;
    }
    if (vfs_seek(fd, 0, 0) < 0) {
        vfs_seek(fd, saved_pos, 0);
        sys_mmap_remove_vma(proc, base, mapped_len);
        sys_munmap_range(base, mapped_len);
        return (uint32_t)-1;
    }

    uint8_t chunk[512];
    uint32_t copied = 0;
    while (copied < length) {
        uint32_t want = length - copied;
        if (want > sizeof(chunk))
            want = sizeof(chunk);
        int n = vfs_read(fd, chunk, want);
        if (n < 0) {
            vfs_seek(fd, saved_pos, 0);
            sys_mmap_remove_vma(proc, base, mapped_len);
            sys_munmap_range(base, mapped_len);
            return (uint32_t)-1;
        }
        if (n == 0)
            break;
        if (copy_to_user((void *)(uintptr_t)(base + copied), chunk, (uint32_t)n) < 0) {
            vfs_seek(fd, saved_pos, 0);
            sys_mmap_remove_vma(proc, base, mapped_len);
            sys_munmap_range(base, mapped_len);
            return (uint32_t)-1;
        }
        copied += (uint32_t)n;
    }
    vfs_seek(fd, saved_pos, 0);
    sys_mmap_apply_file_page_flags(base, pages, prot, flags);

    if (base + mapped_len > proc->mmap_end)
        proc->mmap_end = base + mapped_len;
    return base;
}

static uint32_t sys_munmap_user(uint32_t addr, uint32_t len)
{
    process_t *proc;
    uint32_t end;

    if (len == 0 || (addr & (PAGE_SIZE - 1u)) != 0)
        return (uint32_t)-1;

    len = page_align_up_u32(len);
    end = addr + len;
    if (end <= addr || addr < SYS_MMAP_BASE || end > SYS_MMAP_LIMIT)
        return (uint32_t)-1;

    proc = proc_find(proc_current_pid());
    if (!sys_mmap_range_is_mapped(proc, addr, len))
        return (uint32_t)-1;

    if (sys_munmap_sync_shared_file_range(proc, addr, len) < 0)
        return (uint32_t)-1;

    if (sys_mmap_remove_vma(proc, addr, len) < 0)
        return (uint32_t)-1;

    sys_munmap_range(addr, len);

    if (proc && end == proc->mmap_end)
        proc->mmap_end = addr;

    return 0;
}

static uint32_t sys_tls_set(uint32_t base)
{
    thread_t *thread = sched_get_current();
    if (!thread)
        return (uint32_t)-1;
    thread->tls_base = base;
    return 0;
}

static uint32_t sys_tls_get(void)
{
    thread_t *thread = sched_get_current();
    if (!thread)
        return 0;
    return thread->tls_base;
}

static uint32_t sys_chromium_memory_policy(void)
{
    /*
     * OpenOS is currently built as 32-bit i386 without an NX-capable page-table
     * policy. Chromium/V8 must default to jitless mode until executable memory
     * can enforce W^X at the MMU level.
     */
    return OPENOS_CHROMIUM_MEM_JITLESS_DEFAULT |
           OPENOS_CHROMIUM_MEM_EXEC_PROT_RESERVED;
}

static uint32_t sys_mprotect_user(uint32_t addr, uint32_t len, uint32_t prot)
{
    process_t *proc;
    uint32_t end;
    uint32_t page_flags;

    if (len == 0 || (addr & (PAGE_SIZE - 1u)) != 0)
        return (uint32_t)-1;

    len = page_align_up_u32(len);
    end = addr + len;
    if (end <= addr)
        return (uint32_t)-1;

    proc = proc_find(proc_current_pid());
    if (!proc)
        return (uint32_t)-1;

    if (addr >= SYS_MMAP_BASE && end <= SYS_MMAP_LIMIT) {
        if (!sys_mmap_find_vma(proc, addr, len))
            return (uint32_t)-1;
    } else if (!(addr >= proc->heap_start && end <= proc->heap_end)) {
        return (uint32_t)-1;
    }

    if ((prot & ~(uint32_t)0x7u) != 0)
        return (uint32_t)-1;
    if ((prot & PROCESS_MMAP_PROT_EXEC) != 0)
        return (uint32_t)-1;

    for (uint32_t va = addr; va < end; va += PAGE_SIZE) {
        uint32_t pte = vmm_get_mapping(va);
        if ((pte & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER))
            return (uint32_t)-1;
    }

    page_flags = PTE_PRESENT | PTE_USER;
    if (prot & 0x2u)
        page_flags |= PTE_RW;

    for (uint32_t va = addr; va < end; va += PAGE_SIZE)
        vmm_update_page_flags(va, page_flags);

    if (addr >= SYS_MMAP_BASE && end <= SYS_MMAP_LIMIT) {
        process_mmap_vma_t *vma = sys_mmap_find_vma(proc, addr, len);
        if (vma)
            vma->prot = prot;
    }

    return 0;
}

static uint32_t sys_brk_set(uint32_t new_end)
{
    process_t *proc = proc_find(proc_current_pid());
    uint32_t old_end;

    if (!proc || proc->heap_start == 0)
        return (uint32_t)-1;

    if (new_end == 0)
        return proc->heap_end;

    if (new_end < proc->heap_start || new_end >= SYS_MMAP_BASE)
        return (uint32_t)-1;

    old_end = proc->heap_end;
    if (new_end < old_end) {
        uint32_t va = page_align_up_u32(new_end);
        uint32_t map_end = page_align_up_u32(old_end);
        if (va < map_end)
            sys_munmap_range(va, map_end - va);
    }

    /* Demand paging: heap growth only extends metadata; pages are allocated on #PF. */
    proc->heap_end = new_end;
    return proc->heap_end;
}

static uint32_t sys_sbrk_delta(uint32_t increment)
{
    process_t *proc = proc_find(proc_current_pid());
    uint32_t old_end;
    uint32_t new_end;
    int32_t delta = (int32_t)increment;

    if (!proc)
        return (uint32_t)-1;

    old_end = proc->heap_end;
    if (delta >= 0) {
        if ((uint32_t)delta > (SYS_MMAP_BASE - old_end))
            return (uint32_t)-1;
        new_end = old_end + (uint32_t)delta;
    } else {
        uint32_t abs_delta = (uint32_t)(-delta);
        if (abs_delta > (old_end - proc->heap_start))
            return (uint32_t)-1;
        new_end = old_end - abs_delta;
    }

    if (sys_brk_set(new_end) == (uint32_t)-1)
        return (uint32_t)-1;
    return old_end;
}

static uint32_t syscall_return(uint32_t value)
{
    uint32_t pid = proc_current_pid();
    if (pid)
        proc_handle_pending_signals(pid);
    return value;
}

static int syscall_require_cap(uint32_t cap)
{
    if (proc_current_sandboxed())
        return -1;
    if (proc_current_has_cap(cap))
        return 0;
    return -1;
}

static int sys_dnslookup(uint32_t user_name, uint32_t user_ip)
{
    char name[128];
    uint32_t ip = 0;
    int i;

    if (!user_name || !user_ip) return -1;
    if (strncpy_from_user(name, (const char *)(uintptr_t)user_name, sizeof(name)) < 0) return -1;
    name[sizeof(name) - 1] = '\0';
    if (!name[0]) return -1;

    if (dns_query_a(name) < 0) return -1;
    for (i = 0; i < 250; i++) {
        uint32_t start_ms = sched_time_ms();
        net_poll();
        if (dns_get_state() == DNS_STATE_RESOLVED) {
            ip = dns_get_last_result();
            break;
        }
        if (dns_get_state() == DNS_STATE_FAILED) break;
        while (sched_time_ms() == start_ms)
            asm volatile ("pause");
    }

    if (!ip) {
        dns_mark_failed();
        vga_write("dns: lookup timed out or failed\n");
        dns_print_info();
        return -1;
    }
    if (copy_to_user((void *)(uintptr_t)user_ip, &ip, sizeof(ip)) < 0) return -1;
    return 0;
}

/* ============================================================
 * 系统调用分发
 * ============================================================ */
uint32_t syscall_dispatch(uint32_t num,
                          uint32_t a, uint32_t b, uint32_t c,
                          uint32_t d, uint32_t e)
{
    (void)e;
    uint32_t current_pid = proc_current_pid();
    if (current_pid)
        proc_handle_pending_signals(current_pid);
    switch (num) {
    case SYS_GETPID:
        return sys_getpid();

    case SYS_GETTID:
        return sys_gettid();

    case SYS_WRITE:
        return syscall_write_user_buffer((int)a, (const void *)b, c);

    case SYS_READ:
        return syscall_read_user_buffer((int)a, (void *)b, c);

    case SYS_EXIT:
        gui_user_cleanup_process((uint32_t)proc_current_pid());
        sys_exit((int)a);
        return 0;

    case SYS_SLEEP:
        thread_sleep(a);
        return 0;

    case SYS_UPTIME_MS:
        return sched_time_ms();

    case SYS_CLOCK_GETTIME:
        return (uint32_t)syscall_clock_gettime(a, (openos_timespec_t *)b);

    case SYS_NANOSLEEP:
        return (uint32_t)syscall_nanosleep((const openos_timespec_t *)a, (openos_timespec_t *)b);

    case SYS_FONT_QUERY:
        {
            openos_font_query_t query;
            font_cjk_resource_info_t cjk;
            font_text_metrics_t metrics;
            const font_renderer_t *font = font_get_default();
            if (!a || !user_ptr_valid((void *)a, sizeof(query), USERMEM_READ | USERMEM_WRITE))
                return (uint32_t)-1;
            if (copy_from_user(&query, (const void *)a, sizeof(query)) < 0)
                return (uint32_t)-1;
            query.text[sizeof(query.text) - 1] = 0;

            font_get_cjk_resource_info(&cjk);
            font_measure_text(font, query.text, &metrics);
            query.ascii_width = font_get_ascii_width(font);
            query.ascii_height = font_get_ascii_height(font);
            query.unicode_width = font_get_unicode_width();
            query.unicode_height = font_get_unicode_height();
            query.line_height = font_get_line_height(font);
            query.scale_percent = font_get_scale_percent();
            query.font_size = font_get_size();
            query.cjk_loaded = cjk.loaded ? 1u : 0u;
            query.cjk_glyph_count = cjk.glyph_count;
            query.cjk_width = cjk.width;
            query.cjk_height = cjk.height;
            query.codepoint_width = font_measure_codepoint_width(font, query.codepoint);
            query.text_width = metrics.width;
            query.text_height = metrics.height;
            query.text_lines = metrics.lines;
            if (copy_to_user((void *)a, &query, sizeof(query)) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_YIELD:
        sched_yield();
        return 0;

    case SYS_MALLOC:
        {
            void *p = pmm_alloc_page();
            return (uint32_t)p;
        }

    case SYS_FREE:
        pmm_free_page((void *)a);
        return 0;

    case SYS_MMAP:
        return sys_mmap_anonymous(a, b, c, d);

    case SYS_MUNMAP:
        return sys_munmap_user(a, b);

    case SYS_MMAP_FILE:
        return sys_mmap_file(a, b, c, d);

    case SYS_CHROMIUM_MEMORY_POLICY:
        return sys_chromium_memory_policy();

    case SYS_TLS_SET:
        return sys_tls_set(a);

    case SYS_TLS_GET:
        return sys_tls_get();

    case SYS_MPROTECT:
        return sys_mprotect_user(a, b, c);

    case SYS_BRK:
        return sys_brk_set(a);

    case SYS_SBRK:
        return sys_sbrk_delta(a);

    case SYS_THREAD_CREATE:
        return (uint32_t)sys_thread_create(a, b, c);

    case SYS_THREAD_CREATE_TLS:
        return (uint32_t)sys_thread_create_tls(a, b, c, d);

    case SYS_THREAD_EXIT:
        sys_thread_exit((int)a);
        return 0;

    case SYS_MUTEX_CREATE:
        return syscall_mutex_create();

    case SYS_MUTEX_LOCK:
        return syscall_mutex_lock(a);

    case SYS_MUTEX_UNLOCK:
        return syscall_mutex_unlock(a);

    case SYS_MUTEX_DESTROY:
        return syscall_mutex_destroy(a);

    case SYS_SEM_CREATE:
        return syscall_sem_create(a);

    case SYS_SEM_WAIT:
        return syscall_sem_wait(a);

    case SYS_SEM_POST:
        return syscall_sem_post(a);

    case SYS_SEM_DESTROY:
        return syscall_sem_destroy(a);

    case SYS_COND_CREATE:
        return syscall_cond_create();

    case SYS_COND_WAIT:
        return syscall_cond_wait(a, b);

    case SYS_COND_SIGNAL:
        return syscall_cond_signal(a);

    case SYS_COND_BROADCAST:
        return syscall_cond_broadcast(a);

    case SYS_COND_DESTROY:
        return syscall_cond_destroy(a);
    case SYS_FUTEX_WAIT:
        return syscall_futex_wait(a, b);
    case SYS_FUTEX_WAKE:
        return syscall_futex_wake(a, b);
    case SYS_FUTEX_WAIT_TIMEOUT:
        return syscall_futex_wait_timeout(a, b, c);
    case SYS_GETPRIORITY:
        return syscall_getpriority(a);
    case SYS_SETPRIORITY:
        return syscall_setpriority(a, (int)b);
    case SYS_NICE:
        return syscall_nice((int)a);

    case SYS_FORK:
        return sys_fork();

    case SYS_WAIT:
        {
            int status = 0;
            uint32_t ret;
            if (a && !user_ptr_valid((void *)a, sizeof(status), USERMEM_WRITE))
                return (uint32_t)-1;
            ret = sys_wait(a ? &status : NULL);
            if (a && copy_to_user((void *)a, &status, sizeof(status)) < 0)
                return (uint32_t)-1;
            return ret;
        }

    case SYS_WAITPID:
        {
            int status = 0;
            uint32_t ret;
            if (b && !user_ptr_valid((void *)b, sizeof(status), USERMEM_WRITE))
                return (uint32_t)-1;
            ret = sys_waitpid((int)a, b ? &status : NULL, (int)c);
            if (b && copy_to_user((void *)b, &status, sizeof(status)) < 0)
                return (uint32_t)-1;
            return ret;
        }

    case SYS_GETPPID:
        return sys_getppid();

    case SYS_OPEN:
        {
            char path[USERMEM_CSTR_MAX];
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)vfs_open(path, (int)b, (int)c);
        }

    case SYS_CLOSE:
        return (uint32_t)vfs_close((int)a);

    case SYS_READ_FD:
        return syscall_read_user_buffer((int)a, (void *)b, c);

    case SYS_WRITE_FD:
        return syscall_write_user_buffer((int)a, (const void *)b, c);

    case SYS_SEEK:
        return (uint32_t)vfs_seek((int)a, (int)b, (int)c);

    case SYS_DUP:
        return (uint32_t)vfs_dup((int)a);

    case SYS_DUP2:
        return (uint32_t)vfs_dup2((int)a, (int)b);

    case SYS_PIPE:
        {
            int pipefd[2];
            if (!a || !user_ptr_valid((void *)a, sizeof(pipefd), USERMEM_WRITE))
                return (uint32_t)-1;
            if (vfs_pipe(pipefd) < 0)
                return (uint32_t)-1;
            if (copy_to_user((void *)a, pipefd, sizeof(pipefd)) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_KILL:
        if (syscall_require_cap(OPENOS_CAP_KILL) < 0)
            return (uint32_t)-1;
        return syscall_return((uint32_t)sys_kill((int)a, (int)b));

    case SYS_ALARM:
        return syscall_return((uint32_t)sys_alarm((unsigned int)a));

    case SYS_MKDIR:
        {
            char path[USERMEM_CSTR_MAX];
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)vfs_mkdir(path, (int)b);
        }

    case SYS_UNLINK:
        {
            char path[USERMEM_CSTR_MAX];
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)vfs_unlink(path);
        }

    case SYS_RMDIR:
        {
            char path[USERMEM_CSTR_MAX];
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)vfs_rmdir(path);
        }

    case SYS_LINK:
        {
            char oldpath[USERMEM_CSTR_MAX];
            char newpath[USERMEM_CSTR_MAX];
            if (syscall_copy_user_path(oldpath, (const char *)a) < 0)
                return (uint32_t)-1;
            if (syscall_copy_user_path(newpath, (const char *)b) < 0)
                return (uint32_t)-1;
            return (uint32_t)vfs_link(oldpath, newpath);
        }

    case SYS_SYMLINK:
        {
            char target[USERMEM_CSTR_MAX];
            char linkpath[USERMEM_CSTR_MAX];
            if (syscall_copy_user_path(target, (const char *)a) < 0)
                return (uint32_t)-1;
            if (syscall_copy_user_path(linkpath, (const char *)b) < 0)
                return (uint32_t)-1;
            return (uint32_t)vfs_symlink(target, linkpath);
        }

    case SYS_READLINK:
        {
            char path[USERMEM_CSTR_MAX];
            char target[USERMEM_CSTR_MAX];
            int ret;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            if (c <= 0 || c > USERMEM_CSTR_MAX)
                return (uint32_t)-1;
            ret = vfs_readlink(path, target, (uint32_t)c);
            if (ret < 0)
                return (uint32_t)-1;
            if (copy_to_user((void *)b, target, (uint32_t)ret + 1) < 0)
                return (uint32_t)-1;
            return (uint32_t)ret;
        }

    case SYS_EXEC:
        {
            char path[USERMEM_CSTR_MAX];
            syscall_strvec_t argv;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            if (syscall_copy_user_strvec(&argv, (char *const *)b,
                                         SYSCALL_ARG_MAX, SYSCALL_ARG_LEN) < 0)
                return (uint32_t)-1;
            return (uint32_t)sys_exec(path, argv.ptrs);
        }

    case SYS_SPAWN:
        {
            char path[USERMEM_CSTR_MAX];
            syscall_strvec_t argv;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            if (syscall_copy_user_strvec(&argv, (char *const *)b,
                                         SYSCALL_ARG_MAX, SYSCALL_ARG_LEN) < 0)
                return (uint32_t)-1;
            return (uint32_t)spawn_user_process(path, argv.ptrs);
        }

    case SYS_EXEC_ENV:
        {
            char path[USERMEM_CSTR_MAX];
            syscall_strvec_t argv;
            syscall_strvec_t envp;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            if (syscall_copy_user_strvec(&argv, (char *const *)b,
                                         SYSCALL_ARG_MAX, SYSCALL_ARG_LEN) < 0)
                return (uint32_t)-1;
            if (syscall_copy_user_strvec(&envp, (char *const *)c,
                                         SYSCALL_ENV_MAX, SYSCALL_ENV_LEN) < 0)
                return (uint32_t)-1;
            return (uint32_t)sys_exec_env(path, argv.ptrs, envp.ptrs);
        }

    case SYS_SPAWN_ENV:
        {
            char path[USERMEM_CSTR_MAX];
            syscall_strvec_t argv;
            syscall_strvec_t envp;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            if (syscall_copy_user_strvec(&argv, (char *const *)b,
                                         SYSCALL_ARG_MAX, SYSCALL_ARG_LEN) < 0)
                return (uint32_t)-1;
            if (syscall_copy_user_strvec(&envp, (char *const *)c,
                                         SYSCALL_ENV_MAX, SYSCALL_ENV_LEN) < 0)
                return (uint32_t)-1;
            return (uint32_t)spawn_user_process_env(path, argv.ptrs, envp.ptrs);
        }

    case SYS_STAT:
        {
            char path[USERMEM_CSTR_MAX];
            inode_t st;
            openos_stat_t user_st;
            if (!b || !user_ptr_valid((void *)b, sizeof(user_st), USERMEM_WRITE))
                return (uint32_t)-1;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            if (vfs_stat(path, &st) < 0)
                return (uint32_t)-1;
            syscall_fill_user_stat(&user_st, &st);
            if (copy_to_user((void *)b, &user_st, sizeof(user_st)) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_FSTAT:
        {
            file_t *f;
            openos_stat_t user_st;
            if (!b || !user_ptr_valid((void *)b, sizeof(user_st), USERMEM_WRITE))
                return (uint32_t)-1;
            f = vfs_get_file((int)a);
            if (!f || !f->inode)
                return (uint32_t)-1;
            syscall_fill_user_stat(&user_st, f->inode);
            if (copy_to_user((void *)b, &user_st, sizeof(user_st)) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_STATFS:
        {
            char path[USERMEM_CSTR_MAX];
            inode_t st;
            openos_statfs_t user_st;
            if (!b || !user_ptr_valid((void *)b, sizeof(user_st), USERMEM_WRITE))
                return (uint32_t)-1;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            if (vfs_stat(path, &st) < 0)
                return (uint32_t)-1;
            syscall_fill_user_statfs(&user_st, &st);
            if (copy_to_user((void *)b, &user_st, sizeof(user_st)) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_FSTATFS:
        {
            file_t *f;
            openos_statfs_t user_st;
            if (!b || !user_ptr_valid((void *)b, sizeof(user_st), USERMEM_WRITE))
                return (uint32_t)-1;
            f = vfs_get_file((int)a);
            if (!f || !f->inode)
                return (uint32_t)-1;
            syscall_fill_user_statfs(&user_st, f->inode);
            if (copy_to_user((void *)b, &user_st, sizeof(user_st)) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_FCNTL:
        {
            file_t *f;
            f = vfs_get_file((int)a);
            if (!f)
                return (uint32_t)-1;
            if ((int)b == OPENOS_F_GETFL)
                return (uint32_t)f->flags;
            if ((int)b == OPENOS_F_SETFL) {
                f->flags = (f->flags & ~OPENOS_FCNTL_SETFL_MASK) |
                           ((int)c & OPENOS_FCNTL_SETFL_MASK);
                return 0;
            }
            return (uint32_t)-1;
        }

    case SYS_LSTAT:
        {
            char path[USERMEM_CSTR_MAX];
            inode_t st;
            openos_stat_t user_st;
            if (!b || !user_ptr_valid((void *)b, sizeof(user_st), USERMEM_WRITE))
                return (uint32_t)-1;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            /* 当前 VFS 尚未区分 symlink，lstat 先与 stat 等价。 */
            if (vfs_stat(path, &st) < 0)
                return (uint32_t)-1;
            syscall_fill_user_stat(&user_st, &st);
            if (copy_to_user((void *)b, &user_st, sizeof(user_st)) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_GETCWD:
        {
            char cwd[MAX_PATH];
            uint32_t len;
            if (!a || b == 0 || b > USERMEM_CSTR_MAX)
                return (uint32_t)-1;
            if (!user_ptr_valid((void *)a, b, USERMEM_WRITE))
                return (uint32_t)-1;
            if (vfs_getcwd(cwd, sizeof(cwd)) < 0)
                return (uint32_t)-1;
            len = (uint32_t)strlen(cwd) + 1;
            if (len > b)
                return (uint32_t)-1;
            if (copy_to_user((void *)a, cwd, len) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_CHDIR:
        {
            char path[USERMEM_CSTR_MAX];
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)vfs_chdir(path);
        }

    case SYS_CHMOD:
        {
            char path[USERMEM_CSTR_MAX];
            if (syscall_require_cap(OPENOS_CAP_SYS_ADMIN) < 0)
                return (uint32_t)-1;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)vfs_chmod(path, (uint32_t)b);
        }

    case SYS_CHOWN:
        {
            char path[USERMEM_CSTR_MAX];
            if (syscall_require_cap(OPENOS_CAP_SYS_ADMIN) < 0)
                return (uint32_t)-1;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)vfs_chown(path, (uint32_t)b, (uint32_t)c);
        }

    case SYS_GETUID:
        return proc_current_uid();

    case SYS_SETUID:
        if (a != proc_current_uid() && syscall_require_cap(OPENOS_CAP_SETUID) < 0)
            return (uint32_t)-1;
        return (uint32_t)proc_set_current_uid((uint32_t)a);

    case SYS_GETGID:
        return proc_current_gid();

    case SYS_SETGID:
        if (a != proc_current_gid() && syscall_require_cap(OPENOS_CAP_SETGID) < 0)
            return (uint32_t)-1;
        return (uint32_t)proc_set_current_gid((uint32_t)a);

    case SYS_GETPWUID:
        return (uint32_t)syscall_getpwuid((uint32_t)a, (openos_user_t *)b);

    case SYS_GETGRGID:
        return (uint32_t)syscall_getgrgid((uint32_t)a, (openos_group_t *)b);

    case SYS_CAPGET:
        return proc_current_caps();

    case SYS_CAPSET:
        if (((uint32_t)a & ~proc_current_caps()) != 0 && syscall_require_cap(OPENOS_CAP_SYS_ADMIN) < 0)
            return (uint32_t)-1;
        return (uint32_t)proc_set_current_caps((uint32_t)a);

    case SYS_SANDBOX_GET:
        return (uint32_t)proc_current_sandboxed();

    case SYS_SANDBOX_SET:
        return (uint32_t)proc_set_current_sandbox((uint32_t)a);

    case SYS_POLL:
        return syscall_poll((openos_pollfd_t *)a, b, c);

    case SYS_SELECT:
        return syscall_select(a, (uint32_t *)b, (uint32_t *)c, (uint32_t *)d, e);

    case SYS_SOCKET:
        if (syscall_require_cap(OPENOS_CAP_NET_ADMIN) < 0)
            return (uint32_t)-1;
        return (uint32_t)socket_create_fd((int)a, (int)b, (int)c);

    case SYS_SOCKETPAIR:
        {
            int sv[2];
            if (syscall_require_cap(OPENOS_CAP_NET_ADMIN) < 0)
                return (uint32_t)-1;
            if (!d)
                return (uint32_t)-1;
            if (socketpair_create_fds((int)a, (int)b, (int)c, sv) < 0)
                return (uint32_t)-1;
            if (copy_to_user((void *)d, sv, sizeof(sv)) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_BIND:
        {
            openos_sockaddr_in_t addr;
            if (!b || c < sizeof(openos_sockaddr_in_t) || c > sizeof(openos_sockaddr_in_t))
                return (uint32_t)-1;
            if (copy_from_user(&addr, (const void *)b, sizeof(addr)) < 0)
                return (uint32_t)-1;
            return (uint32_t)socket_bind_fd((int)a, (const openos_sockaddr_t *)&addr, sizeof(addr));
        }

    case SYS_LISTEN:
        return (uint32_t)socket_listen_fd((int)a, (int)b);

    case SYS_ACCEPT:
        {
            openos_sockaddr_in_t addr;
            uint32_t addrlen = 0;
            int ret;
            if ((b && !c) || (!b && c))
                return (uint32_t)-1;
            if (c && copy_from_user(&addrlen, (const void *)c, sizeof(addrlen)) < 0)
                return (uint32_t)-1;
            ret = socket_accept_fd((int)a, b ? (openos_sockaddr_t *)&addr : 0, c ? &addrlen : 0);
            if (ret < 0)
                return (uint32_t)ret;
            if (b && copy_to_user((void *)b, &addr, sizeof(addr)) < 0)
                return (uint32_t)-1;
            if (c && copy_to_user((void *)c, &addrlen, sizeof(addrlen)) < 0)
                return (uint32_t)-1;
            return (uint32_t)ret;
        }

    case SYS_CONNECT:
        {
            openos_sockaddr_in_t addr;
            if (!b || c < sizeof(openos_sockaddr_in_t) || c > sizeof(openos_sockaddr_in_t))
                return (uint32_t)-1;
            if (copy_from_user(&addr, (const void *)b, sizeof(addr)) < 0)
                return (uint32_t)-1;
            return (uint32_t)socket_connect_fd((int)a, (const openos_sockaddr_t *)&addr, sizeof(addr));
        }

    case SYS_SEND:
        {
            void *kbuf;
            int ret;
            if (!b || c == 0 || c > SYSCALL_SEND_MAX)
                return (uint32_t)-1;
            kbuf = pmm_alloc_page();
            if (!kbuf)
                return (uint32_t)-1;
            if (copy_from_user(kbuf, (const void *)b, c) < 0) {
                pmm_free_page(kbuf);
                return (uint32_t)-1;
            }
            ret = socket_send_fd((int)a, (const uint8_t *)kbuf, c, (int)d);
            pmm_free_page(kbuf);
            return (uint32_t)ret;
        }

    case SYS_RECV:
        {
            void *kbuf;
            int ret;
            if (!b || c == 0 || c > SYSCALL_RECV_MAX)
                return (uint32_t)-1;
            kbuf = pmm_alloc_page();
            if (!kbuf)
                return (uint32_t)-1;
            ret = socket_recv_fd((int)a, (uint8_t *)kbuf, c, (int)d);
            if (ret > 0 && copy_to_user((void *)b, kbuf, (uint32_t)ret) < 0) {
                pmm_free_page(kbuf);
                return (uint32_t)-1;
            }
            pmm_free_page(kbuf);
            return (uint32_t)ret;
        }

    case SYS_SHUTDOWN:
        return (uint32_t)socket_shutdown_fd((int)a, (int)b);

    case SYS_SETSOCKOPT:
        {
            uint8_t kopt[sizeof(openos_timeval_t)];
            if (!d || e == 0 || e > sizeof(kopt))
                return (uint32_t)-1;
            if (copy_from_user(kopt, (const void *)d, e) < 0)
                return (uint32_t)-1;
            return (uint32_t)socket_setsockopt_fd((int)a, (int)b, (int)c, kopt, e);
        }

    case SYS_GETSOCKOPT:
        {
            uint8_t kopt[sizeof(openos_timeval_t)];
            uint32_t koptlen;
            if (!d || !e)
                return (uint32_t)-1;
            if (copy_from_user(&koptlen, (const void *)e, sizeof(koptlen)) < 0)
                return (uint32_t)-1;
            if (koptlen == 0 || koptlen > sizeof(kopt))
                return (uint32_t)-1;
            memset(kopt, 0, sizeof(kopt));
            if (socket_getsockopt_fd((int)a, (int)b, (int)c, kopt, &koptlen) < 0)
                return (uint32_t)-1;
            if (copy_to_user((void *)d, kopt, koptlen) < 0)
                return (uint32_t)-1;
            if (copy_to_user((void *)e, &koptlen, sizeof(koptlen)) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_SENDTO:
        {
            void *kbuf;
            openos_sockaddr_in_t addr;
            int ret;
            if (!b || !e || c == 0 || c > SYSCALL_SEND_MAX)
                return (uint32_t)-1;
            if (copy_from_user(&addr, (const void *)e, sizeof(addr)) < 0)
                return (uint32_t)-1;
            kbuf = pmm_alloc_page();
            if (!kbuf)
                return (uint32_t)-1;
            if (copy_from_user(kbuf, (const void *)b, c) < 0) {
                pmm_free_page(kbuf);
                return (uint32_t)-1;
            }
            ret = socket_sendto_fd((int)a, (const uint8_t *)kbuf, c, (int)d,
                                   (const openos_sockaddr_t *)&addr, sizeof(addr));
            pmm_free_page(kbuf);
            return (uint32_t)ret;
        }

    case SYS_RECVFROM:
        {
            void *kbuf;
            openos_sockaddr_in_t addr;
            uint32_t addrlen = sizeof(addr);
            int ret;
            if (!b || c == 0 || c > SYSCALL_RECV_MAX)
                return (uint32_t)-1;
            kbuf = pmm_alloc_page();
            if (!kbuf)
                return (uint32_t)-1;
            ret = socket_recvfrom_fd((int)a, (uint8_t *)kbuf, c, (int)d,
                                     e ? (openos_sockaddr_t *)&addr : NULL,
                                     e ? &addrlen : NULL);
            if (ret > 0 && copy_to_user((void *)b, kbuf, (uint32_t)ret) < 0) {
                pmm_free_page(kbuf);
                return (uint32_t)-1;
            }
            if (ret >= 0 && e && copy_to_user((void *)e, &addr, sizeof(addr)) < 0) {
                pmm_free_page(kbuf);
                return (uint32_t)-1;
            }
            pmm_free_page(kbuf);
            return (uint32_t)ret;
        }

    case SYS_NETINFO:
        {
            net_diag_stats_t stats;
            net_device_info_t devinfo;
            openos_netinfo_t info;
            if (!a || !user_ptr_valid((void *)a, sizeof(info), USERMEM_WRITE))
                return (uint32_t)-1;
            net_poll();
            if (net_get_diag_stats(&stats) < 0)
                return (uint32_t)-1;
            memset(&info, 0, sizeof(info));
            strncpy(info.name, stats.name, sizeof(info.name) - 1);
            memcpy(info.mac, stats.mac, sizeof(info.mac));
            info.ip = stats.ip;
            info.netmask = stats.netmask;
            info.gateway = stats.gateway;
            if (net_get_device_info_by_name(stats.name, &devinfo) == 0) {
                info.dns = devinfo.dns;
                info.flags = devinfo.flags;
                info.config_mode = devinfo.config_mode;
            }
            info.rx_packets = stats.rx_packets;
            info.tx_packets = stats.tx_packets;
            info.rx_dropped = stats.rx_dropped;
            info.tx_dropped = stats.tx_dropped;
            info.arp_entries = stats.arp_entries;
            info.udp_bindings = stats.udp_bindings;
            info.tcp_listeners = stats.tcp_listeners;
            info.tcp_connections = stats.tcp_connections;
            info.icmp_echo_requests = stats.icmp_echo_requests;
            info.icmp_echo_replies = stats.icmp_echo_replies;
            info.last_ipv4_src = stats.last_ipv4_src;
            info.last_ipv4_dst = stats.last_ipv4_dst;
            info.last_ipv4_protocol = stats.last_ipv4_protocol;
            info.last_icmp_src = stats.last_icmp_src;
            info.last_icmp_type = stats.last_icmp_type;
            info.last_icmp_code = stats.last_icmp_code;
            info.ipv4_drop_short = stats.ipv4_drop_short;
            info.ipv4_drop_version = stats.ipv4_drop_version;
            info.ipv4_drop_ihl = stats.ipv4_drop_ihl;
            info.ipv4_drop_len = stats.ipv4_drop_len;
            info.ipv4_drop_checksum = stats.ipv4_drop_checksum;
            info.ipv4_drop_dst = stats.ipv4_drop_dst;
            info.last_ipv4_tx_src = stats.last_ipv4_tx_src;
            info.last_ipv4_tx_dst = stats.last_ipv4_tx_dst;
            info.last_ipv4_tx_next_hop = stats.last_ipv4_tx_next_hop;
            info.last_ipv4_tx_protocol = stats.last_ipv4_tx_protocol;
            info.last_ipv4_tx_len = stats.last_ipv4_tx_len;
            info.last_ipv4_tx_result = stats.last_ipv4_tx_result;
            info.last_ping_dst = stats.last_ping_dst;
            info.last_ping_id = stats.last_ping_id;
            info.last_ping_seq = stats.last_ping_seq;
            info.last_ping_send_result = stats.last_ping_send_result;
            if (copy_to_user((void *)a, &info, sizeof(info)) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_PING:
        if (syscall_require_cap(OPENOS_CAP_NET_ADMIN) < 0)
            return (uint32_t)-1;
        return (uint32_t)net_ping_ipv4((uint32_t)a);

    case SYS_DNSLOOKUP:
        return (uint32_t)sys_dnslookup((uint32_t)a, (uint32_t)b);

    case SYS_NETCONFIG:
        if (syscall_require_cap(OPENOS_CAP_NET_ADMIN) < 0)
            return (uint32_t)-1;
        return (uint32_t)net_config_ipv4((uint32_t)a, (uint32_t)b, (uint32_t)c, (uint32_t)d);

    case SYS_NETDEVCTL:
        {
            char dev_name[16];
            if (!a || !user_ptr_valid((const void *)a, 1, USERMEM_READ))
                return (uint32_t)-1;
            if (strncpy_from_user(dev_name, (const char *)a, sizeof(dev_name)) < 0)
                return (uint32_t)-1;
            if (syscall_require_cap(OPENOS_CAP_NET_ADMIN) < 0)
                return (uint32_t)-1;
            if ((uint32_t)b == NETDEV_CTL_SET_UP)
                return (uint32_t)net_set_device_admin_up(dev_name, 1);
            if ((uint32_t)b == NETDEV_CTL_SET_DOWN)
                return (uint32_t)net_set_device_admin_up(dev_name, 0);
            if ((uint32_t)b == NETDEV_CTL_DHCP_START)
                return (uint32_t)dhcp_start();
            if ((uint32_t)b == NETDEV_CTL_DHCP_RENEW)
                return (uint32_t)dhcp_renew();
            if ((uint32_t)b == NETDEV_CTL_DHCP_RELEASE)
                return (uint32_t)dhcp_release();
            if ((uint32_t)b == NETDEV_CTL_REFRESH)
                return (uint32_t)net_refresh_device_status(dev_name);
            return (uint32_t)-1;
        }

    case SYS_FIREWALL:
        {
            net_firewall_rule_t rule;
            if ((uint32_t)a == NET_FW_OP_GET) {
                if (!c) return (uint32_t)-1;
                if (net_firewall_get((uint32_t)b, &rule) < 0) return (uint32_t)-1;
                if (copy_to_user((void *)c, &rule, sizeof(rule)) < 0) return (uint32_t)-1;
                return 0;
            }
            if (syscall_require_cap(OPENOS_CAP_NET_ADMIN) < 0) return (uint32_t)-1;
            if ((uint32_t)a == NET_FW_OP_ADD) {
                if (!c) return (uint32_t)-1;
                if (copy_from_user(&rule, (const void *)c, sizeof(rule)) < 0) return (uint32_t)-1;
                return (uint32_t)net_firewall_add(&rule);
            }
            if ((uint32_t)a == NET_FW_OP_DELETE) {
                return (uint32_t)net_firewall_delete((uint32_t)b);
            }
            if ((uint32_t)a == NET_FW_OP_CLEAR) {
                net_firewall_clear();
                return 0;
            }
            return (uint32_t)-1;
        }

    case SYS_MQ_CREATE:
        return syscall_mq_create();

    case SYS_MQ_SEND:
        return syscall_mq_send((uint32_t)a, (const void *)b, (uint32_t)c);

    case SYS_MQ_RECV:
        return syscall_mq_recv((uint32_t)a, (void *)b, (uint32_t)c);

    case SYS_MQ_DESTROY:
        return syscall_mq_destroy((uint32_t)a);

    case SYS_SHM_CREATE:
        return syscall_shm_create();

    case SYS_SHM_MAP:
        return syscall_shm_map((uint32_t)a);

    case SYS_SHM_DESTROY:
        return syscall_shm_destroy((uint32_t)a);

    case SYS_SHM_INFO:
        return syscall_shm_info((uint32_t)a, (uint32_t)b);

    case SYS_EVENTFD_CREATE:
        return syscall_eventfd_create((uint32_t)a);

    case SYS_EVENTFD_WRITE:
        return syscall_eventfd_write((uint32_t)a, (uint32_t)b);

    case SYS_EVENTFD_READ:
        return syscall_eventfd_read((uint32_t)a, (uint32_t)b);

    case SYS_EVENTFD_DESTROY:
        return syscall_eventfd_destroy((uint32_t)a);

    case SYS_FSYNC:
        return (uint32_t)vfs_fsync((int)a);

    case SYS_GETDENTS:
        return syscall_getdents(a, b, c);

    case SYS_CLIPBOARD_SET:
        return (uint32_t)syscall_clipboard_set((const char *)a);

    case SYS_CLIPBOARD_GET:
        return (uint32_t)syscall_clipboard_get((char *)a, (uint32_t)b);

    case SYS_READDIR:
        {
            char path[USERMEM_CSTR_MAX];
            dentry_t *de;
            openos_dirent_t user_de;
            if (!c || !user_ptr_valid((void *)c, sizeof(user_de), USERMEM_WRITE))
                return (uint32_t)-1;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            de = vfs_readdir(path, (int)b);
            if (!de || !de->inode)
                return 0;
            user_de.ino = de->inode->ino;
            user_de.mode = de->inode->mode;
            user_de.size = de->inode->size;
            strncpy(user_de.name, de->name, sizeof(user_de.name) - 1);
            user_de.name[sizeof(user_de.name) - 1] = 0;
            if (copy_to_user((void *)c, &user_de, sizeof(user_de)) < 0)
                return (uint32_t)-1;
            return 1;
        }

    case SYS_AI_REQUEST:
        return sys_ai_request((uint32_t)a);

    case SYS_GUI_CREATE_WINDOW:
        {
            char title[64];
            if (strncpy_from_user(title, (const char *)a, sizeof(title)) < 0)
                return (uint32_t)-1;
            title[sizeof(title) - 1] = 0;
            return (uint32_t)gui_user_create_window(title, (int)b, (int)c, (int)d, (int)e, 0);
        }

    case SYS_GUI_DESTROY_WINDOW:
        return (uint32_t)gui_user_destroy_window((uint32_t)a);

    case SYS_GUI_ADD_LABEL:
        {
            gui_user_widget_request_t req;
            if (!a || !user_ptr_valid((void *)a, sizeof(req), USERMEM_READ))
                return (uint32_t)-1;
            if (copy_from_user(&req, (const void *)a, sizeof(req)) < 0)
                return (uint32_t)-1;
            req.text[sizeof(req.text) - 1] = 0;
            return (uint32_t)gui_user_add_label(req.window_id, req.x, req.y, req.w, req.h, req.text);
        }

    case SYS_GUI_ADD_BUTTON:
        {
            gui_user_widget_request_t req;
            if (!a || !user_ptr_valid((void *)a, sizeof(req), USERMEM_READ))
                return (uint32_t)-1;
            if (copy_from_user(&req, (const void *)a, sizeof(req)) < 0)
                return (uint32_t)-1;
            req.text[sizeof(req.text) - 1] = 0;
            return (uint32_t)gui_user_add_button(req.window_id, req.x, req.y, req.w, req.h, req.text);
        }

    case SYS_GUI_POLL_EVENT:
        {
            gui_user_event_t event;
            int rc;
            if (!a || !user_ptr_valid((void *)a, sizeof(event), USERMEM_WRITE))
                return (uint32_t)-1;
            rc = gui_user_poll_event(&event);
            if (rc < 0)
                return (uint32_t)-1;
            if (copy_to_user((void *)a, &event, sizeof(event)) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_GUI_SET_TEXT:
        {
            gui_user_widget_request_t req;
            if (!a || !user_ptr_valid((void *)a, sizeof(req), USERMEM_READ))
                return (uint32_t)-1;
            if (copy_from_user(&req, (const void *)a, sizeof(req)) < 0)
                return (uint32_t)-1;
            req.text[sizeof(req.text) - 1] = 0;
            return (uint32_t)gui_user_set_text(req.window_id, req.widget_id, req.text);
        }

    case SYS_GUI_DRAW:
        {
            gui_user_draw_request_t req;
            uint32_t *pixels = 0;
            uint32_t pixel_bytes = 0;
            int rc;
            if (!a || !user_ptr_valid((void *)a, sizeof(req), USERMEM_READ))
                return (uint32_t)-1;
            if (copy_from_user(&req, (const void *)a, sizeof(req)) < 0)
                return (uint32_t)-1;
            req.text[sizeof(req.text) - 1] = 0;

            if (req.op == GUI_USER_DRAW_BLIT_RGBA32) {
                if (req.w <= 0 || req.h <= 0 || req.w > 256 || req.h > 256)
                    return (uint32_t)-1;
                pixel_bytes = (uint32_t)req.w * (uint32_t)req.h * sizeof(uint32_t);
                if (!req.pixels_user_ptr || !user_ptr_valid((void *)req.pixels_user_ptr, pixel_bytes, USERMEM_READ))
                    return (uint32_t)-1;
                pixels = (uint32_t *)kmalloc(pixel_bytes);
                if (!pixels)
                    return (uint32_t)-1;
                if (copy_from_user(pixels, (const void *)req.pixels_user_ptr, pixel_bytes) < 0) {
                    kfree(pixels);
                    return (uint32_t)-1;
                }
                req.pixels_user_ptr = (uint32_t)pixels;
            }

            rc = gui_user_draw(&req);
            if (pixels)
                kfree(pixels);
            return (uint32_t)rc;
        }

    case SYS_GUI_RESIZE_WINDOW:
        return (uint32_t)gui_user_resize_window((uint32_t)a, (int)b, (int)c);

    case SYS_GUI_GET_WINDOW_INFO:
        {
            gui_user_window_info_t info;
            if (!b || !user_ptr_valid((void *)b, sizeof(info), USERMEM_WRITE))
                return (uint32_t)-1;
            if (gui_user_get_window_info((uint32_t)a, &info) < 0)
                return (uint32_t)-1;
            if (copy_to_user((void *)b, &info, sizeof(info)) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_GUI_GET_DISPLAY_INFO:
        {
            gui_user_display_info_t info;
            if (!a || !user_ptr_valid((void *)a, sizeof(info), USERMEM_WRITE))
                return (uint32_t)-1;
            if (gui_user_get_display_info(&info) < 0)
                return (uint32_t)-1;
            if (copy_to_user((void *)a, &info, sizeof(info)) < 0)
                return (uint32_t)-1;
            return 0;
        }

    default:
        return 0xFFFFFFFF;
    }
}

/* ============================================================
 * 初始化系统调用
 * ============================================================ */
void syscall_init(void)
{
    /* IDT 中断 0x80 设置在 idt.c 中 */
    /* 汇编入口在 isr.asm 中作为 sysenter / int 0x80 处理 */
    vga_write_str("[SYSCALL] initialized\n", 0x0A);
}
