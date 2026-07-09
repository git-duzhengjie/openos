/*
 * sfdtable64.h — Syscall FD Indirection Table (M4.1a; OFD layer added M4.1c)
 *
 * Purpose: syscall-visible file descriptors (fd >= 3) are decoupled from the
 * underlying backend via a per-slot pointer to an "open file description"
 * (OFD). Slots 0/1/2 are permanently reserved for the stdio console path.
 *
 * M4.1c: an OFD carries a backend kind (VFS file or pipe end) plus a
 * reference count so that dup/dup2 can share one description (and thus one
 * VFS seek offset / one pipe end) across several syscall fds. The backend is
 * only torn down when the last referencing fd is closed.
 */
#ifndef OPENOS_ARCH_X86_64_SFDTABLE64_H
#define OPENOS_ARCH_X86_64_SFDTABLE64_H

#ifdef __cplusplus
extern "C" {
#endif

/* Total syscall fd slots; slot index == syscall fd. */
#define SFD_MAX   256
/* First fd handed out to userspace (0/1/2 reserved for stdio). */
#define SFD_FIRST 3

/* OFD backend kinds. */
#define SFD_KIND_NONE 0
#define SFD_KIND_VFS  1  /* backend is a unified-VFS fd         */
#define SFD_KIND_PIPE 2  /* backend is one end of a pipe64 pipe */

/* Reset the whole table (init / teardown). */
void sfd_reset(void);

/*
 * Allocate a syscall fd backed by a unified-VFS fd. Creates a fresh OFD with
 * refcount 1. Returns the new syscall fd (>= SFD_FIRST) or -1 if full.
 */
int sfd_alloc(int vfs_fd);

/*
 * Allocate a syscall fd backed by one end of a pipe. is_write selects the
 * write (1) or read (0) end. Fresh OFD, refcount 1. Returns fd or -1.
 */
int sfd_alloc_pipe(int pipe_id, int is_write);

/*
 * Resolve a syscall fd to its underlying VFS fd. Returns the VFS fd, or -1 if
 * invalid or not a VFS-backed descriptor.
 */
int sfd_resolve(int sfd);

/* Backend kind of a syscall fd (SFD_KIND_*), or SFD_KIND_NONE if invalid. */
int sfd_kind(int sfd);

/* Pipe accessors for a pipe-backed fd; -1 if not pipe-backed. */
int sfd_pipe_id(int sfd);
int sfd_pipe_is_write(int sfd);

/*
 * Duplicate a syscall fd. dup: newfd = lowest free slot. dup2: caller-chosen
 * newfd (closed first if in use). Both share oldfd's OFD (refcount++).
 * Returns newfd or -1. dup2 with oldfd==newfd (valid) returns newfd as-is.
 */
int sfd_dup(int oldfd);
int sfd_dup2(int oldfd, int newfd);

/*
 * Close a syscall fd: drop this slot's OFD reference. When the OFD refcount
 * hits 0 the caller learns which backend to release via out-params:
 *   *out_kind = SFD_KIND_VFS  -> *out_a = vfs fd
 *   *out_kind = SFD_KIND_PIPE -> *out_a = pipe id, *out_b = is_write
 *   *out_kind = SFD_KIND_NONE -> still shared, nothing to release
 * Returns 0 on success, -1 on invalid fd.
 */
int sfd_close(int sfd, int *out_kind, int *out_a, int *out_b);

/* Diagnostics: number of syscall fds currently open (>= SFD_FIRST). */
int sfd_open_count(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_ARCH_X86_64_SFDTABLE64_H */
