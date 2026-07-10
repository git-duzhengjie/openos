/*
 * Architecture-neutral syscall dispatcher for x86_64.
 *
 * This file isolates "which syscall number does what" from the two assembly
 * entry paths (int 0x80 compat trap and the native syscall instruction).
 *
 * Syscall numbers follow src/kernel/include/syscall.h (the canonical OpenOS
 * table that the i386 port has been using all along). The x86_64 port is
 * still in bring-up: most syscalls return ENOSYS until their backing kernel
 * subsystem is ported to x86_64. This file is the single place to add a
 * real backend once a subsystem becomes available.
 *
 * Calling convention: all six arguments are passed as uint64_t. The two
 * assembly entry layers are responsible for loading them from the right
 * registers (ebx/ecx/edx/esi/edi for int 0x80, rdi/rsi/rdx/r10/r8/r9 for
 * the syscall instruction).
 */

#include "../include/syscall_dispatch64.h"

#include <stddef.h>
#include <stdint.h>

#include "../include/early_console64.h"
#include "../include/elf64_loader.h"
#include "../include/elf64_lazy.h" /* M5.1d: 惰性绑定 dl_resolve */
#include "../include/address_space64.h"
#include "../include/fdtable64.h"
#include "../include/heap64.h"
#include "../include/initrd64.h"
#include "../include/net64.h"
#include "../include/syscall_net64.h" /* split-out network syscall backends */
#include "../include/proc64.h"
#include "../include/sched64.h"
#include "../include/gdt64.h"      /* M5.2b: user CS/SS selectors for clone launch */
#include "../include/tsc64.h"
#include "../include/usermode64.h"
#include "../include/vfs64.h"
#include "../include/percpu64.h"
#include "../include/sfdtable64.h" /* M4.1a: syscall fd -> vfs fd indirection */
#include "../include/pipe64.h" /* M4.1c: anonymous pipe pool for pipe/dup/dup2 */
#include "../include/fifo64.h" /* M4.3b: named pipes (FIFO) */
#include "../include/shm64.h" /* M4.3c: System V shared memory segments */
#include "../include/futex64.h" /* M5.2c: fast userspace mutex for pthread */
#include "../include/tty64.h" /* M4.4: pseudo-terminal line discipline */
#include "../include/lapic64.h" /* γ.4 S3: cross-CPU wake IPI for fork/wait */
#include "../include/smp64.h"   /* γ.4 S3: BSP apic_id for wake IPI target */
#include "vmem64.h" /* M4.1b: anonymous vmem for mmap/munmap/mprotect/brk/sbrk */
#include "../../../kernel/core/fs/vfs.h" /* RAMFS-backed vfs_open/read/stat */
#include "../../../kernel/net/net.h" /* real TCP/IP stack: ping/dns/netinfo/dev-ctl */
#include "syscall.h" /* canonical SYS_* numbers (shared with i386) */

static uint64_t dispatch_total_count;
static uint64_t dispatch_enosys_count;
static uint64_t dispatch_per_num_count[8];   /* SYS_EXIT..SYS_GETPID hot path */

/* ---------------------------------------------------------------------------
 * Tiny helpers (avoid pulling in libc-style headers).
 * ------------------------------------------------------------------------- */

static x86_64_size_t k_strlen(const char *s) {
    x86_64_size_t n = 0;
    if (s == NULL) return 0;
    while (s[n] != '\0') ++n;
    return n;
}

static int k_strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { ++a; ++b; }
    return (int)((unsigned char)*a) - (int)((unsigned char)*b);
}

/*
 * Validate a user-mode pointer + length. Step-B implementation is permissive
 * (kernel and user share the higher-half identity map), but we still reject
 * NULL + non-zero length so bad call sites surface early.
 */
static int validate_user_buf(uint64_t ptr, uint64_t len) {
    if (len == 0) return 1;             /* zero-length is always OK */
    if (ptr == 0) return 0;             /* NULL with len > 0 is bogus */
    return 1;
}

/*
 * Exported thin wrappers so the split-out network backends (syscall_net64.c)
 * can reuse these helpers without duplicating them. Declared in syscall_net64.h.
 */
int arch_x86_64_validate_user_buf(uint64_t ptr, uint64_t len) {
    return validate_user_buf(ptr, len);
}
uint64_t arch_x86_64_k_strlen(const char *s) {
    return (uint64_t)k_strlen(s);
}

/* ---------------------------------------------------------------------------
 * Backends currently wired up on x86_64.
 * Each helper documents what it covers and what is still missing relative to
 * the i386 implementation, so future porting work has a clear checklist.
 * ------------------------------------------------------------------------- */

/*
 * ---------------------------------------------------------------------------
 * M4.3a: blocking pipe read/write.
 *
 * The pipe64 ring primitives are non-blocking. Real pipe(2) semantics require
 * a reader on an empty pipe to sleep until data arrives (or all writers close
 * -> EOF), and a writer on a full pipe to sleep until space frees (or all
 * readers close -> EPIPE). We implement this cooperatively on top of the
 * scheduler: register as a waiter, sched_yield to let the peer run, and
 * re-check. After making progress we drain the peer's waiter queue and wake
 * those slots.
 *
 * Safety net: a bounded spin cap. In the single-threaded selftest bootstrap
 * context there may be no peer thread to make progress, so rather than
 * deadlock the kernel we bail out after PIPE_BLOCK_SPIN_MAX yields, returning
 * whatever was transferred (0 for read = would-be-block treated as EOF-ish).
 * When a real concurrent kthread peer exists, progress happens long before
 * the cap and true blocking/wakeup is exercised.
 * ------------------------------------------------------------------------- */
#define PIPE_BLOCK_SPIN_MAX 100000

static void pipe_wake_peers(int pid, int wake_writers) {
    uint32_t slots[PIPE64_WAITERS_MAX];
    int n = pipe64_wait_drain(pid, wake_writers, slots);
    for (int i = 0; i < n; ++i) {
        arch_x86_64_sched_slot_wakeup(slots[i]);
    }
}

static int pipe_write_blocking(int pid, const void *buf, uint32_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t done = 0;
    uint32_t self = arch_x86_64_sched_current_slot();
    long spins = 0;
    while (done < len) {
        /* No reader left -> EPIPE (unless we already moved some bytes). */
        if (pipe64_has_reader(pid) == 0) {
            return done > 0 ? (int)done : -2;
        }
        int w = pipe64_write(pid, p + done, len - done);
        if (w < 0) {
            return done > 0 ? (int)done : w;
        }
        if (w > 0) {
            done += (uint32_t)w;
            /* We produced data: wake any readers parked on this pipe. */
            pipe_wake_peers(pid, 0);
            continue;
        }
        /* w == 0: ring full. Park as a writer-waiter and yield. */
        if (++spins > PIPE_BLOCK_SPIN_MAX) {
            pipe64_wait_remove(pid, 1, self);
            return (int)done;
        }
        pipe64_wait_add(pid, 1, self);
        arch_x86_64_sched_yield();
    }
    pipe64_wait_remove(pid, 1, self);
    return (int)done;
}

static int pipe_read_blocking(int pid, void *buf, uint32_t len) {
    uint8_t *p = (uint8_t *)buf;
    uint32_t self = arch_x86_64_sched_current_slot();
    long spins = 0;
    for (;;) {
        int r = pipe64_read(pid, p, len);
        if (r < 0) {
            return r;
        }
        if (r > 0) {
            /* We consumed data: wake any writers parked for space. */
            pipe_wake_peers(pid, 1);
            pipe64_wait_remove(pid, 0, self);
            return r;
        }
        /* r == 0: empty. If no writer remains, this is real EOF. */
        if (pipe64_has_writer(pid) == 0) {
            pipe64_wait_remove(pid, 0, self);
            return 0;
        }
        /* Otherwise block: park as reader-waiter and yield. */
        if (++spins > PIPE_BLOCK_SPIN_MAX) {
            pipe64_wait_remove(pid, 0, self);
            return 0; /* give up: treat as would-block -> EOF-ish */
        }
        pipe64_wait_add(pid, 0, self);
        arch_x86_64_sched_yield();
    }
}

/*
 * SYS_WRITE: routes through fdtable64 so fd=1/2 hit early_console64 and any
 * future writable fd (none yet) plugs in centrally.
 */
static uint64_t do_write(uint64_t fd, uint64_t buf_ptr, uint64_t len) {
    if (!validate_user_buf(buf_ptr, len)) return (uint64_t)-1;
    /* fd 0/1/2 stay on the early console path (stdout/stderr mirror). */
    if ((int)fd < SFD_FIRST) {
        int n = arch_x86_64_fd_write((int)fd,
                                     (const void *)(uintptr_t)buf_ptr,
                                     (x86_64_size_t)len);
        if (n < 0) return (uint64_t)-1;
        return (uint64_t)n;
    }
    /* fd >= 3: resolve to the underlying VFS fd and write through the
     * unified VFS (ramfs/fat32/ext). */
    /* fd >= 3: could be a pipe end or a VFS-backed fd. */
    if (sfd_kind((int)fd) == SFD_KIND_PIPE) {
        int pid = sfd_pipe_id((int)fd);
        if (pid < 0 || sfd_pipe_is_write((int)fd) != 1) return (uint64_t)-1;
        int n = pipe_write_blocking(pid,
                                    (const void *)(uintptr_t)buf_ptr,
                                    (uint32_t)len);
        if (n < 0) return (uint64_t)-1;   /* -1 bad id, -2 EPIPE */
        return (uint64_t)n;
    }
    int vfd = sfd_resolve((int)fd);
    if (vfd < 0) return (uint64_t)-1;
    int n = vfs_write(vfd, (const void *)(uintptr_t)buf_ptr, (uint32_t)len);
    if (n < 0) return (uint64_t)-1;
    return (uint64_t)n;
}

/*
 * SYS_READ: fd=0 (stdin) has no input source yet and returns 0. fd 1/2 are
 * not readable. fd >= 3 reads through the unified VFS.
 */
static uint64_t do_read(uint64_t fd, uint64_t buf_ptr, uint64_t len) {
    if (!validate_user_buf(buf_ptr, len)) return (uint64_t)-1;
    if ((int)fd < SFD_FIRST) {
        int n = arch_x86_64_fd_read((int)fd,
                                    (void *)(uintptr_t)buf_ptr,
                                    (x86_64_size_t)len);
        if (n < 0) return (uint64_t)-1;
        return (uint64_t)n;
    }
    if (sfd_kind((int)fd) == SFD_KIND_PIPE) {
        int pid = sfd_pipe_id((int)fd);
        if (pid < 0 || sfd_pipe_is_write((int)fd) != 0) return (uint64_t)-1;
        int n = pipe_read_blocking(pid,
                                   (void *)(uintptr_t)buf_ptr,
                                   (uint32_t)len);
        if (n < 0) return (uint64_t)-1;
        return (uint64_t)n;
    }
    int vfd = sfd_resolve((int)fd);
    if (vfd < 0) return (uint64_t)-1;
    int n = vfs_read(vfd, (void *)(uintptr_t)buf_ptr, (uint32_t)len);
    if (n < 0) return (uint64_t)-1;
    return (uint64_t)n;
}

/*
 * SYS_OPEN: routes through the unified VFS (ramfs64), which now hosts the
 * imported initrd tree plus the /mnt/fat (FAT32) and /mnt/ext (ext2/4)
 * mounts. The raw VFS fd is wrapped in a syscall fd (>= 3) via the sfd
 * indirection table so it never collides with stdin/stdout/stderr.
 */
static int fifo_try_open(const char *path, int flags); /* fwd: M4.3b FIFO open */

static uint64_t do_open(uint64_t path_ptr, uint64_t flags, uint64_t mode) {
    if (path_ptr == 0) return (uint64_t)-1;
    const char *path = (const char *)(uintptr_t)path_ptr;
    /* M4.3b: a registered FIFO path is opened via the named-pipe layer, not
     * the VFS. fifo_try_open returns -1 when the path is not a FIFO. */
    int frc = fifo_try_open(path, (int)flags);
    if (frc >= 0) return (uint64_t)frc;
    if (frc == -2) return (uint64_t)-1;   /* real FIFO open error */
    int vfd = vfs_open(path, (int)flags, (int)mode);
    if (vfd < 0) return (uint64_t)-1;
    int sfd = sfd_alloc(vfd);
    if (sfd < 0) {
        vfs_close(vfd);   /* table full: don't leak the VFS fd */
        return (uint64_t)-1;
    }
    return (uint64_t)sfd;
}

static uint64_t do_close(uint64_t fd) {
    /* Closing stdin/stdout/stderr is a no-op success (POSIX-ish). */
    if ((int)fd < SFD_FIRST) return 0;
    int kind = SFD_KIND_NONE, a = -1, b = 0;
    if (sfd_close((int)fd, &kind, &a, &b) < 0) return (uint64_t)-1;
    /* Only release the backend when this was the last reference (dup-aware). */
    if (kind == SFD_KIND_VFS) {
        if (a >= 0) vfs_close(a);
    } else if (kind == SFD_KIND_PIPE) {
        pipe64_close_end(a, b);
    }
    return 0;
}

/*
 * SYS_PIPE: create an anonymous pipe. Writes fd[0]=read end, fd[1]=write end
 * into the caller-provided int[2]. Returns 0 on success, -1 on failure.
 */
static uint64_t do_pipe(uint64_t fds_ptr) {
    if (fds_ptr == 0) return (uint64_t)-1;
    if (!validate_user_buf(fds_ptr, sizeof(int) * 2)) return (uint64_t)-1;
    int pid = pipe64_create();
    if (pid < 0) return (uint64_t)-1;
    int rfd = sfd_alloc_pipe(pid, 0);   /* read end  */
    if (rfd < 0) {
        pipe64_close_end(pid, 0);
        pipe64_close_end(pid, 1);
        return (uint64_t)-1;
    }
    int wfd = sfd_alloc_pipe(pid, 1);   /* write end */
    if (wfd < 0) {
        /* rfd took one read ref; drop the pipe entirely. */
        int k, x, y; sfd_close(rfd, &k, &x, &y);
        pipe64_close_end(pid, 0);
        pipe64_close_end(pid, 1);
        return (uint64_t)-1;
    }
    int *out = (int *)(uintptr_t)fds_ptr;
    out[0] = rfd;
    out[1] = wfd;
    return 0;
}

/*
 * SYS_MKFIFO: create a named pipe (FIFO) at path. a0=path, a1=mode.
 * The backing ring is allocated lazily on first open. Returns 0 on success,
 * -1 on bad args / table full / EEXIST.
 */
static uint64_t do_mkfifo(uint64_t path_ptr, uint64_t mode) {
    if (path_ptr == 0) return (uint64_t)-1;
    const char *path = (const char *)(uintptr_t)path_ptr;
    int rc = fifo64_mkfifo(path, (int)mode);
    return (rc == 0) ? 0 : (uint64_t)-1;
}

/*
 * FIFO-aware open: consulted by do_open before falling through to the VFS.
 * Returns a syscall fd (>= SFD_FIRST) on success, or -1 if the path is not a
 * registered FIFO (caller should then try the VFS), or -2 on a real FIFO
 * open error.
 */
static int fifo_try_open(const char *path, int flags) {
    if (fifo64_lookup(path) < 0) {
        return -1;   /* not a FIFO: let the VFS handle it */
    }
    /* Map open flags to a single direction. O_RDONLY=0, O_WRONLY=1, O_RDWR=2.
     * A FIFO opened O_RDWR gets a write end (a self-feeding endpoint); we keep
     * it simple and treat RDWR as the write side for producer semantics. */
    int lo = flags & 0x3;
    int dir = (lo == 0) ? FIFO64_O_READ : FIFO64_O_WRITE;
    int pid = fifo64_open(path, dir);
    if (pid < 0) return -2;
    int is_write = (dir == FIFO64_O_WRITE) ? 1 : 0;
    int sfd = sfd_alloc_pipe(pid, is_write);
    if (sfd < 0) {
        /* undo the end ref we just took */
        pipe64_close_end(pid, is_write);
        return -2;
    }
    return sfd;
}

/*
 * ---------------------------------------------------------------------------
 * M4.3c: shared memory syscalls (System V-style), delegating to shm64.
 *   SYS_SHM_CREATE  a0=key a1=size          -> shm id (>=0) / -1
 *   SYS_SHM_MAP     a0=shm_id               -> base address / (uint64_t)-1
 *   SYS_SHM_DETACH  a0=shm_id               -> 0 / <0
 *   SYS_SHM_DESTROY a0=shm_id               -> 0 / -1 (IPC_RMID)
 *   SYS_SHM_INFO    a0=shm_id a1=which       -> size(0)/nattch(1)/base(2)
 * ------------------------------------------------------------------------- */
static uint64_t do_shm_create(uint64_t key, uint64_t size) {
    int id = shm64_get((uint32_t)key, size);
    return (id < 0) ? (uint64_t)-1 : (uint64_t)id;
}

static uint64_t do_shm_map(uint64_t shm_id) {
    return shm64_attach((int)shm_id);   /* SHM64_ATTACH_FAILED == (uint64_t)-1 */
}

static uint64_t do_shm_detach(uint64_t shm_id) {
    int rc = shm64_detach((int)shm_id);
    return (rc == 0) ? 0 : (uint64_t)-1;
}

static uint64_t do_shm_destroy(uint64_t shm_id) {
    int rc = shm64_rmid((int)shm_id);
    return (rc == 0) ? 0 : (uint64_t)-1;
}

static uint64_t do_shm_info(uint64_t shm_id, uint64_t which) {
    switch (which) {
        case 0: return shm64_size((int)shm_id);
        case 1: return (uint64_t)shm64_nattch((int)shm_id);
        case 2: return shm64_base((int)shm_id);
        default: return (uint64_t)-1;
    }
}

/* SYS_DUP: lowest free syscall fd sharing oldfd's description. */
static uint64_t do_dup(uint64_t oldfd) {
    if ((int)oldfd < SFD_FIRST) return (uint64_t)-1;
    int nf = sfd_dup((int)oldfd);
    if (nf < 0) return (uint64_t)-1;
    return (uint64_t)nf;
}

/* SYS_DUP2: force newfd to alias oldfd's description (closing newfd first). */
static uint64_t do_dup2(uint64_t oldfd, uint64_t newfd) {
    if ((int)oldfd < SFD_FIRST || (int)newfd < SFD_FIRST) return (uint64_t)-1;
    int nf = sfd_dup2((int)oldfd, (int)newfd);
    if (nf < 0) return (uint64_t)-1;
    return (uint64_t)nf;
}

/*
 * SYS_LSEEK: reposition a VFS-backed fd. Standard streams are not seekable.
 */
static uint64_t do_lseek(uint64_t fd, uint64_t offset, uint64_t whence) {
    if ((int)fd < SFD_FIRST) return (uint64_t)-1;
    int vfd = sfd_resolve((int)fd);
    if (vfd < 0) return (uint64_t)-1;
    int r = vfs_seek(vfd, (int)offset, (int)whence);
    if (r < 0) return (uint64_t)-1;
    return (uint64_t)r;
}

/* ------------------------------------------------------------------
 * M4.1a: file metadata syscalls (stat / lstat / fstat / mkdir /
 * unlink / rename). All path-based ones route straight through the
 * unified VFS; fstat resolves the syscall fd first.
 * ------------------------------------------------------------------ */

/* Convert vfs_time_t (broken-down) to a packed UTC-ish u64 for userspace.
 * We keep it simple: pack Y/M/D/h/m/s into a sortable decimal number
 * (YYYYMMDDhhmmss). Good enough for listing/sorting; not epoch seconds. */
static uint64_t pack_vfs_time(const vfs_time_t *t) {
    if (!t) return 0;
    return (uint64_t)t->year   * 10000000000ULL
         + (uint64_t)t->month  * 100000000ULL
         + (uint64_t)t->day    * 1000000ULL
         + (uint64_t)t->hour   * 10000ULL
         + (uint64_t)t->minute * 100ULL
         + (uint64_t)t->second;
}

static void fill_stat_from_inode(openos_stat_t *st, const inode_t *ip) {
    st->ino       = ip->ino;
    st->mode      = ip->mode;
    st->size      = ip->size;
    st->nlinks    = ip->nlinks;
    st->fs_type   = ip->fs_type;
    st->uid       = ip->uid;
    st->gid       = ip->gid;
    st->ctime_utc = pack_vfs_time(&ip->ctime);
    st->mtime_utc = pack_vfs_time(&ip->mtime);
    st->atime_utc = pack_vfs_time(&ip->atime);
}

static uint64_t do_stat(uint64_t path_ptr, uint64_t st_ptr) {
    if (path_ptr == 0 || st_ptr == 0) return (uint64_t)-1;
    if (!validate_user_buf(st_ptr, sizeof(openos_stat_t))) return (uint64_t)-1;
    const char *path = (const char *)(uintptr_t)path_ptr;
    inode_t ino;
    if (vfs_stat(path, &ino) < 0) return (uint64_t)-1;
    fill_stat_from_inode((openos_stat_t *)(uintptr_t)st_ptr, &ino);
    return 0;
}

/* lstat: identical to stat but does not follow the final symlink. The VFS
 * exposes vfs_lstat when symlink support (M3.5) is compiled in; fall back to
 * vfs_stat otherwise. */
static uint64_t do_lstat(uint64_t path_ptr, uint64_t st_ptr) {
    if (path_ptr == 0 || st_ptr == 0) return (uint64_t)-1;
    if (!validate_user_buf(st_ptr, sizeof(openos_stat_t))) return (uint64_t)-1;
    const char *path = (const char *)(uintptr_t)path_ptr;
    inode_t ino;
    if (vfs_lstat(path, &ino) < 0) return (uint64_t)-1;
    fill_stat_from_inode((openos_stat_t *)(uintptr_t)st_ptr, &ino);
    return 0;
}

static uint64_t do_fstat(uint64_t fd, uint64_t st_ptr) {
    if (st_ptr == 0) return (uint64_t)-1;
    if (!validate_user_buf(st_ptr, sizeof(openos_stat_t))) return (uint64_t)-1;
    if ((int)fd < SFD_FIRST) return (uint64_t)-1; /* no stat for stdio */
    int vfd = sfd_resolve((int)fd);
    if (vfd < 0) return (uint64_t)-1;
    inode_t ino;
    if (vfs_fstat(vfd, &ino) < 0) return (uint64_t)-1;
    fill_stat_from_inode((openos_stat_t *)(uintptr_t)st_ptr, &ino);
    return 0;
}

static uint64_t do_mkdir(uint64_t path_ptr, uint64_t mode) {
    if (path_ptr == 0) return (uint64_t)-1;
    const char *path = (const char *)(uintptr_t)path_ptr;
    int r = vfs_mkdir(path, (int)mode);
    return (r < 0) ? (uint64_t)-1 : 0;
}

static uint64_t do_unlink(uint64_t path_ptr) {
    if (path_ptr == 0) return (uint64_t)-1;
    const char *path = (const char *)(uintptr_t)path_ptr;
    int r = vfs_unlink(path);
    return (r < 0) ? (uint64_t)-1 : 0;
}

static uint64_t do_rename(uint64_t old_ptr, uint64_t new_ptr) {
    if (old_ptr == 0 || new_ptr == 0) return (uint64_t)-1;
    const char *oldp = (const char *)(uintptr_t)old_ptr;
    const char *newp = (const char *)(uintptr_t)new_ptr;
    int r = vfs_rename(oldp, newp);
    return (r < 0) ? (uint64_t)-1 : 0;
}

/*
 * H.3 SYS_EXEC backend.
 *
 * execve(path, argv, envp) -- the POSIX-like "replace this process image"
 * call. argv/envp are accepted but ignored for now (H.x will wire them up
 * once we have a real userspace stack-arg builder).
 *
 * Flow:
 *   1) Look up `path` in the initrd. ENOENT -> -1 (no replacement happened,
 *      caller may handle gracefully).
 *   2) Hand the file off to elf64_loader. If load fails -> -1.
 *   3) Stash the new entry in usermode64 via mark_exec() and longjmp back
 *      to the kernel stack via return_to_kernel(). The outer driver in
 *      kernel64.c observes pending_exec and loops back into ring3 on the
 *      new image -- pid stays the same.
 *
 * On the -1 paths we explicitly do NOT touch usermode state, so a failing
 * execve looks just like a normal failing syscall: ring3 keeps running on
 * the *current* image and can handle it (typically by calling exit()).
 */
static uint64_t do_exec(uint64_t path_ptr, uint64_t argv_ptr, uint64_t envp_ptr) {
    early_console64_write("[x86_64][exec] ENTER do_exec\n");
    if (path_ptr == 0) {
        arch_x86_64_usermode_note_exec_fail();
        return (uint64_t)-1;
    }
    /*
     * IMPORTANT: copy `path` onto the kernel stack BEFORE we touch the
     * loader. Reason: under the H.2/H.3 identity-mapped layout the ring3
     * .rodata lives inside the very VA range (e.g. 0x400000..) that
     * elf64_load_image is about to overwrite with the *new* image. If we
     * keep using the original ring3 pointer past load_image, subsequent
     * dereferences (logging, find()) would read garbage from the new image
     * mapped at the same address.
     */
    char path_buf[128];
    {
        const char *src = (const char *)(uintptr_t)path_ptr;
        x86_64_size_t i = 0;
        for (; i < sizeof(path_buf) - 1u; ++i) {
            char c = src[i];
            path_buf[i] = c;
            if (c == '\0') break;
        }
        path_buf[sizeof(path_buf) - 1u] = '\0';
    }
    const char *path = path_buf;

    /*
     * H.4: snapshot argv onto kernel-side storage BEFORE elf load. Same
     * hazard as `path` above -- the source pointers may live in the
     * ring3 image about to be overwritten. We cap at
     * X86_64_USER_ARGV_MAX entries and X86_64_USER_ARG_MAX-1 bytes per
     * entry; anything beyond is silently truncated (POSIX-equivalent
     * E2BIG is not exposed yet). NULL argv is acceptable -> argc=0.
     * The storage is `static` because do_exec is single-entrant under
     * the current uniprocessor scheduling model and we want it out of
     * the (limited) kernel stack budget.
     */
    static char argv_storage[X86_64_USER_ARGV_MAX][X86_64_USER_ARG_MAX];
    static const char *argv_ptrs[X86_64_USER_ARGV_MAX];
    int argc_kern = 0;
    if (argv_ptr != 0) {
        const char *const *uargv =
            (const char *const *)(uintptr_t)argv_ptr;
        for (unsigned i = 0; i < X86_64_USER_ARGV_MAX; ++i) {
            const char *u = uargv[i];
            if (u == NULL) break;
            x86_64_size_t j = 0;
            for (; j < X86_64_USER_ARG_MAX - 1u; ++j) {
                char c = u[j];
                argv_storage[i][j] = c;
                if (c == '\0') break;
            }
            argv_storage[i][X86_64_USER_ARG_MAX - 1u] = '\0';
            argv_ptrs[i] = argv_storage[i];
            argc_kern = (int)i + 1;
        }
    }

    /*
     * H.5a: snapshot envp onto kernel-side storage with the same lifetime
     * rationale as argv above. NULL envp -> envc=0. We reuse the
     * ARG_MAX-sized slots for env strings; KEY=VALUE pairs longer than
     * that get truncated silently (no E2BIG yet).
     */
    static char envp_storage[X86_64_USER_ENVP_MAX][X86_64_USER_ENV_MAX];
    static const char *envp_ptrs[X86_64_USER_ENVP_MAX];
    int envc_kern = 0;
    if (envp_ptr != 0) {
        const char *const *uenvp =
            (const char *const *)(uintptr_t)envp_ptr;
        for (unsigned i = 0; i < X86_64_USER_ENVP_MAX; ++i) {
            const char *u = uenvp[i];
            if (u == NULL) break;
            x86_64_size_t j = 0;
            for (; j < X86_64_USER_ENV_MAX - 1u; ++j) {
                char c = u[j];
                envp_storage[i][j] = c;
                if (c == '\0') break;
            }
            envp_storage[i][X86_64_USER_ENV_MAX - 1u] = '\0';
            envp_ptrs[i] = envp_storage[i];
            envc_kern = (int)i + 1;
        }
    }

    /*
     * I.1: resolve the executable image. Priority order:
     *   (1) VFS/RAMFS (disk-backed, persistent) -- lets users drop new
     *       programs onto the filesystem and exec them without a rebuild.
     *   (2) initrd (compiled-in) -- fallback for the boot-critical set
     *       (/init, launcher, ...) that must exist before any disk mount.
     *
     * The loader (_load_image_into) *copies* PT_LOADs into fresh physical
     * pages, so whatever buffer we hand it can be freed right after the
     * load returns. For the VFS path we own a kmalloc'd buffer; for the
     * initrd path the data is zero-copy inside the embedded image and must
     * NOT be freed. `vfs_buf` tracks ownership.
     */
    const uint8_t *img_data = (const uint8_t *)0;
    uint64_t       img_size = 0;
    void          *vfs_buf  = (void *)0;

    {
        inode_t st;
        if (vfs_stat(path, &st) == 0 && (st.mode & FS_FILE) && st.size > 0) {
            void *buf = arch_x86_64_kmalloc((x86_64_size_t)st.size);
            if (buf != (void *)0) {
                int fd = vfs_open(path, 0 /*O_RDONLY*/, 0);
                if (fd >= 0) {
                    int got = vfs_read(fd, buf, (uint32_t)st.size);
                    vfs_close(fd);
                    if (got == (int)st.size) {
                        vfs_buf  = buf;
                        img_data = (const uint8_t *)buf;
                        img_size = (uint64_t)st.size;
                    } else {
                        arch_x86_64_kfree(buf);
                    }
                } else {
                    arch_x86_64_kfree(buf);
                }
            }
        }
    }

    if (img_data == (const uint8_t *)0) {
        const x86_64_initrd_file_t *file = arch_x86_64_initrd_find(path);
        if (file == NULL) {
            arch_x86_64_usermode_note_exec_fail();
            early_console64_write("[x86_64][exec] ENOENT path=");
            early_console64_write(path);
            early_console64_write("\n");
            return (uint64_t)-1;
        }
        img_data = file->data;
        img_size = file->size;
    } else {
        early_console64_write("[x86_64][exec] vfs-load path=");
        early_console64_write(path);
        early_console64_write("\n");
    }
    /*
     * H.5b.2 step A: create a fresh AS for the replacement image and mirror
     * the PT_LOADs into it via _load_image_into. CR3 is NOT switched here;
     * the legacy boot-identity ring3 path continues to back the actual
     * iretq. Step B will: (1) destroy old PCB.as, (2) set PCB.as=new_as,
     * (3) arch_x86_64_as_activate(new_as) right before iretq.
     */
    struct x86_64_address_space *new_as = arch_x86_64_as_create();
    elf64_load_result_t lr = arch_x86_64_elf64_load_image_into(img_data, img_size, new_as);
    /* loader has copied everything it needs; release the VFS buffer now. */
    if (vfs_buf != (void *)0) {
        arch_x86_64_kfree(vfs_buf);
        vfs_buf = (void *)0;
    }
    if (lr.status != ELF64_LOADER_OK) {
        arch_x86_64_usermode_note_exec_fail();
        early_console64_write("[x86_64][exec] elf-load-failed path=");
        early_console64_write(path);
        early_console64_write(" status=");
        early_console64_write_hex64((uint64_t)(uint32_t)lr.status);
        early_console64_write("\n");
        if (new_as != ((struct x86_64_address_space *)0)) {
            arch_x86_64_as_destroy(new_as);
        }
        return (uint64_t)-1;
    }
    {
        /*
         * A2.P3-B-beta fix: CR3 must point at new_as BEFORE we destroy old_as.
         *
         * Bug found in P3-C investigation (was misdiagnosed as a high-half PT
         * gap): old_as's PML4 physical page is freed by as_destroy(). If CR3
         * still references it, the next PMM allocation can hand that page
         * back out and a subsequent write will corrupt the in-flight PML4 --
         * including PML4[511] which maps the kernel image. The next IRQ then
         * triple-faults trying to fetch the ISR.
         *
         * Order: (1) set PCB.as=new_as, (2) load CR3 from new_as, (3) free old.
         *
         * M5.2d exec double-alloc fix: step (3) is no longer done inline.
         * seed_user_stack + the stack remap in usermode_run() still run
         * alloc_table() calls AFTER this point; if we freed old_as here, one
         * of those allocations could pull a just-freed page that physically
         * overlaps the new image's .text and memset() it to zero, faulting
         * ring3 on all-zero bytes. Instead we QUEUE old_as and let
         * usermode_run() drain the queue only after all such allocations are
         * done (right before the iretq drop). CR3 has already been switched
         * to new_as above, so the old PML4 page is safe to free later.
         */
        struct x86_64_address_space *old_as = arch_x86_64_proc_current_get_as();
        arch_x86_64_proc_current_set_as(new_as);
        arch_x86_64_as_activate(new_as);
        arch_x86_64_usermode_queue_as_destroy(old_as);
    }
    early_console64_write("[x86_64][exec] path=");
    early_console64_write(path);
    early_console64_write(" entry=");
    early_console64_write_hex64((uint64_t)lr.entry);
    early_console64_write(" size=");
    early_console64_write_hex64((uint64_t)img_size);
    early_console64_write(" argc=");
    early_console64_write_hex64((uint64_t)(uint32_t)argc_kern);
    early_console64_write(" envc=");
    early_console64_write_hex64((uint64_t)(uint32_t)envc_kern);
    early_console64_write("\n");

    /*
     * Commit the replacement. set_args / set_envs queue argv/envp for the
     * next usermode_run; mark_exec stashes the new entry and flips
     * usermode_exited=1 / pending_exec=1, then return_to_kernel longjmps
     * back to usermode_run()'s saved kernel frame. Crucially we do NOT
     * call proc_exit -- the pid lives on.
     */
    arch_x86_64_usermode_set_args(argc_kern, argv_ptrs);
    arch_x86_64_usermode_set_envs(envc_kern, envp_ptrs);
    arch_x86_64_usermode_mark_exec(lr.entry);
    arch_x86_64_usermode_return_to_kernel();
    return 0;  /* unreachable */
}

/*
 * SYS_EXIT backend.
 * Marks the current user-mode thread as exited so the kernel can reclaim it.
 *
 * CONTRACT (A2.P0): this function MUST NOT return. It longjmps via
 * arch_x86_64_usermode_return_to_kernel(), restoring the kernel stack saved
 * by arch_x86_64_usermode_run(). Returning would let the syscall entry
 * wrapper sysretq into a stale ring3 RIP (#GP at CPL=3); the wrapper in
 * syscall64.c trips a loud assert if that ever happens.
 */
static uint64_t do_exit(uint64_t status) {
    /* gamma.3b-S2a Seg-6: sched-dispatched USER slot exit path.
     *
     * If we got here because a child user thread that had been dispatched
     * onto an AP via a PARKED sched_slot called exit(), we are running on
     * that AP inside the syscall wrapper. There is no usermode_run() frame
     * on our kernel stack to longjmp back to -- the AP entered ring3 from
     * sched_run's dispatch, not from usermode_run.
     *
     * Detect this condition by asking sched: is my current slot USER-typed
     * AND does its owner_proc == me AND am I not on the BSP kmain path
     * (usermode_running flag is only ever set on the BSP that owns
     * usermode_run)? If yes:
     *   1. Record the exit code + child_exited flag on the PCB so the
     *      parent's do_wait busy-loop (Seg-7) can observe it.
     *   2. Mark the sched_slot EXITED so the dispatcher will reap it on
     *      its next tick and pick another READY slot (or fall back to
     *      the AP's idle slot).
     *   3. NULL out slot.owner_proc so any subsequent proc_current() on
     *      this CPU (before reap) sees the kernel PCB, not our stale
     *      pointer.
     *   4. Call sched_run() -- it will pick another slot and
     *      context_switch away, never returning here. The dying kernel
     *      stack is safe to free asynchronously (Seg-6 punts this: the
     *      slot's stack lives until beta reap; we do not free it inline).
     *
     * The legacy longjmp branch below is untouched for BSP-hosted user
     * procs (kmain / usermode_run() loop). */
    {
        uint32_t sslot = arch_x86_64_sched_current_slot();
        struct x86_64_proc *owner =
            arch_x86_64_sched_slot_get_owner_proc(sslot);
        x86_64_proc_t *me = arch_x86_64_proc_current();
        /* usermode_is_running is a global set by BSP's usermode_run() -- it does
     * NOT tell us whether the CURRENT cpu is a BSP-usermode caller. On APs
     * child do_exit must go through the sched-USER path even while BSP is
     * still spinning inside usermode_run. Gate by CPU id. */
    int on_bsp_usermode_run =
            (arch_x86_64_this_cpu_idx() == 0u) &&
            (int)arch_x86_64_usermode_is_running();
        if (owner != NULL && owner == me && !on_bsp_usermode_run &&
            sslot != 0u) {
            /* γ.4 DIAG: trace which do_exit path each child takes. */
            early_console64_write("[do_exit:zpath] pid=");
            early_console64_write_hex64((uint64_t)(me ? me->pid : 0xFFFF));
            early_console64_write(" sslot=");
            early_console64_write_hex64((uint64_t)sslot);
            early_console64_write(" cpu=");
            early_console64_write_hex64((uint64_t)arch_x86_64_this_cpu_idx());
            early_console64_write("\n");
            me->child_exit_code = (int)status;
            me->child_exited    = true;  /* legacy self-flag; harmless */
            /* γ.4: notify the *parent* PCB so its do_wait Mode-A poll
             * on parent->child_exited actually observes the exit. me is
             * the CHILD PCB; the parent PCB was linked via me->parent_slot
             * in fork_capture (see proc64.c fork_alloc_child). */
            x86_64_proc_t *parent =
                arch_x86_64_proc_slot(me->parent_slot);
            if (parent != NULL) {
                /* γ.4 S2b — thread this child onto the parent's zombie
                 * list.
                 *
                 * 1. Remove me from parent->children_head (singly-linked
                 *    via child.sibling_next). O(N) but N is tiny.
                 * 2. Prepend me to parent->zombie_head.
                 * 3. Mirror my exit_code/pid into the parent's legacy
                 *    singleton fields so old wait() code paths keep
                 *    seeing SOMETHING (the most recent zombie). do_wait
                 *    below now consumes zombie_head, not the singleton.
                 * 4. Raise parent->child_exited so any Mode-A poller
                 *    still spinning wakes up.
                 *
                 * γ.4 S2d: LOCKING. The old code ran unlocked on the
                 * assumption that fork/wait was serviced single-CPU. That
                 * is FALSE once children run on APs: fork-multi spawns 3
                 * children, the scheduler lands them on CPU1/CPU2, and two
                 * of them can hit this unlink/prepend concurrently. The
                 * interleaving drops a node (observed: pid=3 on CPU1 lost,
                 * parent's wait blocks forever). We now take the global
                 * family lock around the whole list surgery. CRITICAL: the
                 * lock is released BEFORE sched_exit_self() (which never
                 * returns) — holding it across the context switch would
                 * wedge every other CPU spinning on the lock. */
                uint16_t my_slot = arch_x86_64_proc_slot_of(me);
                arch_x86_64_proc_family_lock();
                if (parent->children_head == my_slot) {
                    parent->children_head = me->sibling_next;
                } else {
                    uint16_t cur = parent->children_head;
                    while (cur != OPENOS_X86_64_PROC_INVALID_INDEX) {
                        x86_64_proc_t *cp = arch_x86_64_proc_slot(cur);
                        if (cp == NULL) break;
                        if (cp->sibling_next == my_slot) {
                            cp->sibling_next = me->sibling_next;
                            break;
                        }
                        cur = cp->sibling_next;
                    }
                }
                me->sibling_next = OPENOS_X86_64_PROC_INVALID_INDEX;

                me->zombie_next    = parent->zombie_head;
                parent->zombie_head = my_slot;

                parent->child_exit_code = (int)status;
                parent->child_exited    = true;
                arch_x86_64_proc_family_unlock();
            }

            (void)arch_x86_64_sched_slot_set_owner_proc(sslot, NULL);

            /* γ.4 S3 — CROSS-CPU WAKE. Root cause of the fork-multi hang:
             * this child ran on an AP and just published its zombie +
             * child_exited=true, but the parent is asleep in do_wait's
             * `sti; hlt` idle loop on the BSP. Nothing pokes the BSP, so
             * the parent only re-checks zombie_head on the next stray
             * interrupt (LAPIC timer) — and with 3 children the timing
             * lines up so the *middle* child's wakeup is lost (classic
             * test-and-hlt lost-wakeup). We now explicitly send the parent
             * a reschedule IPI (vector 0x41) so its hlt returns immediately
             * and it re-evaluates zombie_head. The parent runs on the BSP
             * (fork/wait Mode B is BSP-only), so target the BSP apic_id.
             * The 0x41 handler just EOIs + counts, so a spurious poke is
             * harmless if the parent was already awake. */
            arch_x86_64_lapic_send_fixed_ipi(arch_x86_64_smp_bsp_apic_id(),
                                             0x41u);

            /* Hand control back to the scheduler. sched_exit_self marks
             * the current slot EXITED, picks the next runnable, and
             * context_switches away. Never returns. */
            arch_x86_64_sched_exit_self();

            /* Safety net: if sched_exit_self ever returned we would sysret
             * into a dying ring3 context. Halt loudly. */
            for (;;) __asm__ volatile ("cli; hlt");
        }
    }

    /* Snapshot the kernel return RSP while `current` is still the exiting
     * user proc -- mark_exited() flips current to the kernel proc whose
     * kernel_return_rsp is 0, so the trampoline would lose its unwind
     * target otherwise. */
    /* γ.4 DIAG: this is the BSP/else fallback path (NOT the zombie-list
     * path). If a fork-multi child lands here its exit is invisible to the
     * parent's wait -- that is the bug we are hunting. */
    {
        x86_64_proc_t *dme = arch_x86_64_proc_current();
        early_console64_write("[do_exit:bsppath] pid=");
        early_console64_write_hex64((uint64_t)(dme ? dme->pid : 0xFFFF));
        early_console64_write(" cpu=");
        early_console64_write_hex64((uint64_t)arch_x86_64_this_cpu_idx());
        early_console64_write("\n");
    }
    arch_x86_64_usermode_snapshot_return_rsp();
    arch_x86_64_usermode_mark_exited((int)status);
    /*
     * Step D.3 fix: do NOT return -- otherwise dispatch_common returns,
     * the syscall entry path does sysretq, and ring3 keeps executing the
     * instruction after 'syscall' (a 'hlt' in the _start epilogue), which
     * #GP's at CPL=3.  Instead, unwind back to the kernel stack saved by
     * usermode_run() and let it observe usermode_exited=1.
     */
    arch_x86_64_usermode_return_to_kernel();
    return 0;  /* unreachable */
}

/*
 * SYS_MALLOC / SYS_FREE: thin wrappers around heap64. Pointers are returned
 * as uint64_t (full 64-bit); callers must treat the value as a pointer, not
 * truncate to 32-bit as the i386 path does.
 */
static uint64_t do_malloc(uint64_t size) {
    void *p = arch_x86_64_kmalloc((x86_64_size_t)size);
    return (uint64_t)(uintptr_t)p;
}

static uint64_t do_free(uint64_t ptr) {
    arch_x86_64_kfree((void *)(uintptr_t)ptr);
    return 0;
}

/*
 * M5.1d: 惰性绑定解析。由用户态 _dl_runtime_resolve trampoline 首次调用时陷入。
 *   a0 = link_map*  (GOT[1]，内核白名单校验)
 *   a1 = reloc_index (JMPREL 下标)
 * 返回目标函数运行时地址；0 表示失败。
 */
static uint64_t do_dl_resolve(uint64_t a0, uint64_t a1) {
    return openos_elf64_dl_resolve_entry(a0, a1);
}

/*
 * SYS_CLONE (478) — M5.2a: create a new THREAD that shares the caller's
 * address space.
 *
 *   a0 = flags        (OPENOS_CLONE_* bitmask)
 *   a1 = child_stack  (user %rsp top for the new thread)
 *   a2 = entry        (user thread start function)
 *   a3 = arg          (single argument passed to entry)
 *   a4 = tls          (CLONE_SETTLS: %fs.base for the thread)
 *
 * Returns the new kernel tid (>0) on success, or a negative errno-style
 * value on failure. M5.2a only builds the PCB (shared AS, tgid, tls_base,
 * clear_child_tid); actually launching the thread into ring3 is M5.2b.
 *
 * SECURITY: every unsupported flag bit is rejected so userland can never
 * rely on silently-ignored semantics. child_stack/entry are validated as
 * real user addresses so a bogus request can't stage a kernel-pointer
 * launch. The CHILD_CLEARTID user address (if requested) is bounds-checked
 * here too, before it is recorded for the exit-time futex wake.
 */
static uint64_t do_clone(uint64_t a0, uint64_t a1, uint64_t a2,
                         uint64_t a3, uint64_t a4) {
    openos_clone_args_t args;
    args.flags       = (uint32_t)a0;
    args.child_stack = a1;
    args.entry       = a2;
    args.arg         = a3;
    args.tls         = a4;
    args.parent_tid  = 0;
    args.child_tid   = 0;

    /* Reject any flag bit we don't implement (see OPENOS_CLONE_SUPPORTED_MASK). */
    if ((args.flags & ~OPENOS_CLONE_SUPPORTED_MASK) != 0) {
        return (uint64_t)-1;
    }
    /* M5.2a only supports the pthread-style shared-AS thread. */
    if ((args.flags & OPENOS_CLONE_THREAD_MIN) != OPENOS_CLONE_THREAD_MIN) {
        return (uint64_t)-1;
    }
    /* child_stack / entry must be valid user addresses. We probe a single
     * machine word at each so an obviously-bogus (kernel / unmapped)
     * pointer is rejected before the PCB is allocated. */
    if (!validate_user_buf(args.child_stack - sizeof(uint64_t), sizeof(uint64_t))) {
        return (uint64_t)-1;
    }
    if (!validate_user_buf(args.entry, 1)) {
        return (uint64_t)-1;
    }
    /* CLONE_CHILD_CLEARTID: the ctid user address must be writable at exit;
     * bounds-check it now (a3 holds arg, ctid is not passed via registers in
     * this minimal ABI — M5.2a reuses child_stack-relative convention only,
     * so ctid comes from tls-adjacent TCB in userland; here we simply record
     * 0 unless a future ABI extension supplies it). */
    if (args.flags & OPENOS_CLONE_CHILD_CLEARTID) {
        /* No dedicated ctid register in this 5-arg ABI yet; disabled. */
        args.child_tid = 0;
    }

    x86_64_proc_t *me = arch_x86_64_proc_current();
    if (me == NULL) {
        return (uint64_t)-1;
    }

    x86_64_proc_t *child = arch_x86_64_proc_clone_thread(me, &args);
    if (child == NULL) {
        return (uint64_t)-1;
    }

    /*
     * M5.2b: launch the thread for real. clone_thread has built a PCB that
     * SHARES our address space; now park it into a scheduler slot with the
     * 6-qword thread-start frame so it begins at entry(arg) on child_stack
     * with %fs.base=tls. Mirrors the fork spawn path in syscall64.c but uses
     * spawn_uthread_thread (arg->rdi) instead of the fork-resume trampoline.
     */
    uint16_t ucs = (uint16_t)(OPENOS_X86_64_GDT_USER_CODE | 3u);
    uint16_t uss = (uint16_t)(OPENOS_X86_64_GDT_USER_DATA | 3u);
    uint32_t slot = arch_x86_64_sched_spawn_uthread_thread(
        (uint64_t)child->thread_entry,
        child->fork_user_rsp,
        child->thread_arg,
        0x202ULL,                 /* IF=1, reserved bit 1 */
        ucs, uss,
        child->tls_base,
        OPENOS_X86_64_SCHED_PRIO_DEFAULT,
        arch_x86_64_this_cpu_ptr()->cpu_idx);
    if (slot == 0xFFFFFFFFu) {
        /* spawn failed: tear the PCB back down (release_slot -> as_put drops
         * the shared-AS refcount clone_thread bumped) so we don't leak. */
        arch_x86_64_proc_release_slot(arch_x86_64_proc_slot_of(child));
        return (uint64_t)-1;
    }

    /* Bind AS + owner BEFORE wakeup, otherwise the target CPU may dispatch
     * the slot with a NULL address space and fault. */
    (void)arch_x86_64_sched_slot_set_as(slot, child->as);
    (void)arch_x86_64_sched_slot_set_owner_proc(slot, child);
    child->fork_child_sched_slot = slot;

    early_console64_write("[x86_64][clone] launch thread tid=");
    early_console64_write_hex64((uint64_t)child->tid);
    early_console64_write(" tgid=");
    early_console64_write_hex64((uint64_t)child->tgid);
    early_console64_write(" slot=");
    early_console64_write_hex64((uint64_t)slot);
    early_console64_write("\n");

    (void)arch_x86_64_sched_slot_wakeup(slot);
    return (uint64_t)child->tid;
}

/*
 * -------------------------------------------------------------------------
 * SYS_FUTEX_WAIT / SYS_FUTEX_WAKE / SYS_FUTEX_WAIT_TIMEOUT  (M5.2c)
 * -------------------------------------------------------------------------
 * Thin dispatch shims over the futex64 subsystem. PRIVATE semantics: the key
 * is the user virtual address of a 32-bit word, valid across a CLONE_VM thread
 * group because siblings share one address space.
 *
 *   futex_wait(uaddr, val)          — a0=uaddr, a1=val; blocks until woken
 *   futex_wait_timeout(uaddr, val, ms) — a0=uaddr, a1=val, a2=timeout_ms
 *   futex_wake(uaddr, count)        — a0=uaddr, a1=count; returns #woken
 *
 * All negative returns are errno-style codes from futex64.h.
 */
static uint64_t do_futex_wait(uint64_t a0, uint64_t a1) {
    early_serial64_write("[futex] WAIT uaddr=");
    early_console64_write_hex64(a0);
    early_serial64_write(" val=");
    early_console64_write_hex64(a1);
    early_serial64_write("\n");
    return (uint64_t)(int64_t)arch_x86_64_futex_wait(a0, (uint32_t)a1, 0);
}

static uint64_t do_futex_wait_timeout(uint64_t a0, uint64_t a1, uint64_t a2) {
    return (uint64_t)(int64_t)arch_x86_64_futex_wait(a0, (uint32_t)a1, a2);
}

static uint64_t do_futex_wake(uint64_t a0, uint64_t a1) {
    early_serial64_write("[futex] WAKE uaddr=");
    early_console64_write_hex64(a0);
    early_serial64_write(" n=");
    early_console64_write_hex64(a1);
    early_serial64_write("\n");
    return (uint64_t)(int64_t)arch_x86_64_futex_wake(a0, (int)(int32_t)a1);
}

/*
 * SYS_UPTIME_MS: real millisecond uptime, calibrated against the i8254 PIT
 * during early boot (see tsc64.c). If calibration somehow failed (per_ms==0)
 * we fall back to the legacy `rdtsc >> 20` placeholder so the call stays
 * monotonic instead of returning a flat 0 — that keeps existing consumers
 * working even on exotic hosts where the PIT poll didn't fire.
 */
static uint64_t do_uptime_ms(void) {
    uint64_t ms = arch_x86_64_tsc_uptime_ms();
    if (ms != 0) return ms;
    if (arch_x86_64_tsc_per_ms() != 0) return 0; /* calibrated, just early */
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t tsc = ((uint64_t)hi << 32) | lo;
    return tsc >> 20;
}

/*
 * SYS_CLOCK_GETTIME (332): fill a user openos_timespec_t with a monotonic
 * timestamp derived from do_uptime_ms(). Only CLOCK_MONOTONIC is meaningful
 * on this kernel (no RTC wall-clock wiring yet); any clk_id is accepted and
 * treated as monotonic so callers get a usable steady clock. Returns 0 on
 * success, -1 on a bogus user pointer.
 */
static uint64_t do_clock_gettime(uint64_t clk_id, uint64_t ts_ptr) {
    (void)clk_id;
    if (!validate_user_buf(ts_ptr, sizeof(openos_timespec_t))) return (uint64_t)-1;
    uint64_t ms = do_uptime_ms();
    openos_timespec_t *ts = (openos_timespec_t *)ts_ptr;
    ts->tv_sec  = (int64_t)(ms / 1000ULL);
    ts->tv_nsec = (int64_t)((ms % 1000ULL) * 1000000ULL);
    return 0;
}

/*
 * Shared busy-wait sleep core: spin-yield until `ms` milliseconds of
 * monotonic uptime have elapsed. This kernel has no blocking timer queue yet,
 * so we cooperatively yield each iteration to let other tasks run instead of
 * hard-spinning. A zero/short sleep still yields once (POSIX-friendly).
 */
static void sleep_ms_busy(uint64_t ms) {
    uint64_t start = do_uptime_ms();
    do {
        (void)arch_x86_64_proc_yield();
    } while ((do_uptime_ms() - start) < ms);
}

/*
 * SYS_SLEEP (200): sleep for `seconds` whole seconds. Returns 0 (no remaining
 * time tracking on this cooperative implementation).
 */
static uint64_t do_sleep(uint64_t seconds) {
    sleep_ms_busy(seconds * 1000ULL);
    return 0;
}

/*
 * SYS_NANOSLEEP (348): sleep for the duration in the user openos_timespec_t
 * pointed to by req_ptr, at millisecond granularity (kernel clock resolution).
 * rem_ptr, if non-NULL, is zeroed since this cooperative sleep is not
 * interruptible. Returns 0 on success, -1 on a bogus req pointer.
 */
static uint64_t do_nanosleep(uint64_t req_ptr, uint64_t rem_ptr) {
    if (!validate_user_buf(req_ptr, sizeof(openos_timespec_t))) return (uint64_t)-1;
    const openos_timespec_t *req = (const openos_timespec_t *)req_ptr;
    int64_t sec  = req->tv_sec;
    int64_t nsec = req->tv_nsec;
    if (sec < 0 || nsec < 0) return (uint64_t)-1;
    uint64_t ms = (uint64_t)sec * 1000ULL + (uint64_t)nsec / 1000000ULL;
    sleep_ms_busy(ms);
    if (rem_ptr != 0 && validate_user_buf(rem_ptr, sizeof(openos_timespec_t))) {
        openos_timespec_t *rem = (openos_timespec_t *)rem_ptr;
        rem->tv_sec  = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

/*
 * SYS_GETTIMEOFDAY (466): fill a user openos_timeval with the current time.
 * M4.1d alpha: we do not yet have a persistent RTC/CMOS wall-clock feed into
 * userspace, so we expose the monotonic since-boot clock (same source as
 * SYS_CLOCK_GETTIME). tv_sec/tv_usec therefore count from boot. When the RTC
 * epoch bridge lands (tracked in M4.x) this becomes true UTC without changing
 * the ABI. a1 (timezone) is legacy/ignored per modern POSIX. Returns 0, or
 * -1 on a bad user pointer.
 */
static uint64_t do_gettimeofday(uint64_t tv_ptr, uint64_t tz_ptr) {
    (void)tz_ptr; /* struct timezone is obsolete; ignore like Linux */
    if (tv_ptr == 0 || !validate_user_buf(tv_ptr, sizeof(openos_timeval_t))) {
        return (uint64_t)-1;
    }
    uint64_t ms = do_uptime_ms();
    openos_timeval_t *tv = (openos_timeval_t *)tv_ptr;
    tv->tv_sec  = (int64_t)(ms / 1000ULL);
    tv->tv_usec = (int64_t)((ms % 1000ULL) * 1000ULL);
    return 0;
}

/*
 * SYS_IOCTL (467): device control multiplexer. M4.1d ships the ABI seam only;
 * we have no TTY/line-discipline or block-device ioctl surface yet (that is
 * M4.4 TTY work). For now we validate the fd against the syscall fd table and
 * return -1 (ENOTTY-equivalent) for every request, which is the correct answer
 * for a plain file. This lets libc probes (isatty(), tcgetattr()) fail cleanly
 * instead of trapping. Once the TTY driver exists this switch grows real cases.
 */
/* ioctl request numbers (Linux-compatible subset) that the TTY layer honours. */
#define IOCTL_TCGETS      0x5401u
#define IOCTL_TCSETS      0x5402u
#define IOCTL_TIOCGWINSZ  0x5413u
#define IOCTL_TIOCSWINSZ  0x5414u
#define IOCTL_TIOCGPGRP   0x540Fu
#define IOCTL_TIOCSPGRP   0x5410u

/*
 * The kernel owns a single console pseudo-terminal, lazily created on first
 * use. fd 0/1/2 (stdin/stdout/stderr) are all bound to it: reads pull cooked
 * input, writes push to the output ring, and ioctl(TCGETS/...) manipulates its
 * line discipline. A dedicated selftest may create additional ttys.
 */
static int g_console_tty = -1;

static int syscall_console_tty(void) {
    if (g_console_tty < 0 || !tty64_valid(g_console_tty)) {
        g_console_tty = tty64_create();
    }
    return g_console_tty;
}

/* Is this fd a terminal? Standard streams 0/1/2 are the console tty. */
static int fd_is_tty(uint64_t fd) {
    return (fd <= 2);
}

/*
 * M4.4b bridge: drain any control-char signal latched by the console tty
 * (e.g. ^C -> SIGINT, ^\ -> SIGQUIT) and broadcast it to the tty's current
 * foreground process group. This is the kernel-side of job control: only the
 * foreground job receives keyboard-generated signals. Returns the number of
 * processes signalled (>=0), or -1 if the console tty is absent. Called from
 * the input path and exercised directly by the self-test.
 */
int arch_x86_64_tty_pump_signals(void) {
    int tid = syscall_console_tty();
    if (tid < 0) return -1;
    int sig = tty64_take_signal(tid);
    if (sig == 0) return 0;
    int pgrp = tty64_get_pgrp(tid);
    if (pgrp <= 0) return 0; /* no foreground group -> signal dropped */
    return arch_x86_64_proc_signal_group((uint32_t)pgrp, sig);
}

static uint64_t do_ioctl(uint64_t fd, uint64_t request, uint64_t arg) {
    /* fd 0/1/2 are the console terminal; everything else must be a live vfs
     * fd (and vfs objects currently expose no ioctl surface -> ENOTTY). */
    if (!fd_is_tty(fd)) {
        if (sfd_resolve((int)fd) < 0) return (uint64_t)-1; /* EBADF */
        return (uint64_t)-1; /* ENOTTY: regular files have no ioctl */
    }

    int tid = syscall_console_tty();
    if (tid < 0) return (uint64_t)-1;

    switch ((uint32_t)request) {
    case IOCTL_TCGETS: {
        if (!arg) return (uint64_t)-1;
        tty64_termios_t t;
        if (tty64_tcgets(tid, &t) < 0) return (uint64_t)-1;
        tty64_termios_t *u = (tty64_termios_t *)arg;
        *u = t;
        return 0;
    }
    case IOCTL_TCSETS: {
        if (!arg) return (uint64_t)-1;
        const tty64_termios_t *u = (const tty64_termios_t *)arg;
        tty64_termios_t t = *u;
        return (tty64_tcsets(tid, &t) == 0) ? 0 : (uint64_t)-1;
    }
    case IOCTL_TIOCGWINSZ: {
        if (!arg) return (uint64_t)-1;
        tty64_winsize_t w;
        if (tty64_get_winsize(tid, &w) < 0) return (uint64_t)-1;
        *(tty64_winsize_t *)arg = w;
        return 0;
    }
    case IOCTL_TIOCSWINSZ: {
        if (!arg) return (uint64_t)-1;
        tty64_winsize_t w = *(const tty64_winsize_t *)arg;
        return (tty64_set_winsize(tid, &w) == 0) ? 0 : (uint64_t)-1;
    }
    case IOCTL_TIOCGPGRP: {
        if (!arg) return (uint64_t)-1;
        int pg = tty64_get_pgrp(tid);
        if (pg < 0) return (uint64_t)-1;
        *(uint32_t *)arg = (uint32_t)pg;
        return 0;
    }
    case IOCTL_TIOCSPGRP: {
        if (!arg) return (uint64_t)-1;
        int pg = (int)(*(const uint32_t *)arg);
        return (tty64_set_pgrp(tid, pg) == 0) ? 0 : (uint64_t)-1;
    }
    default:
        return (uint64_t)-1; /* unsupported ioctl on a tty -> EINVAL/ENOTTY */
    }
}

/*
 * SYS_KILL (245): deliver a signal to the process identified by pid.
 * Backed by arch_x86_64_proc_signal(). M4.2 semantics: the target's pending
 * bitmap is set through the signal64 layer; terminating-class signals whose
 * disposition is still SIG_DFL reap the target (EXITED, code 128+sig); sig 0
 * is an existence probe; SIGKILL/SIGSTOP ignore any handler. Returns 0 on
 * success, -1 if no such pid (ESRCH-equivalent) or on an invalid signal.
 */
static uint64_t do_kill(uint64_t pid, uint64_t sig) {
    if ((int)sig < 0 || (int)sig > 31) {
        return (uint64_t)-1; /* EINVAL: outside the classic signal range */
    }
    int spid = (int)pid;
    /* POSIX kill() pid conventions:
     *   pid  > 0 : signal that one process.
     *   pid  < 0 : signal every member of process group |pid| (job control).
     *   pid == 0 : signal every member of the CALLER's process group.
     * (pid == -1 "broadcast to all" is intentionally not supported here.) */
    if (spid > 0) {
        int rc = arch_x86_64_proc_signal((uint32_t)spid, (int)sig);
        return (rc == 0) ? 0 : (uint64_t)-1;
    }
    uint32_t pgid;
    if (spid == 0) {
        x86_64_proc_t *cur = arch_x86_64_proc_current();
        if (cur == 0) return (uint64_t)-1;
        pgid = cur->pgid;
    } else {
        pgid = (uint32_t)(-spid);
    }
    int hits = arch_x86_64_proc_signal_group(pgid, (int)sig);
    return (hits > 0) ? 0 : (uint64_t)-1;
}

/* M4.4b job-control syscalls. Thin wrappers over the proc64 group/session
 * API; all operate on the current process unless a pid is supplied. */
static uint64_t do_setpgid(uint64_t pid, uint64_t pgid) {
    return (arch_x86_64_proc_setpgid((uint32_t)pid, (uint32_t)pgid) == 0)
               ? 0 : (uint64_t)-1;
}
static uint64_t do_getpgid(uint64_t pid) {
    uint32_t pg = arch_x86_64_proc_getpgid((uint32_t)pid);
    return (pg == (uint32_t)-1) ? (uint64_t)-1 : (uint64_t)pg;
}
static uint64_t do_setsid(void) {
    int sid = arch_x86_64_proc_setsid();
    return (sid < 0) ? (uint64_t)-1 : (uint64_t)sid;
}
static uint64_t do_getsid(uint64_t pid) {
    uint32_t sid = arch_x86_64_proc_getsid((uint32_t)pid);
    return (sid == (uint32_t)-1) ? (uint64_t)-1 : (uint64_t)sid;
}

/*
 * SYS_RT_SIGACTION (468): register / query a signal disposition on the
 * CURRENT process. a0=sig, a1=const openos_sigaction* (may be 0 for query),
 * a2=openos_sigaction* old (may be 0). Both pointers are validated as
 * identity-mapped user buffers before use. Returns 0 / -1.
 */
static uint64_t do_rt_sigaction(uint64_t sig, uint64_t act_ptr,
                                uint64_t old_ptr) {
    openos_sigaction_t act_local;
    openos_sigaction_t old_local;
    const openos_sigaction_t *act = 0;
    openos_sigaction_t *old = 0;

    if (act_ptr != 0) {
        if (!validate_user_buf(act_ptr, sizeof(openos_sigaction_t))) {
            return (uint64_t)-1;
        }
        act_local = *(const openos_sigaction_t *)act_ptr;
        act = &act_local;
    }
    if (old_ptr != 0) {
        if (!validate_user_buf(old_ptr, sizeof(openos_sigaction_t))) {
            return (uint64_t)-1;
        }
        old = &old_local;
    }
    int rc = arch_x86_64_proc_sigaction((int)sig, act, old);
    if (rc != 0) {
        return (uint64_t)-1;
    }
    if (old_ptr != 0) {
        *(openos_sigaction_t *)old_ptr = old_local;
    }
    return 0;
}

/*
 * SYS_RT_SIGPROCMASK (469): adjust the CURRENT process's blocked mask.
 * a0=how (BLOCK/UNBLOCK/SETMASK), a1=const uint64* set (may be 0 -> query),
 * a2=uint64* oldset (may be 0). SIGKILL/SIGSTOP are silently unblockable.
 * Returns 0 / -1.
 */
static uint64_t do_rt_sigprocmask(uint64_t how, uint64_t set_ptr,
                                  uint64_t oldset_ptr) {
    uint64_t set = 0;
    uint64_t oldset = 0;
    int query_only = 0;

    if (set_ptr != 0) {
        if (!validate_user_buf(set_ptr, sizeof(uint64_t))) {
            return (uint64_t)-1;
        }
        set = *(const uint64_t *)set_ptr;
    } else {
        /* No new set: keep the current mask (query). SETMASK with a NULL
         * set would clobber, so treat NULL as "don't change". */
        query_only = 1;
    }
    if (oldset_ptr != 0 &&
        !validate_user_buf(oldset_ptr, sizeof(uint64_t))) {
        return (uint64_t)-1;
    }
    int rc;
    if (query_only) {
        /* BLOCK with an empty set is a no-op that still returns oldset. */
        rc = arch_x86_64_proc_sigprocmask(OPENOS_SIG_BLOCK, 0, &oldset);
    } else {
        rc = arch_x86_64_proc_sigprocmask((int)how, set, &oldset);
    }
    if (rc != 0) {
        return (uint64_t)-1;
    }
    if (oldset_ptr != 0) {
        *(uint64_t *)oldset_ptr = oldset;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * M4.1b memory syscalls — mmap / munmap / mprotect / brk / sbrk.
 *
 * These delegate to vmem64, which in the current kernel-backed mode carves
 * anonymous pages straight from the PMM and hands back their identity-mapped
 * address. The ABI matches the classic POSIX shapes so a later per-process
 * address-space backing is a drop-in without touching callers.
 * ------------------------------------------------------------------------- */

/* SYS_MMAP(addr, length, prot, flags, fd, offset): anonymous mapping only for
 * now — addr hint / fd / offset are ignored, flags unused. Returns the base
 * address or MAP_FAILED ((uint64_t)-1). */
static uint64_t do_mmap(uint64_t addr, uint64_t length, uint64_t prot,
                        uint64_t flags, uint64_t fd, uint64_t offset) {
    (void)addr; (void)flags; (void)fd; (void)offset;
    return arch_x86_64_vmem_mmap(length, (int)prot);
}

/* SYS_MUNMAP(addr, length): release a whole region. 0 / -1. */
static uint64_t do_munmap(uint64_t addr, uint64_t length) {
    return (uint64_t)(int64_t)arch_x86_64_vmem_munmap(addr, length);
}

/* SYS_MPROTECT(addr, length, prot): 0 / -1. */
static uint64_t do_mprotect(uint64_t addr, uint64_t length, uint64_t prot) {
    return (uint64_t)(int64_t)arch_x86_64_vmem_mprotect(addr, length, (int)prot);
}

/* SYS_BRK(addr): set program break; addr==0 queries. Returns new break. */
static uint64_t do_brk(uint64_t addr) {
    return arch_x86_64_vmem_brk(addr);
}

/* SYS_SBRK(delta): grow/shrink heap; returns previous break or MAP_FAILED. */
static uint64_t do_sbrk(uint64_t delta) {
    return arch_x86_64_vmem_sbrk((int64_t)delta);
}

/*
 * SYS_YIELD: Step E.1 routes the call through proc64's cooperative yield
 * counter. The dispatcher itself stays branchless so future sched64 work
 * only has to swap proc64_yield()'s body for a real reschedule. We still
 * return 0 (success) — POSIX semantics for sched_yield().
 */
static uint64_t do_yield(void) {
    (void)arch_x86_64_proc_yield();
    return 0;
}

/* γ.4 S2c — helper: unlink a zombie child from parent->zombie_head by pid.
 * Returns the zombie PCB pointer with zombie_next cleared, or NULL if no
 * zombie in the chain has that pid. Walks the singly-linked list with a
 * `prev` cursor so we can splice out interior nodes. */
static x86_64_proc_t *try_reap_zombie_by_pid(x86_64_proc_t *parent, uint32_t pid) {
    /* γ.4 S2d: this splices a node out of zombie_head, which do_exit on
     * another CPU may be prepending to concurrently. Serialise with the
     * global family lock. The critical section is O(N<=8) with no nested
     * acquire and no blocking, so it is safe to hold briefly here; callers
     * that spin (waitpid live-child path) call us repeatedly WITHOUT the
     * lock held across their hlt, so no deadlock. */
    arch_x86_64_proc_family_lock();
    uint16_t prev = OPENOS_X86_64_PROC_INVALID_INDEX;
    uint16_t cur  = parent->zombie_head;
    while (cur != OPENOS_X86_64_PROC_INVALID_INDEX) {
        x86_64_proc_t *z = arch_x86_64_proc_slot(cur);
        if (z == NULL) break;
        if (z->pid == pid) {
            uint16_t nxt = z->zombie_next;
            if (prev == OPENOS_X86_64_PROC_INVALID_INDEX) {
                parent->zombie_head = nxt;
            } else {
                x86_64_proc_t *pp = arch_x86_64_proc_slot(prev);
                if (pp != NULL) pp->zombie_next = nxt;
            }
            z->zombie_next = OPENOS_X86_64_PROC_INVALID_INDEX;
            arch_x86_64_proc_family_unlock();
            return z;
        }
        prev = cur;
        cur  = z->zombie_next;
    }
    arch_x86_64_proc_family_unlock();
    return NULL;
}

/* γ.4 S2c — helper: is `pid` currently in parent's live children list?
 * Used by waitpid(specific_pid) to decide between "block until this one
 * exits" and "ECHILD, we've never heard of it". */
static bool is_live_child_pid(x86_64_proc_t *parent, uint32_t pid) {
    uint16_t cur = parent->children_head;
    while (cur != OPENOS_X86_64_PROC_INVALID_INDEX) {
        x86_64_proc_t *c = arch_x86_64_proc_slot(cur);
        if (c == NULL) break;
        if (c->pid == pid) return true;
        cur = c->sibling_next;
    }
    return false;
}

/* γ.4 S2c — write the classic wait status word (exit code in bits 8..15)
 * to user memory, tolerating USER_VBASE mapping. Shared between the
 * wait-any and waitpid(pid) return paths. */
static void wait_write_status(uint64_t status_ptr, int code) {
    if (status_ptr == 0) return;
    uintptr_t status_addr = (uintptr_t)status_ptr;
    if (status_addr >= OPENOS_X86_64_USER_VBASE) {
        status_addr -= OPENOS_X86_64_USER_VBASE;
    }
    int *status_out = (int *)status_addr;
    *status_out = (code & 0xFF) << 8;
}

static uint64_t do_wait_common(uint64_t pid_arg, uint64_t status_ptr, int use_pid) {
    x86_64_proc_t *p = arch_x86_64_proc_current();
    if (p == NULL) return (uint64_t)-1;

    /* γ.4 S2c — waitpid(specific_pid) fast path. Separate from wait-any
     * because the semantics differ: we don't pop the zombie_head LIFO,
     * we pluck the matching pid out of the chain (possibly interior).
     * If the pid is a live child we busy-poll until do_exit moves it
     * onto zombie_head, then splice-out.  A pid that is neither live
     * nor zombie is ECHILD. */
    if (use_pid && pid_arg != (uint64_t)-1 && pid_arg != 0) {
        uint32_t target_pid = (uint32_t)pid_arg;
        /* Already dead? */
        x86_64_proc_t *z = try_reap_zombie_by_pid(p, target_pid);
        if (z == NULL) {
            /* Not dead. Live child? If not, ECHILD. */
            if (!is_live_child_pid(p, target_pid)) {
                return (uint64_t)-1;
            }
            /* Live child — spin until do_exit lifts it onto zombie_head,
             * then splice out. Same STI/CLI dance as the wait-any path so
             * the LAPIC keeps ticking. */
            __asm__ volatile ("sti" ::: "memory");
            while ((z = try_reap_zombie_by_pid(p, target_pid)) == NULL) {
                /* γ.4 S2c fix: hlt (not pause) so the BSP yields host
                 * cycles to the APs under QEMU/TCG. See the wait-any path
                 * below for the full rationale. */
                __asm__ volatile ("hlt" ::: "memory");
            }
            __asm__ volatile ("cli" ::: "memory");
        }
        /* Got the zombie. Drain flag mirrors zombie_head emptiness. */
        int code      = z->child_exit_code;
        int child_pid = (int)z->pid;
        if (p->zombie_head == OPENOS_X86_64_PROC_INVALID_INDEX) {
            p->child_exited    = false;
            p->child_pid       = 0;
            p->child_exit_code = 0;
        }
        wait_write_status(status_ptr, code);
        p->fork_pending = 0;
        return (uint64_t)(uint32_t)child_pid;
    }

    /* γ.4 S2b — no live children AND no zombies => nothing to wait for.
     * children_head is the live-child list; zombie_head is the
     * exited-but-not-reaped list. Either being non-empty means there is
     * (or will be) something to reap. Legacy: if a caller hand-set
     * child_pid without going through fork() we still honor it via the
     * old singleton fallback below. */
    if (p->children_head == OPENOS_X86_64_PROC_INVALID_INDEX &&
        p->zombie_head   == OPENOS_X86_64_PROC_INVALID_INDEX &&
        p->child_pid == 0) {
        return (uint64_t)-1;
    }
    /* waitpid(-1) falls through to the wait-any path below; specific-pid
     * cases were handled by the S2c fast path above. */
    (void)use_pid;

    int code;
    int child_pid;
    /* gamma.3b-S2a Seg-7: two-mode wait.
     *
     * Mode A (S2a new): child is running as a real sched_slot on an AP.
     *   fork_pending was cleared by fork_capture after slot_wakeup, so we
     *   detect Mode A by (fork_pending == 0 && child_pid != 0). In this
     *   mode we simply spin on p->child_exited, which the AP-side do_exit
     *   Seg-6 branch will set via arch_x86_64_proc_slot(ppid). Busy-loop
     *   with a pause hint; hlt / IPI wakeup is S2b territory.
     *
     * Mode B (legacy): fork_pending is still set -> child never went to
     *   an AP, so kmain/usermode_run has to hand-dispatch it under the
     *   parent's kernel stack. This is the pre-S2a path; unchanged. */
    if (p->fork_pending == 0u) {
        /* Mode A: busy-poll until AP child sets parent->child_exited via
         * cross-CPU write in do_exit. Interrupts were masked on syscall
         * entry by FMASK; re-enable IF for the wait so the LAPIC can keep
         * time-slicing this CPU (not strictly required for correctness —
         * the AP-side write is cache-coherent — but keeps the CPU
         * responsive to other work). */
        __asm__ volatile ("sti" ::: "memory");
        while (p->zombie_head == OPENOS_X86_64_PROC_INVALID_INDEX &&
               !p->child_exited) {
            /* γ.4 S2a fix: hlt instead of a raw pause spin. A busy pause
             * loop keeps the BSP 100% hot, which under QEMU/TCG (single
             * host thread multiplexing all vCPUs) starves the APs running
             * our child threads -> reaping degrades to minutes. hlt parks
             * the BSP until the next LAPIC tick (or the child's do_exit
             * cross-CPU IPI), yielding host cycles to the APs. IF=1 was set
             * by the sti above; the AP-side write is cache-coherent so we
             * never miss the wakeup between the test and the hlt. */
            __asm__ volatile ("hlt" ::: "memory");
        }
        __asm__ volatile ("cli" ::: "memory");

        /* γ.4 S2b — pop one zombie off the head. Order is LIFO (newest
         * exit first) which is fine because user-space is expected to
         * treat wait() results as an unordered set. If zombie_head is
         * empty here it means the legacy singleton path fired (fork
         * without list linkage, or Mode B fallback); fall through.
         *
         * γ.4 S2d: hold the family lock across the pop so a do_exit
         * prepend on another CPU cannot splice a node in between our read
         * of zombie_head and our write of z->zombie_next back into it.
         * The section is short and non-blocking (interrupts already masked
         * by the cli above), so no deadlock with the hlt spin, which sits
         * entirely before this point WITHOUT the lock held. */
        arch_x86_64_proc_family_lock();
        if (p->zombie_head != OPENOS_X86_64_PROC_INVALID_INDEX) {
            uint16_t zslot = p->zombie_head;
            x86_64_proc_t *z = arch_x86_64_proc_slot(zslot);
            if (z != NULL) {
                p->zombie_head = z->zombie_next;
                z->zombie_next = OPENOS_X86_64_PROC_INVALID_INDEX;
                code      = z->child_exit_code;
                child_pid = (int)z->pid; /* child's own pid */

                /* Once drained, clear the legacy "has zombie" flag so a
                 * subsequent wait() call blocks correctly. If there are
                 * more zombies still queued, keep it set. */
                if (p->zombie_head == OPENOS_X86_64_PROC_INVALID_INDEX) {
                    p->child_exited = false;
                }
            } else {
                code      = p->child_exit_code;
                child_pid = p->child_pid;
                p->child_exited = false;
            }
        } else {
            code      = p->child_exit_code;
            child_pid = p->child_pid;
            p->child_exited = false;
        }
        arch_x86_64_proc_family_unlock();
    } else {
        /* Mode B: legacy hand-dispatch. */
        code = arch_x86_64_usermode_run_pending_child_for_wait();
        if (!p->child_exited) return (uint64_t)-1;
        child_pid = p->child_pid;
        p->child_exited    = false;
        p->child_exit_code = 0;
    }

    /* Minimal wait status encoding: normal exit, code in bits 8..15.
     * P4.c keeps user-visible stack pointers in USER_VBASE while the
     * kernel writes the same pages through their low-half identity alias. */
    wait_write_status(status_ptr, code);

    /* Legacy singleton mirrors: only clear when *no* zombies remain queued;
     * otherwise subsequent wait() calls would treat the parent as having no
     * children left. */
    if (p->zombie_head == OPENOS_X86_64_PROC_INVALID_INDEX) {
        p->child_pid       = 0;
        p->child_exit_code = 0;
    }
    p->fork_pending = 0;
    return (uint64_t)(uint32_t)child_pid;
}


/* ---------------------------------------------------------------------------
 * Common dispatch table.
 * ------------------------------------------------------------------------- */

uint64_t arch_x86_64_syscall_dispatch_common(uint64_t num,
                                             uint64_t a0,
                                             uint64_t a1,
                                             uint64_t a2,
                                             uint64_t a3,
                                             uint64_t a4,
                                             uint64_t a5) {
    /* a3..a5 reserved for syscalls with more than 3 args; Step E.3 uses a3/a4
     * for sendto/recvfrom (flags + port slot). a5 is still unused. */
    (void)a5;

    ++dispatch_total_count;

    switch (num) {
    /* -------- process / thread -------- */
    case SYS_EXIT:        return do_exit(a0);
    /* Step E.1: identity syscalls now read proc64's current PCB. */
    case SYS_GETPID:      return (uint64_t)arch_x86_64_proc_current_pid();
    case SYS_GETTID:      return (uint64_t)arch_x86_64_proc_current_tid();
    case SYS_GETPPID:     return (uint64_t)arch_x86_64_proc_current_ppid();
    case SYS_GETUID:      return (uint64_t)arch_x86_64_proc_current_uid();
    case SYS_GETGID:      return (uint64_t)arch_x86_64_proc_current_gid();
    case SYS_YIELD:       return do_yield();
    case SYS_WAIT:        return do_wait_common(0, a0, 0);
    case SYS_WAITPID:     return do_wait_common(a0, a1, 1);

    /* -------- I/O (read-only initrd + early console) -------- */
    case SYS_WRITE:       return do_write(a0, a1, a2);
    case SYS_READ:        return do_read(a0, a1, a2);
    case SYS_READ_FD:     return do_read(a0, a1, a2);
    case SYS_OPEN:        return do_open(a0, a1, a2);
    case SYS_CLOSE:       return do_close(a0);
    case SYS_PIPE:        return do_pipe(a0);
    case SYS_MKFIFO:      return do_mkfifo(a0, a1);
    case SYS_SHM_CREATE:  return do_shm_create(a0, a1);
    case SYS_SHM_MAP:     return do_shm_map(a0);
    case SYS_SHM_DETACH:  return do_shm_detach(a0);
    case SYS_SHM_DESTROY: return do_shm_destroy(a0);
    case SYS_SHM_INFO:    return do_shm_info(a0, a1);
    case SYS_DUP:         return do_dup(a0);
    case SYS_DUP2:        return do_dup2(a0, a1);
    /* -------- M4.1a: seek + file metadata (unified VFS) -------- */
    case SYS_SEEK:        return do_lseek(a0, a1, a2);
    case SYS_STAT:        return do_stat(a0, a1);
    case SYS_LSTAT:       return do_lstat(a0, a1);
    case SYS_FSTAT:       return do_fstat(a0, a1);
    case SYS_MKDIR:       return do_mkdir(a0, a1);
    case SYS_UNLINK:      return do_unlink(a0);
    case SYS_RENAME:      return do_rename(a0, a1);
    /* -------- H.3 execve -------- */
    case SYS_EXEC:        early_console64_write("[x86_64][disp] SYS_EXEC case\n"); return do_exec(a0, a1, a2);

    /* -------- memory -------- */
    case SYS_MALLOC:      return do_malloc(a0);
    case SYS_FREE:        return do_free(a0);

    /* -------- M5.1d 惰性绑定 PLT/GOT -------- */
    case SYS_DL_RESOLVE:  return do_dl_resolve(a0, a1);
    case SYS_CLONE:       return do_clone(a0, a1, a2, a3, a4);
    case SYS_FUTEX_WAIT:         return do_futex_wait(a0, a1);
    case SYS_FUTEX_WAKE:         return do_futex_wake(a0, a1);
    case SYS_FUTEX_WAIT_TIMEOUT: return do_futex_wait_timeout(a0, a1, a2);

    /* -------- time -------- */
    case SYS_UPTIME_MS:   return do_uptime_ms();

    /* -------- time / clock (M4.1) -------- */
    case SYS_CLOCK_GETTIME: return do_clock_gettime(a0, a1);
    case SYS_SLEEP:         return do_sleep(a0);
    case SYS_NANOSLEEP:     return do_nanosleep(a0, a1);
    case SYS_GETTIMEOFDAY:  return do_gettimeofday(a0, a1);

    /* -------- device control / process signal (M4.1d) -------- */
    case SYS_IOCTL:         return do_ioctl(a0, a1, a2);
    case SYS_KILL:          return do_kill(a0, a1);
    case SYS_RT_SIGACTION:  return do_rt_sigaction(a0, a1, a2);
    case SYS_RT_SIGPROCMASK: return do_rt_sigprocmask(a0, a1, a2);

    /* -------- job control (M4.4b) -------- */
    case SYS_SETPGID:       return do_setpgid(a0, a1);
    case SYS_GETPGID:       return do_getpgid(a0);
    case SYS_SETSID:        return do_setsid();
    case SYS_GETSID:        return do_getsid(a0);

    /* -------- memory (M4.1b) -------- */
    case SYS_MMAP:          return do_mmap(a0, a1, a2, a3, a4, a5);
    case SYS_MUNMAP:        return do_munmap(a0, a1);
    case SYS_MPROTECT:      return do_mprotect(a0, a1, a2);
    case SYS_BRK:           return do_brk(a0);
    case SYS_SBRK:          return do_sbrk(a0);

    /* -------- net (Step E.3, loopback only) -------- */
    case SYS_SOCKET:      return arch_x86_64_sys_socket(a0, a1, a2);
    case SYS_BIND:        return arch_x86_64_sys_bind(a0, a1);
    case SYS_SENDTO:      return arch_x86_64_sys_sendto(a0, a1, a2, a3, a4);
    case SYS_RECVFROM:    return arch_x86_64_sys_recvfrom(a0, a1, a2, a3, a4);

    /* -------- net (M1.5.3, live virtio-net TCP/IP stack) -------- */
    case SYS_NETINFO:     return arch_x86_64_sys_netinfo(a0);
    case SYS_PING:        return arch_x86_64_sys_ping(a0, a1);
    case SYS_NETCONFIG:   return arch_x86_64_sys_netconfig(a0, a1, a2, a3, a4);
    case SYS_DNSLOOKUP:   return arch_x86_64_sys_dnslookup(a0, a1);

    /* -------- M1.7 ring3 TCP -------- */
    case SYS_TCP_CONNECT: return arch_x86_64_sys_tcp_connect(a0, a1);
    case SYS_TCP_SEND:    return arch_x86_64_sys_tcp_send(a0, a1, a2);
    case SYS_TCP_RECV:    return arch_x86_64_sys_tcp_recv(a0, a1, a2, a3);
    case SYS_TCP_CLOSE:   return arch_x86_64_sys_tcp_close(a0);
    case SYS_HTTP_GET:    return arch_x86_64_sys_http_get(a0, a1, a2, a3);

    default:
        ++dispatch_enosys_count;
        return (uint64_t)-1;
    }
}

void arch_x86_64_syscall_dispatch_reset(void) {
    dispatch_total_count = 0;
    dispatch_enosys_count = 0;
    for (unsigned i = 0; i < sizeof(dispatch_per_num_count)/sizeof(dispatch_per_num_count[0]); ++i)
        dispatch_per_num_count[i] = 0;
}

uint64_t arch_x86_64_syscall_dispatch_total(void) {
    return dispatch_total_count;
}

uint64_t arch_x86_64_syscall_dispatch_enosys(void) {
    return dispatch_enosys_count;
}

/* Silence unused-static warnings for helpers we kept for forthcoming wiring. */
static void __attribute__((unused)) _keep_alive_refs(void) {
    (void)k_strcmp;
    (void)k_strlen;
    (void)dispatch_per_num_count;
}
