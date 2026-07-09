/*
 * pipe64.h — Anonymous pipe pool (M4.1c)
 *
 * A tiny, self-contained ring-buffer pipe implementation used to back the
 * pipe(2) syscall. Each pipe has a fixed-size ring buffer and independent
 * open-end reference counts for the read and write ends. When both ends are
 * fully closed the pipe slot is recycled.
 *
 * There is no blocking scheduler integration yet: reads on an empty pipe
 * whose write end is still open return 0 (would-block treated as EOF-ish for
 * the current single-threaded self-test path); reads after all write ends
 * are closed return 0 (EOF). Writes to a pipe with no readers return -1
 * (EPIPE). This is enough for the M4.1c self-test and for later shell
 * plumbing once a scheduler-aware blocking layer is added (tracked in M4.3).
 */
#ifndef OPENOS_ARCH_X86_64_PIPE64_H
#define OPENOS_ARCH_X86_64_PIPE64_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Number of pipe slots and per-pipe ring capacity. */
#define PIPE64_MAX      16
#define PIPE64_CAPACITY 4096

/*
 * Create a new pipe. On success returns a pipe id in [0, PIPE64_MAX) with
 * both read and write ends open (refcount 1 each). Returns -1 if the pool
 * is exhausted.
 */
int pipe64_create(void);

/*
 * M4.3b: create a pipe ring with BOTH end refcounts at zero (a "bare" ring).
 * Used to back a named FIFO whose ends are opened independently via
 * pipe64_ref_end. Because the refcounts start at zero the caller MUST bump at
 * least one end before any close, otherwise the ring is never reclaimed by a
 * close (it simply sits idle until pipe64_reset). Returns a pipe id or -1.
 */
int pipe64_create_bare(void);

/*
 * Increment the open-end refcount for one end of a pipe (used by dup/dup2
 * when a pipe-backed descriptor is duplicated). is_write selects the end.
 * Returns 0 on success, -1 on invalid id.
 */
int pipe64_ref_end(int pipe_id, int is_write);

/*
 * Close one end of a pipe (decrement that end's refcount). When both ends
 * reach zero the pipe is recycled. Returns 0 on success, -1 on invalid id.
 */
int pipe64_close_end(int pipe_id, int is_write);

/*
 * Read up to len bytes from the pipe. Returns the number of bytes read
 * (0 if the buffer is empty), or -1 on invalid id / closed read end.
 */
int pipe64_read(int pipe_id, void *buf, uint32_t len);

/*
 * Write up to len bytes into the pipe. Returns the number of bytes written
 * (may be less than len if the ring fills up), -1 on invalid id, or -2 on
 * EPIPE (no open read end).
 */
int pipe64_write(int pipe_id, const void *buf, uint32_t len);

/* Diagnostics: number of currently allocated pipes. */
int pipe64_active_count(void);

/* Reset the whole pool (init / teardown). */
void pipe64_reset(void);

/*
 * ---------------------------------------------------------------------------
 * M4.3a: scheduler-aware blocking support.
 *
 * The ring-buffer primitives above are non-blocking: an empty pipe with an
 * open write end returns 0 from pipe64_read, and a full pipe returns a short
 * write. To implement real blocking pipe(2) semantics the syscall layer needs
 * to (a) distinguish EOF from would-block and (b) park the calling scheduler
 * slot on the pipe until the peer makes progress, then be woken.
 *
 * These helpers expose exactly that: a poll query and a tiny per-end waiter
 * queue that records parked scheduler slot ids. The syscall layer owns the
 * park/yield/wakeup dance; pipe64 only bookkeeps who is waiting.
 * ---------------------------------------------------------------------------
 */

/* Poll result bits, returned by pipe64_poll(). */
#define PIPE64_POLL_READABLE  0x01  /* data available, or read-end EOF   */
#define PIPE64_POLL_WRITABLE  0x02  /* space available for a write       */
#define PIPE64_POLL_HUP       0x04  /* peer end fully closed             */

/* Max scheduler slots that may be parked on a single pipe end at once. */
#define PIPE64_WAITERS_MAX 8

/*
 * Query the readiness of a pipe. Returns a bitmask of PIPE64_POLL_* bits, or
 * -1 on invalid id. An empty pipe with no open write end reports READABLE
 * (so the reader observes EOF rather than blocking forever); a pipe with no
 * open read end reports WRITABLE|HUP (so the writer observes EPIPE).
 */
int pipe64_poll(int pipe_id);

/*
 * Number of bytes currently buffered / free in the ring. Return -1 on bad id.
 */
int pipe64_buffered(int pipe_id);
int pipe64_space(int pipe_id);

/* True (1) if the pipe still has an open write / read end. -1 on bad id. */
int pipe64_has_writer(int pipe_id);
int pipe64_has_reader(int pipe_id);

/*
 * Register the calling scheduler slot as a waiter on one end of the pipe.
 * is_write selects the write-side queue (writer waiting for space) vs the
 * read-side queue (reader waiting for data). Returns 0 on success, -1 on bad
 * id, -2 if that end's waiter queue is full. Duplicate registrations of the
 * same slot are coalesced (idempotent).
 */
int pipe64_wait_add(int pipe_id, int is_write, uint32_t slot);

/*
 * Remove the calling slot from a waiter queue (e.g. on wakeup or abort).
 * Returns 0 on success, -1 on bad id. Missing slot is a no-op success.
 */
int pipe64_wait_remove(int pipe_id, int is_write, uint32_t slot);

/*
 * Drain all waiters from one end into out_slots[] (capacity PIPE64_WAITERS_MAX)
 * and clear that queue. Returns the number of slots written, or -1 on bad id.
 * The syscall layer calls this after making progress to wake the peers.
 */
int pipe64_wait_drain(int pipe_id, int is_write, uint32_t *out_slots);

/* Diagnostics: number of slots parked on one end. -1 on bad id. */
int pipe64_wait_count(int pipe_id, int is_write);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_ARCH_X86_64_PIPE64_H */
