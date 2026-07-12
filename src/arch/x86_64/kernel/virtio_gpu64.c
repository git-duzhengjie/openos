/* =====================================================================
 * virtio-gpu 2D driver (modern PCI transport)
 *
 * Brings up the virtio-gpu 2D pipeline on top of virtio_modern64:
 *   GET_DISPLAY_INFO -> RESOURCE_CREATE_2D -> ATTACH_BACKING
 *   -> SET_SCANOUT, then per-frame TRANSFER_TO_HOST_2D + RESOURCE_FLUSH.
 *
 * Exposes a linear 32bpp BGRA backing store (identity-mapped phys pages)
 * that upper layers draw into; virtio_gpu_flush_rect() pushes dirty
 * regions to the host display.
 * ===================================================================== */

#include "virtio_gpu.h"
#include "virtio_modern.h"
#include "pci.h"
#include "pmm64.h"
#include "serial.h"

#define VIRTIO_VENDOR_ID       0x1AF4u
#define VIRTIO_GPU_DEVICE_ID   0x1050u  /* modern virtio-gpu */
#define VIRTIO_GPU_XFORM_RESID 1u       /* our single 2D resource id */
#define VIRTIO_GPU_CURSOR_RESID 2u      /* hardware cursor sprite resource */
#define GPU_CTRLQ              0        /* controlq index */
#define GPU_CURSORQ            1        /* cursorq index */

/* ---- driver state ---- */
static virtio_modern_dev_t g_dev;
static virtqueue_t         g_ctrlq;
static uint16_t            g_ctrlq_notify_off;
static virtqueue_t         g_cursorq;
static uint16_t            g_cursorq_notify_off;
static uint32_t            g_cursor_ready;   /* cursorq + resource live */
static uint8_t            *g_cursor_fb;      /* 64x64 BGRA sprite backing */
static uint32_t            g_edid_ok;        /* EDID negotiated + parsed */
static uint32_t            g_edid_pref_w;    /* preferred mode width */
static uint32_t            g_edid_pref_h;    /* preferred mode height */
/* enumerated scanouts: how many are enabled + each geometry (mirror mode) */
static uint32_t            g_scanout_count;
static uint32_t            g_scanout_ids[VIRTIO_GPU_MAX_SCANOUTS];
static uint32_t            g_scanout_w[VIRTIO_GPU_MAX_SCANOUTS];
static uint32_t            g_scanout_h[VIRTIO_GPU_MAX_SCANOUTS];
static uint32_t            g_count;
static uint32_t            g_width;
static uint32_t            g_height;
static uint8_t            *g_fb;        /* backing store base (BGRA) */
static uint32_t            g_fb_pages;

/* one shared request+response bounce buffer (single-threaded init/flush) */
static uint8_t            *g_cmd_buf;   /* phys-identity page */
#define CMD_REQ_OFF   0u
#define CMD_RESP_OFF  2048u

/* separate bounce page for cursor-queue commands (req + resp) */
static uint8_t            *g_cur_cmd_buf;
#define CUR_REQ_OFF   0u
#define CUR_RESP_OFF  2048u

static void gpu_memset(void *d, int c, uint64_t n) {
    uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c;
}

/* ---- virtqueue helpers (mirror virtio_net64 algorithm) ---- */
static uint16_t vq_alloc_desc(virtqueue_t *vq) {
    if (vq->num_free == 0) return 0xFFFF;
    uint16_t head = vq->free_head;
    vq->free_head = vq->desc[head].next;
    vq->num_free--;
    return head;
}
static void vq_free_desc(virtqueue_t *vq, uint16_t idx) {
    vq->desc[idx].next = vq->free_head;
    vq->free_head = idx;
    vq->num_free++;
}

/* Issue one control-queue command: req_len bytes at CMD_REQ_OFF are sent
 * (device-readable), resp_len bytes written back into CMD_RESP_OFF
 * (device-writable).  Blocks (polls used ring) until completion.
 * Returns 0 on success, negative on descriptor exhaustion / timeout. */
static int gpu_cmd(uint32_t req_len, uint32_t resp_len) {
    virtqueue_t *vq = &g_ctrlq;
    uint16_t d0 = vq_alloc_desc(vq);
    uint16_t d1 = vq_alloc_desc(vq);
    if (d0 == 0xFFFF || d1 == 0xFFFF) return -1;

    uint64_t base_phys = (uint64_t)(uintptr_t)g_cmd_buf;

    vq->desc[d0].addr  = base_phys + CMD_REQ_OFF;
    vq->desc[d0].len   = req_len;
    vq->desc[d0].flags = VIRTQ_DESC_F_NEXT;
    vq->desc[d0].next  = d1;

    vq->desc[d1].addr  = base_phys + CMD_RESP_OFF;
    vq->desc[d1].len   = resp_len;
    vq->desc[d1].flags = VIRTQ_DESC_F_WRITE;
    vq->desc[d1].next  = 0;

    uint16_t ai = vq->avail->idx % vq->queue_size;
    vq->avail->ring[ai] = d0;
    __asm__ volatile("" ::: "memory");
    vq->avail->idx++;
    __asm__ volatile("" ::: "memory");
    virtio_modern_notify(&g_dev, GPU_CTRLQ, g_ctrlq_notify_off);

    /* poll used ring for completion (bounded spin) */
    uint64_t spins = 0;
    while (vq->last_used == vq->used->idx) {
        if (++spins > 200000000ULL) {
            vq_free_desc(vq, d1);
            vq_free_desc(vq, d0);
            return -2; /* timeout */
        }
        __asm__ volatile("pause" ::: "memory");
    }
    vq->last_used++;
    vq_free_desc(vq, d1);
    vq_free_desc(vq, d0);
    return 0;
}

/* Convenience: fill request header and dispatch, then read response type. */
static uint32_t gpu_resp_type(void) {
    virtio_gpu_ctrl_hdr_t *r = (virtio_gpu_ctrl_hdr_t *)(g_cmd_buf + CMD_RESP_OFF);
    return r->type;
}

/* ---- 2D pipeline commands ---- */

static int gpu_get_display_info(void) {
    virtio_gpu_ctrl_hdr_t *req = (virtio_gpu_ctrl_hdr_t *)(g_cmd_buf + CMD_REQ_OFF);
    gpu_memset(req, 0, sizeof(*req));
    req->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    if (gpu_cmd(sizeof(*req), sizeof(virtio_gpu_resp_display_info_t)) != 0)
        return -1;
    if (gpu_resp_type() != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) return -2;
    virtio_gpu_resp_display_info_t *resp =
        (virtio_gpu_resp_display_info_t *)(g_cmd_buf + CMD_RESP_OFF);
    /* enumerate every enabled scanout (multi-head): record id + geometry.
     * The first enabled scanout also becomes the primary draw geometry. */
    g_scanout_count = 0;
    for (uint32_t i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
        if (!resp->pmodes[i].enabled) continue;
        uint32_t w = resp->pmodes[i].r.width;
        uint32_t h = resp->pmodes[i].r.height;
        if (w == 0 || h == 0) continue;
        g_scanout_ids[g_scanout_count] = i;
        g_scanout_w[g_scanout_count]   = w;
        g_scanout_h[g_scanout_count]   = h;
        if (g_scanout_count == 0) { g_width = w; g_height = h; }
        g_scanout_count++;
    }
    if (g_scanout_count > 0) return 0;
    /* no enabled scanout: fall back to pmode 0 geometry if non-zero */
    if (resp->pmodes[0].r.width && resp->pmodes[0].r.height) {
        g_width  = resp->pmodes[0].r.width;
        g_height = resp->pmodes[0].r.height;
        g_scanout_ids[0] = 0;
        g_scanout_w[0]   = g_width;
        g_scanout_h[0]   = g_height;
        g_scanout_count  = 1;
        return 0;
    }
    return -3;
}

static int gpu_create_2d(void) {
    virtio_gpu_resource_create_2d_t *req =
        (virtio_gpu_resource_create_2d_t *)(g_cmd_buf + CMD_REQ_OFF);
    gpu_memset(req, 0, sizeof(*req));
    req->hdr.type    = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    req->resource_id = VIRTIO_GPU_XFORM_RESID;
    req->format      = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    req->width       = g_width;
    req->height      = g_height;
    if (gpu_cmd(sizeof(*req), sizeof(virtio_gpu_ctrl_hdr_t)) != 0) return -1;
    return (gpu_resp_type() == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -2;
}

static int gpu_attach_backing(void) {
    virtio_gpu_resource_attach_backing_t *req =
        (virtio_gpu_resource_attach_backing_t *)(g_cmd_buf + CMD_REQ_OFF);
    gpu_memset(req, 0, sizeof(*req));
    req->hdr.type    = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    req->resource_id = VIRTIO_GPU_XFORM_RESID;
    req->nr_entries  = 1;
    virtio_gpu_mem_entry_t *ent =
        (virtio_gpu_mem_entry_t *)((uint8_t *)req + sizeof(*req));
    ent->addr    = (uint64_t)(uintptr_t)g_fb;
    ent->length  = g_width * g_height * 4u;
    ent->padding = 0;
    uint32_t req_len = sizeof(*req) + sizeof(*ent);
    if (gpu_cmd(req_len, sizeof(virtio_gpu_ctrl_hdr_t)) != 0) return -1;
    return (gpu_resp_type() == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -2;
}

static int gpu_set_scanout(void) {
    /* Mirror mode: bind our single 2D resource to every enabled scanout so
     * all heads display the same desktop.  Each scanout gets its own rect
     * sized to that head's geometry (clamped to our backing dimensions). */
    for (uint32_t s = 0; s < g_scanout_count; s++) {
        uint32_t w = g_scanout_w[s], h = g_scanout_h[s];
        if (w > g_width)  w = g_width;
        if (h > g_height) h = g_height;
        virtio_gpu_set_scanout_t *req =
            (virtio_gpu_set_scanout_t *)(g_cmd_buf + CMD_REQ_OFF);
        gpu_memset(req, 0, sizeof(*req));
        req->hdr.type    = VIRTIO_GPU_CMD_SET_SCANOUT;
        req->r.x = 0; req->r.y = 0;
        req->r.width = w; req->r.height = h;
        req->scanout_id  = g_scanout_ids[s];
        req->resource_id = VIRTIO_GPU_XFORM_RESID;
        if (gpu_cmd(sizeof(*req), sizeof(virtio_gpu_ctrl_hdr_t)) != 0) return -1;
        if (gpu_resp_type() != VIRTIO_GPU_RESP_OK_NODATA) return -2;
    }
    return 0;
}

/* Create the 64x64 hardware cursor resource + its backing store.
 * Requires the cursor queue to already be set up.  Returns 0 on success. */
static int gpu_setup_cursor(void) {
    /* dedicated 64x64 BGRA backing (one page is plenty: 64*64*4 = 16KB) */
    uint32_t cur_bytes = VIRTIO_GPU_CURSOR_W * VIRTIO_GPU_CURSOR_H * 4u;
    uint32_t cur_pages = (cur_bytes + 4095u) / 4096u;
    x86_64_phys_addr_t cur_phys = arch_x86_64_pmm_alloc_pages(cur_pages);
    if (cur_phys == 0) return -1;
    g_cursor_fb = (uint8_t *)(uintptr_t)cur_phys;
    gpu_memset(g_cursor_fb, 0, cur_pages * 4096u);

    /* RESOURCE_CREATE_2D for the cursor sprite */
    virtio_gpu_resource_create_2d_t *c =
        (virtio_gpu_resource_create_2d_t *)(g_cmd_buf + CMD_REQ_OFF);
    gpu_memset(c, 0, sizeof(*c));
    c->hdr.type    = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    c->resource_id = VIRTIO_GPU_CURSOR_RESID;
    c->format      = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    c->width       = VIRTIO_GPU_CURSOR_W;
    c->height      = VIRTIO_GPU_CURSOR_H;
    if (gpu_cmd(sizeof(*c), sizeof(virtio_gpu_ctrl_hdr_t)) != 0) return -2;
    if (gpu_resp_type() != VIRTIO_GPU_RESP_OK_NODATA) return -3;

    /* ATTACH_BACKING for the cursor resource */
    virtio_gpu_resource_attach_backing_t *a =
        (virtio_gpu_resource_attach_backing_t *)(g_cmd_buf + CMD_REQ_OFF);
    gpu_memset(a, 0, sizeof(*a));
    a->hdr.type    = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    a->resource_id = VIRTIO_GPU_CURSOR_RESID;
    a->nr_entries  = 1;
    virtio_gpu_mem_entry_t *ent =
        (virtio_gpu_mem_entry_t *)((uint8_t *)a + sizeof(*a));
    ent->addr    = (uint64_t)(uintptr_t)g_cursor_fb;
    ent->length  = cur_bytes;
    ent->padding = 0;
    if (gpu_cmd(sizeof(*a) + sizeof(*ent), sizeof(virtio_gpu_ctrl_hdr_t)) != 0)
        return -4;
    return (gpu_resp_type() == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -5;
}

/* Parse the preferred resolution from the first EDID detailed timing
 * descriptor (base block offset 54, 18 bytes).  Horizontal active =
 * byte[2] | ((byte[4] & 0xF0) << 4); vertical active =
 * byte[5] | ((byte[7] & 0xF0) << 4).  Returns 0 on a plausible mode. */
static int gpu_parse_edid(const uint8_t *e, uint32_t len,
                          uint32_t *out_w, uint32_t *out_h) {
    if (len < 128) return -1;
    /* validate EDID magic: 00 FF FF FF FF FF FF 00 */
    static const uint8_t magic[8] = {0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0};
    for (int i = 0; i < 8; i++)
        if (e[i] != magic[i]) return -2;
    const uint8_t *d = e + 54;                 /* first detailed timing */
    uint32_t w = (uint32_t)d[2] | (((uint32_t)(d[4] & 0xF0)) << 4);
    uint32_t h = (uint32_t)d[5] | (((uint32_t)(d[7] & 0xF0)) << 4);
    if (w == 0 || h == 0 || w > 16384 || h > 16384) return -3;
    *out_w = w; *out_h = h;
    return 0;
}

/* Issue GET_EDID for scanout 0 and parse the preferred mode.
 * Only meaningful when VIRTIO_GPU_F_EDID was negotiated. */
static int gpu_setup_edid(void) {
    virtio_gpu_get_edid_t *req =
        (virtio_gpu_get_edid_t *)(g_cmd_buf + CMD_REQ_OFF);
    gpu_memset(req, 0, sizeof(*req));
    req->hdr.type = VIRTIO_GPU_CMD_GET_EDID;
    req->scanout  = 0;
    if (gpu_cmd(sizeof(*req), sizeof(virtio_gpu_resp_edid_t)) != 0) return -1;

    virtio_gpu_resp_edid_t *resp =
        (virtio_gpu_resp_edid_t *)(g_cmd_buf + CMD_RESP_OFF);
    if (resp->hdr.type != VIRTIO_GPU_RESP_OK_EDID) return -2;
    uint32_t sz = resp->size;
    if (sz > sizeof(resp->edid)) sz = sizeof(resp->edid);
    return gpu_parse_edid(resp->edid, sz, &g_edid_pref_w, &g_edid_pref_h);
}

/* ---- per-frame present ---- */

int virtio_gpu_flush_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (g_count == 0 || !g_fb) return -1;
    /* clip to scanout */
    if (x >= g_width || y >= g_height) return 0;
    if (x + w > g_width)  w = g_width - x;
    if (y + h > g_height) h = g_height - y;
    if (w == 0 || h == 0) return 0;

    /* TRANSFER_TO_HOST_2D: copy backing rect into host resource */
    virtio_gpu_transfer_to_host_2d_t *t =
        (virtio_gpu_transfer_to_host_2d_t *)(g_cmd_buf + CMD_REQ_OFF);
    gpu_memset(t, 0, sizeof(*t));
    t->hdr.type    = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    t->r.x = x; t->r.y = y; t->r.width = w; t->r.height = h;
    t->offset      = (uint64_t)(y * g_width + x) * 4u;
    t->resource_id = VIRTIO_GPU_XFORM_RESID;
    if (gpu_cmd(sizeof(*t), sizeof(virtio_gpu_ctrl_hdr_t)) != 0) return -2;
    if (gpu_resp_type() != VIRTIO_GPU_RESP_OK_NODATA) return -3;

    /* RESOURCE_FLUSH: present the rect on screen */
    virtio_gpu_resource_flush_t *f =
        (virtio_gpu_resource_flush_t *)(g_cmd_buf + CMD_REQ_OFF);
    gpu_memset(f, 0, sizeof(*f));
    f->hdr.type    = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    f->r.x = x; f->r.y = y; f->r.width = w; f->r.height = h;
    f->resource_id = VIRTIO_GPU_XFORM_RESID;
    if (gpu_cmd(sizeof(*f), sizeof(virtio_gpu_ctrl_hdr_t)) != 0) return -4;
    return (gpu_resp_type() == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -5;
}

/* ---- hardware cursor (cursor queue) ---- */

/* Submit one cursor-queue command: req_len bytes at CUR_REQ_OFF sent
 * device-readable, resp_len bytes captured at CUR_RESP_OFF device-writable.
 * The cursor queue reuses the same two-descriptor / polled-used-ring scheme
 * as gpu_cmd but on g_cursorq / g_cur_cmd_buf.  resp_len may be 0, in which
 * case a minimal ctrl_hdr response slot is still provided (device always
 * writes a response for cursor commands). */
static int cursor_cmd(uint32_t req_len) {
    virtqueue_t *vq = &g_cursorq;
    uint16_t d0 = vq_alloc_desc(vq);
    uint16_t d1 = vq_alloc_desc(vq);
    if (d0 == 0xFFFF || d1 == 0xFFFF) return -1;

    uint64_t base_phys = (uint64_t)(uintptr_t)g_cur_cmd_buf;
    vq->desc[d0].addr  = base_phys + CUR_REQ_OFF;
    vq->desc[d0].len   = req_len;
    vq->desc[d0].flags = VIRTQ_DESC_F_NEXT;
    vq->desc[d0].next  = d1;
    vq->desc[d1].addr  = base_phys + CUR_RESP_OFF;
    vq->desc[d1].len   = sizeof(virtio_gpu_ctrl_hdr_t);
    vq->desc[d1].flags = VIRTQ_DESC_F_WRITE;
    vq->desc[d1].next  = 0;

    uint16_t ai = vq->avail->idx % vq->queue_size;
    vq->avail->ring[ai] = d0;
    __asm__ volatile("" ::: "memory");
    vq->avail->idx++;
    __asm__ volatile("" ::: "memory");
    virtio_modern_notify(&g_dev, GPU_CURSORQ, g_cursorq_notify_off);

    uint64_t spins = 0;
    while (vq->last_used == vq->used->idx) {
        if (++spins > 200000000ULL) {
            vq_free_desc(vq, d1);
            vq_free_desc(vq, d0);
            return -2;
        }
        __asm__ volatile("pause" ::: "memory");
    }
    vq->last_used++;
    vq_free_desc(vq, d1);
    vq_free_desc(vq, d0);
    return 0;
}

uint32_t virtio_gpu_cursor_available(void) { return g_cursor_ready; }

int virtio_gpu_set_cursor(const uint32_t *src, uint32_t src_w, uint32_t src_h,
                          uint32_t hot_x, uint32_t hot_y) {
    if (!g_cursor_ready || !g_cursor_fb) return -1;

    /* fill the 64x64 sprite backing: copy the provided image top-left,
     * transparent (0) elsewhere; clamp source to 64x64. */
    uint32_t cw = VIRTIO_GPU_CURSOR_W, ch = VIRTIO_GPU_CURSOR_H;
    uint32_t *dst = (uint32_t *)g_cursor_fb;
    for (uint32_t row = 0; row < ch; row++) {
        for (uint32_t col = 0; col < cw; col++) {
            uint32_t px = 0;
            if (src && row < src_h && col < src_w)
                px = src[row * src_w + col];
            dst[row * cw + col] = px;
        }
    }

    /* push sprite into the host cursor resource via the control queue:
     * TRANSFER_TO_HOST_2D over the full 64x64 rect. */
    virtio_gpu_transfer_to_host_2d_t *t =
        (virtio_gpu_transfer_to_host_2d_t *)(g_cmd_buf + CMD_REQ_OFF);
    gpu_memset(t, 0, sizeof(*t));
    t->hdr.type    = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    t->r.x = 0; t->r.y = 0; t->r.width = cw; t->r.height = ch;
    t->offset      = 0;
    t->resource_id = VIRTIO_GPU_CURSOR_RESID;
    if (gpu_cmd(sizeof(*t), sizeof(virtio_gpu_ctrl_hdr_t)) != 0) return -2;
    if (gpu_resp_type() != VIRTIO_GPU_RESP_OK_NODATA) return -3;

    /* UPDATE_CURSOR on the cursor queue: bind resource + hotspot + show. */
    virtio_gpu_update_cursor_t *u =
        (virtio_gpu_update_cursor_t *)(g_cur_cmd_buf + CUR_REQ_OFF);
    gpu_memset(u, 0, sizeof(*u));
    u->hdr.type       = VIRTIO_GPU_CMD_UPDATE_CURSOR;
    u->pos.scanout_id = 0;
    u->pos.x = 0; u->pos.y = 0;
    u->resource_id    = VIRTIO_GPU_CURSOR_RESID;
    u->hot_x = hot_x; u->hot_y = hot_y;
    return (cursor_cmd(sizeof(*u)) == 0) ? 0 : -4;
}

int virtio_gpu_move_cursor(uint32_t x, uint32_t y) {
    if (!g_cursor_ready) return -1;
    virtio_gpu_update_cursor_t *u =
        (virtio_gpu_update_cursor_t *)(g_cur_cmd_buf + CUR_REQ_OFF);
    gpu_memset(u, 0, sizeof(*u));
    u->hdr.type       = VIRTIO_GPU_CMD_MOVE_CURSOR;
    u->pos.scanout_id = 0;
    u->pos.x = x; u->pos.y = y;
    u->resource_id    = VIRTIO_GPU_CURSOR_RESID;
    return (cursor_cmd(sizeof(*u)) == 0) ? 0 : -2;
}

int virtio_gpu_hide_cursor(void) {
    if (!g_cursor_ready) return -1;
    virtio_gpu_update_cursor_t *u =
        (virtio_gpu_update_cursor_t *)(g_cur_cmd_buf + CUR_REQ_OFF);
    gpu_memset(u, 0, sizeof(*u));
    u->hdr.type       = VIRTIO_GPU_CMD_UPDATE_CURSOR;
    u->pos.scanout_id = 0;
    u->resource_id    = 0;   /* 0 = hide */
    return (cursor_cmd(sizeof(*u)) == 0) ? 0 : -2;
}

/* ---- accessors ---- */
uint32_t virtio_gpu_device_count(void) { return g_count; }

uint32_t virtio_gpu_scanout_count(void) { return g_scanout_count; }

int virtio_gpu_scanout_mode(uint32_t idx, uint32_t *w, uint32_t *h) {
    if (idx >= g_scanout_count) return -1;
    if (w) *w = g_scanout_w[idx];
    if (h) *h = g_scanout_h[idx];
    return 0;
}

uint32_t virtio_gpu_edid_available(void) { return g_edid_ok; }

int virtio_gpu_edid_preferred_mode(uint32_t *w, uint32_t *h) {
    if (!g_edid_ok) return -1;
    if (w) *w = g_edid_pref_w;
    if (h) *h = g_edid_pref_h;
    return 0;
}
uint32_t virtio_gpu_width(void)  { return g_width; }
uint32_t virtio_gpu_height(void) { return g_height; }
void    *virtio_gpu_framebuffer(void) { return g_fb; }

/* ---- bring-up ---- */

void virtio_gpu_init(void) {
    g_count = 0;
    const pci_device_t *found = pci_find_by_id(VIRTIO_VENDOR_ID, VIRTIO_GPU_DEVICE_ID);
    if (!found) {
        serial_write("[virtio-gpu] no device (1af4:1050)\n");
        return;
    }
    pci_device_t pci = *found;
    serial_write("[virtio-gpu] device found, attaching modern transport\n");

    if (virtio_modern_attach(&pci, &g_dev) != 0) {
        serial_write("[virtio-gpu] transport attach failed\n");
        return;
    }

    virtio_modern_reset(&g_dev);
    uint64_t host = virtio_modern_get_features(&g_dev);
    /* require VERSION_1; opportunistically negotiate EDID if the host offers it */
    uint64_t want = host & (1ULL << VIRTIO_F_VERSION_1);
    uint32_t edid_negotiated = 0;
    if (host & (1ULL << VIRTIO_GPU_F_EDID)) {
        want |= (1ULL << VIRTIO_GPU_F_EDID);
        edid_negotiated = 1;
    }
    if (virtio_modern_set_features(&g_dev, want) != 0) {
        serial_write("[virtio-gpu] feature negotiation failed\n");
        virtio_modern_fail(&g_dev);
        return;
    }

    if (virtio_modern_setup_queue(&g_dev, GPU_CTRLQ, &g_ctrlq,
                                  &g_ctrlq_notify_off) != 0) {
        serial_write("[virtio-gpu] controlq setup failed\n");
        virtio_modern_fail(&g_dev);
        return;
    }
    /* cursor queue (index 1) is optional: failure just disables HW cursor */
    uint32_t cursorq_ok =
        (virtio_modern_setup_queue(&g_dev, GPU_CURSORQ, &g_cursorq,
                                   &g_cursorq_notify_off) == 0);
    if (!cursorq_ok)
        serial_write("[virtio-gpu] cursorq setup failed (HW cursor disabled)\n");
    virtio_modern_set_driver_ok(&g_dev);

    /* shared command bounce page */
    x86_64_phys_addr_t cmd_phys = arch_x86_64_pmm_alloc_pages(1);
    if (cmd_phys == 0) { virtio_modern_fail(&g_dev); return; }
    g_cmd_buf = (uint8_t *)(uintptr_t)cmd_phys;
    gpu_memset(g_cmd_buf, 0, 4096);

    if (gpu_get_display_info() != 0) {
        serial_write("[virtio-gpu] get_display_info failed\n");
        virtio_modern_fail(&g_dev);
        return;
    }
    /* clamp to a sane default if firmware reported nothing */
    if (g_width == 0 || g_height == 0) { g_width = 1280; g_height = 800; }

    /* allocate the linear BGRA backing store */
    uint64_t fb_bytes = (uint64_t)g_width * g_height * 4u;
    g_fb_pages = (uint32_t)((fb_bytes + 4095u) / 4096u);
    x86_64_phys_addr_t fb_phys = arch_x86_64_pmm_alloc_pages(g_fb_pages);
    if (fb_phys == 0) {
        serial_write("[virtio-gpu] framebuffer alloc failed\n");
        virtio_modern_fail(&g_dev);
        return;
    }
    g_fb = (uint8_t *)(uintptr_t)fb_phys;
    gpu_memset(g_fb, 0, g_fb_pages * 4096u);

    if (gpu_create_2d() != 0 || gpu_attach_backing() != 0 ||
        gpu_set_scanout() != 0) {
        serial_write("[virtio-gpu] 2D pipeline setup failed\n");
        virtio_modern_fail(&g_dev);
        return;
    }

    g_count = 1;
    serial_write("[virtio-gpu] 2D pipeline ready\n");
    {
        /* report how many heads we lit up (mirror mode) */
        char nb[2] = { (char)('0' + (g_scanout_count > 9 ? 9 : g_scanout_count)), 0 };
        serial_write("[virtio-gpu] scanouts enabled=");
        serial_write(nb);
        serial_write("\n");
    }

    /* query display EDID if the feature was negotiated */
    g_edid_ok = 0;
    if (edid_negotiated) {
        if (gpu_setup_edid() == 0) {
            g_edid_ok = 1;
            serial_write("[virtio-gpu] EDID preferred mode parsed\n");
        } else {
            serial_write("[virtio-gpu] EDID query/parse failed\n");
        }
    }

    /* bring up the hardware cursor if the cursor queue is available */
    g_cursor_ready = 0;
    if (cursorq_ok) {
        x86_64_phys_addr_t cur_cmd_phys = arch_x86_64_pmm_alloc_pages(1);
        if (cur_cmd_phys != 0) {
            g_cur_cmd_buf = (uint8_t *)(uintptr_t)cur_cmd_phys;
            gpu_memset(g_cur_cmd_buf, 0, 4096);
            if (gpu_setup_cursor() == 0) {
                g_cursor_ready = 1;
                serial_write("[virtio-gpu] hardware cursor ready\n");
            } else {
                serial_write("[virtio-gpu] cursor resource setup failed\n");
            }
        }
    }
}
