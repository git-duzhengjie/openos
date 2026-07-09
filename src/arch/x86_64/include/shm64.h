/*
 * shm64.h — M4.3c: System V-style shared memory segments.
 *
 * A shared memory segment is a run of PMM-backed pages identified by an
 * integer key. Any number of "attachers" can map the same segment and observe
 * each other's writes through the identity-mapped physical address. Because
 * the kernel runs identity-mapped in M4.x, attach simply returns the segment's
 * physical base address; two attachers of the same segment therefore share the
 * exact same bytes. When a real per-process address space lands later, attach
 * will map the shared physical pages into each AS without an ABI change.
 *
 * Lifecycle mirrors System V shmget/shmat/shmdt/shmctl(IPC_RMID):
 *   - shmget(key, size)  -> create-or-lookup a segment, returns shm id
 *   - shmat(id)          -> attach, bumps attach count, returns base address
 *   - shmdt(id)          -> detach, drops attach count
 *   - shmctl_rmid(id)    -> mark for destruction; pages freed when last detach
 *
 * The module keeps a small fixed table. It has no dependency on the process
 * model, so it is trivially unit-testable from the syscall self-test.
 */
#ifndef OPENOS_ARCH_X86_64_SHM64_H
#define OPENOS_ARCH_X86_64_SHM64_H

#include <stdint.h>

#define SHM64_MAX        16          /* max simultaneous segments        */
#define SHM64_MAX_PAGES  256         /* cap one segment at 1 MiB (256*4K)*/

/* Sentinel returned by shm64_attach on failure. */
#define SHM64_ATTACH_FAILED  ((uint64_t)-1)

/*
 * Create a new segment for key of at least size bytes, or look up the existing
 * segment for key (in which case size is ignored). Returns a segment id (>=0)
 * or -1 on error (bad size / table full / out of physical memory).
 *
 * key == 0 (IPC_PRIVATE analogue) always creates a brand-new anonymous segment
 * that will not be found by a later lookup.
 */
int shm64_get(uint32_t key, uint64_t size);

/*
 * Attach to a segment: bump its attach count and return its base address.
 * Returns SHM64_ATTACH_FAILED on bad id or a segment already destroyed.
 */
uint64_t shm64_attach(int shm_id);

/*
 * Detach from a segment: drop its attach count. If the segment was marked for
 * destruction and the attach count reaches zero, its pages are freed now.
 * Returns 0 on success, -1 on bad id, -2 if attach count was already zero.
 */
int shm64_detach(int shm_id);

/*
 * Mark a segment for destruction (IPC_RMID). If no attachers remain the pages
 * are freed immediately; otherwise destruction is deferred to the last detach.
 * Returns 0 on success, -1 on bad id.
 */
int shm64_rmid(int shm_id);

/* Query the byte size / attach count / base of a segment. -1 / 0 on bad id. */
uint64_t shm64_size(int shm_id);
int      shm64_nattch(int shm_id);
uint64_t shm64_base(int shm_id);

/* Diagnostics: number of live (not-yet-freed) segments. */
int shm64_active_count(void);

/* Reset the whole table (init / teardown): frees all backing pages. */
void shm64_reset(void);

#endif /* OPENOS_ARCH_X86_64_SHM64_H */
