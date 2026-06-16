/* ============================================================
 * openos - 系统调用实现
 * ============================================================ */

#include "../include/syscall.h"
#include "../include/process.h"
#include "../include/pmm.h"
#include "../include/serial.h"
#include "../include/vga.h"
#include "../include/input_buffer.h"
#include "../include/usermem.h"
#include "../include/vmm.h"
#include "../proc/process.h"
#include "../fs/vfs.h"
#include "../include/string.h"
#include "../include/blockdev.h"
#include "../net/socket.h"
#include <stddef.h>  /* NULL */

/* VGA */
#define VGA ((volatile uint16_t *)0xB8000)

#define SYSCALL_IO_CHUNK 256u

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SYSCALL_MAX_MUTEXES 64u
#define SYSCALL_MAX_SEMAPHORES 64u
#define SYSCALL_MAX_CONDS 64u
#define SYSCALL_MAX_FUTEX_WAITERS 128u

#define MUTEX_UNUSED 0u
#define MUTEX_READY  1u
#define SEM_UNUSED   0u
#define SEM_READY    1u
#define COND_UNUSED  0u
#define COND_READY   1u
#define FUTEX_WAIT_ACTIVE 1u

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

static syscall_mutex_t syscall_mutexes[SYSCALL_MAX_MUTEXES];
static syscall_sem_t syscall_sems[SYSCALL_MAX_SEMAPHORES];
static syscall_cond_t syscall_conds[SYSCALL_MAX_CONDS];
static syscall_futex_waiter_t syscall_futex_waiters[SYSCALL_MAX_FUTEX_WAITERS];

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
    if (!thread)
        return (uint32_t)-1;
    return (sched_set_thread_priority(thread, nice_to_priority(nice_value)) == 0) ? 0u : (uint32_t)-1;
}

static uint32_t syscall_nice(int inc)
{
    thread_t *current = sched_get_current();
    int next_nice;
    if (!current)
        return (uint32_t)NICE_ERROR;
    next_nice = priority_to_nice(current->priority) + inc;
    if (next_nice < NICE_MIN) next_nice = NICE_MIN;
    if (next_nice > NICE_MAX) next_nice = NICE_MAX;
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

static uint32_t syscall_futex_wait(uint32_t uaddr, uint32_t expected)
{
    uint32_t current_value = 0;
    thread_t *current = sched_get_current();
    uint32_t pid = proc_current_pid();
    syscall_futex_waiter_t *slot = NULL;

    if (!current || uaddr == 0 || (uaddr & 0x3u) != 0)
        return (uint32_t)-1;
    if (copy_from_user(&current_value, (const void *)uaddr, sizeof(current_value)) < 0)
        return (uint32_t)-1;
    if (current_value != expected)
        return 1;

    for (uint32_t i = 0; i < SYSCALL_MAX_FUTEX_WAITERS; i++) {
        if (syscall_futex_waiters[i].used != FUTEX_WAIT_ACTIVE) {
            slot = &syscall_futex_waiters[i];
            break;
        }
    }
    if (!slot)
        return (uint32_t)-1;

    slot->used = FUTEX_WAIT_ACTIVE;
    slot->pid = pid;
    slot->uaddr = uaddr;
    slot->thread = current;

    current->state = PROC_BLOCKED;
    sched_yield();

    syscall_futex_remove_waiter(current);
    return 0;
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

    if ((fd == STDOUT_FILENO || fd == STDERR_FILENO) && !vfs_get_file(fd)) {
        serial_write(fd == STDERR_FILENO ? "[USER-ERR] " : "[USER] ");
    }

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

static uint32_t syscall_poll(openos_pollfd_t *user_fds, uint32_t nfds, uint32_t timeout_ms)
{
    openos_pollfd_t pfd;
    uint32_t ready = 0;

    (void)timeout_ms;
    if (nfds > 64)
        return (uint32_t)-1;
    if (nfds > 0 && (!user_fds || !user_ptr_valid(user_fds, nfds * sizeof(openos_pollfd_t), USERMEM_READ | USERMEM_WRITE)))
        return (uint32_t)-1;

    for (uint32_t i = 0; i < nfds; i++) {
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
    return ready;
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

static void syscall_fill_user_stat(openos_stat_t *user_st, const inode_t *st)
{
    user_st->ino = st->ino;
    user_st->mode = st->mode;
    user_st->size = st->size;
    user_st->nlinks = st->nlinks;
    user_st->fs_type = st->fs_type;
    user_st->uid = st->uid;
    user_st->gid = st->gid;
}

#define SYS_MMAP_BASE  0x50000000u
#define SYS_MMAP_LIMIT 0x70000000u
#define SYS_MMAP_MAX_REQUEST (16u * 1024u * 1024u)

static uint32_t page_align_up_u32(uint32_t value)
{
    return (value + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
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

static uint32_t sys_mmap_anonymous(uint32_t addr, uint32_t len, uint32_t flags)
{
    (void)flags;
    process_t *proc;
    uint32_t start;

    if (addr != 0 || len == 0 || len > SYS_MMAP_MAX_REQUEST)
        return (uint32_t)-1;

    len = page_align_up_u32(len);
    if (len == 0)
        return (uint32_t)-1;

    proc = proc_find(proc_current_pid());
    if (!proc)
        return (uint32_t)-1;

    if (proc->mmap_base == 0 || proc->mmap_end < SYS_MMAP_BASE || proc->mmap_end >= SYS_MMAP_LIMIT) {
        proc->mmap_base = SYS_MMAP_BASE;
        proc->mmap_end = SYS_MMAP_BASE;
    }

    start = page_align_up_u32(proc->mmap_end);
    if (start < SYS_MMAP_BASE || start > SYS_MMAP_LIMIT || len > (SYS_MMAP_LIMIT - start))
        return (uint32_t)-1;

    /* Demand paging: reserve virtual range only. Physical pages are allocated on #PF. */
    proc->mmap_end = start + len;
    return start;
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

    sys_munmap_range(addr, len);

    proc = proc_find(proc_current_pid());
    if (proc && end == proc->mmap_end)
        proc->mmap_end = addr;

    return 0;
}

static uint32_t page_align_down_u32(uint32_t value)
{
    return value & ~(PAGE_SIZE - 1u);
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

/* ============================================================
 * 系统调用分发
 * ============================================================ */
uint32_t syscall_dispatch(uint32_t num,
                          uint32_t a, uint32_t b, uint32_t c,
                          uint32_t d, uint32_t e)
{
    (void)d; (void)e;
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
        sys_exit((int)a);
        return 0;

    case SYS_SLEEP:
        thread_sleep(a);
        return 0;

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
        return sys_mmap_anonymous(a, b, c);

    case SYS_MUNMAP:
        return sys_munmap_user(a, b);

    case SYS_BRK:
        return sys_brk_set(a);

    case SYS_SBRK:
        return sys_sbrk_delta(a);

    case SYS_THREAD_CREATE:
        return (uint32_t)sys_thread_create(a, b, c);

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
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)vfs_chmod(path, (uint32_t)b);
        }

    case SYS_CHOWN:
        {
            char path[USERMEM_CSTR_MAX];
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)vfs_chown(path, (uint32_t)b, (uint32_t)c);
        }

    case SYS_GETUID:
        return proc_current_uid();

    case SYS_SETUID:
        return (uint32_t)proc_set_current_uid((uint32_t)a);

    case SYS_GETGID:
        return proc_current_gid();

    case SYS_SETGID:
        return (uint32_t)proc_set_current_gid((uint32_t)a);

    case SYS_POLL:
        return syscall_poll((openos_pollfd_t *)a, b, c);

    case SYS_SELECT:
        return syscall_select(a, (uint32_t *)b, (uint32_t *)c, (uint32_t *)d, e);

    case SYS_SOCKET:
        return (uint32_t)socket_create_fd((int)a, (int)b, (int)c);

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

    case SYS_FSYNC:
        {
            file_t *f = vfs_get_file((int)a);
            if (!f || !f->inode) return (uint32_t)-9; /* EBADF */
            blockdev_t *dev = 0;

            if (f->inode->fs_type == 0xEF530004u) {
                void *node = f->inode->fs_data;
                if (node) dev = *(blockdev_t **)((uint8_t *)node + 8);
            } else if (f->inode->fs_type == 0x50465300u) {
                void *node = f->inode->fs_data;
                if (node) dev = *(blockdev_t **)((uint8_t *)node + 4);
            } else if (f->inode->fs_type == 0x46415400u) {
                void *node = f->inode->fs_data;
                if (node) dev = *(blockdev_t **)((uint8_t *)node + 4);
            }

            if (!dev) return (uint32_t)-22; /* EINVAL */
            return (uint32_t)blockdev_flush(dev);
        }

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
