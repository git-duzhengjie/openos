/*
 * fifo64.h — M4.3b: named pipes (FIFO) registry.
 *
 * A FIFO is a filesystem-visible rendezvous point identified by a path. Unlike
 * an anonymous pipe (created by pipe(2), which hands back two fds directly), a
 * FIFO is created once with mkfifo(path) and then opened by name any number of
 * times. All opens of the same path share a single underlying byte stream.
 *
 * Design: this module keeps a small fixed table mapping path -> pipe64 ring.
 * The heavy lifting (ring buffer, blocking, poll, waiter queues) is reused
 * wholesale from pipe64; fifo64 only adds the name registry and per-end open
 * reference counting so that a FIFO's backing pipe is created lazily on first
 * open and torn down when the last reader AND writer have closed.
 *
 * This keeps the module small and avoids duplicating ring/blocking logic.
 */
#ifndef OPENOS_ARCH_X86_64_FIFO64_H
#define OPENOS_ARCH_X86_64_FIFO64_H

#include <stdint.h>

#define FIFO64_MAX        16   /* max simultaneously registered FIFOs      */
#define FIFO64_PATH_MAX   64   /* max path length (incl NUL)               */

/* Open direction flags for fifo64_open(). */
#define FIFO64_O_READ   0x01
#define FIFO64_O_WRITE  0x02

/*
 * Create a named FIFO at path. Returns 0 on success, -1 on invalid args,
 * -2 if the table is full, -3 if the path already exists (EEXIST).
 * The backing pipe64 ring is NOT allocated here; it is created lazily on the
 * first open so that an unopened FIFO consumes no ring slot.
 */
int fifo64_mkfifo(const char *path, int mode);

/*
 * Look up a registered FIFO by path. Returns its fifo index (>=0) or -1 if no
 * such FIFO exists.
 */
int fifo64_lookup(const char *path);

/*
 * Open an end of a named FIFO. dir is FIFO64_O_READ or FIFO64_O_WRITE.
 * On the first open the backing pipe64 ring is allocated. Returns the pipe64
 * id (>=0) to be wrapped by the syscall fd layer, or -1 on error (no such
 * FIFO / bad dir / out of rings).
 *
 * Reference counting: each open bumps the matching end's ref count on the
 * shared ring; the ring's own read_refs/write_refs are managed to reflect
 * this so pipe64_poll / EOF / EPIPE semantics behave correctly.
 */
int fifo64_open(const char *path, int dir);

/*
 * Close one end of a FIFO previously opened via fifo64_open. Decrements the
 * ring's matching ref; when both reader and writer refs reach zero the FIFO's
 * backing ring is released (but the name registration persists until
 * fifo64_unlink). Returns 0 on success, -1 on bad path.
 */
int fifo64_close(const char *path, int dir);

/*
 * Remove a FIFO name registration. Returns 0 on success, -1 if not found.
 * If the backing ring is still open this only unlinks the name; the ring
 * lives until its last end closes (POSIX unlink-while-open semantics).
 */
int fifo64_unlink(const char *path);

/* Diagnostics. */
int fifo64_active_count(void);      /* number of registered FIFOs */
int fifo64_pipe_id(const char *path); /* backing pipe64 id or -1 */

/* Reset the whole table (init / teardown). */
void fifo64_reset(void);

#endif /* OPENOS_ARCH_X86_64_FIFO64_H */
