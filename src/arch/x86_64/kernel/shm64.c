/*
 * shm64.c — M4.3c: System V-style shared memory segments.
 *
 * See shm64.h for the design rationale. Segments are backed by contiguous
 * physical pages from the PMM; because the kernel is identity-mapped, the
 * physical base doubles as a directly usable virtual address, so two attachers
 * observe the same bytes. The table is fixed-size and self-contained.
 */
#include "../include/shm64.h"
#include "../include/pmm64.h"

#define SHM64_PAGE_SIZE 4096u

typedef struct {
    int      used;        /* slot allocated                       */
    int      destroyed;   /* marked IPC_RMID, pending last detach  */
    uint32_t key;         /* lookup key (0 => anonymous)           */
    uint64_t size;        /* requested byte size                   */
    uint64_t npages;      /* pages actually reserved               */
    uint64_t phys_base;   /* PMM physical base (== virt, identity)  */
    int      nattch;      /* current attach count                  */
} shm64_seg_t;

static shm64_seg_t g_segs[SHM64_MAX];
static int         g_ready = 0;

/* ------------------------------------------------------------------ */

static void shm64_lazy_init(void)
{
    if (g_ready) return;
    for (int i = 0; i < SHM64_MAX; ++i) {
        g_segs[i].used = 0;
        g_segs[i].destroyed = 0;
        g_segs[i].key = 0;
        g_segs[i].size = 0;
        g_segs[i].npages = 0;
        g_segs[i].phys_base = 0;
        g_segs[i].nattch = 0;
    }
    g_ready = 1;
}

/* Free a segment's backing pages and clear the slot. */
static void shm64_free_slot(shm64_seg_t *s)
{
    if (s->phys_base && s->npages) {
        for (uint64_t i = 0; i < s->npages; ++i) {
            arch_x86_64_pmm_free_page(s->phys_base + i * SHM64_PAGE_SIZE);
        }
    }
    s->used = 0;
    s->destroyed = 0;
    s->key = 0;
    s->size = 0;
    s->npages = 0;
    s->phys_base = 0;
    s->nattch = 0;
}

void shm64_reset(void)
{
    shm64_lazy_init();
    for (int i = 0; i < SHM64_MAX; ++i) {
        if (g_segs[i].used) {
            shm64_free_slot(&g_segs[i]);
        }
    }
}

/* Locate a live segment by non-zero key. Returns index or -1. */
static int find_by_key(uint32_t key)
{
    if (key == 0) return -1; /* anonymous never matches a lookup */
    for (int i = 0; i < SHM64_MAX; ++i) {
        if (g_segs[i].used && !g_segs[i].destroyed && g_segs[i].key == key) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */

int shm64_get(uint32_t key, uint64_t size)
{
    shm64_lazy_init();
    if (size == 0) {
        return -1;
    }
    /* Existing keyed segment: return it (size argument ignored). */
    int existing = find_by_key(key);
    if (existing >= 0) {
        return existing;
    }
    /* Round size up to whole pages, clamp to the per-segment cap. */
    uint64_t npages = (size + SHM64_PAGE_SIZE - 1) / SHM64_PAGE_SIZE;
    if (npages == 0 || npages > SHM64_MAX_PAGES) {
        return -1;
    }
    for (int i = 0; i < SHM64_MAX; ++i) {
        if (!g_segs[i].used) {
            uint64_t base = arch_x86_64_pmm_alloc_pages(npages);
            if (base == 0) {
                return -1; /* out of physical memory */
            }
            g_segs[i].used = 1;
            g_segs[i].destroyed = 0;
            g_segs[i].key = key;
            g_segs[i].size = size;
            g_segs[i].npages = npages;
            g_segs[i].phys_base = base;
            g_segs[i].nattch = 0;
            return i;
        }
    }
    return -1; /* table full */
}

static shm64_seg_t *seg_get(int shm_id)
{
    shm64_lazy_init();
    if (shm_id < 0 || shm_id >= SHM64_MAX) {
        return 0;
    }
    if (!g_segs[shm_id].used) {
        return 0;
    }
    return &g_segs[shm_id];
}

uint64_t shm64_attach(int shm_id)
{
    shm64_seg_t *s = seg_get(shm_id);
    if (!s || s->destroyed) {
        return SHM64_ATTACH_FAILED;
    }
    s->nattch++;
    return s->phys_base;
}

int shm64_detach(int shm_id)
{
    shm64_seg_t *s = seg_get(shm_id);
    if (!s) {
        return -1;
    }
    if (s->nattch <= 0) {
        return -2;
    }
    s->nattch--;
    /* Deferred destruction: free once the last attacher leaves. */
    if (s->destroyed && s->nattch == 0) {
        shm64_free_slot(s);
    }
    return 0;
}

int shm64_rmid(int shm_id)
{
    shm64_seg_t *s = seg_get(shm_id);
    if (!s) {
        return -1;
    }
    if (s->nattch == 0) {
        /* No attachers: free immediately. */
        shm64_free_slot(s);
    } else {
        /* Attachers remain: defer to last detach. */
        s->destroyed = 1;
    }
    return 0;
}

uint64_t shm64_size(int shm_id)
{
    shm64_seg_t *s = seg_get(shm_id);
    return s ? s->size : (uint64_t)-1;
}

int shm64_nattch(int shm_id)
{
    shm64_seg_t *s = seg_get(shm_id);
    return s ? s->nattch : 0;
}

uint64_t shm64_base(int shm_id)
{
    shm64_seg_t *s = seg_get(shm_id);
    return s ? s->phys_base : (uint64_t)-1;
}

int shm64_active_count(void)
{
    shm64_lazy_init();
    int n = 0;
    for (int i = 0; i < SHM64_MAX; ++i) {
        if (g_segs[i].used && !g_segs[i].destroyed) {
            ++n;
        }
    }
    return n;
}
