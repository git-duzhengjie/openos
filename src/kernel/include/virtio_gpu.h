#ifndef OPENOS_KERNEL_VIRTIO_GPU_H
#define OPENOS_KERNEL_VIRTIO_GPU_H

/* =====================================================================
 * virtio-gpu 2D command protocol (virtio 1.1 spec §5.7)
 *
 * PCI id 1af4:1050 (modern-only).  This header defines the control-queue
 * command / response structures used by the 2D pipeline:
 *   GET_DISPLAY_INFO -> CREATE_2D -> ATTACH_BACKING -> SET_SCANOUT
 *   -> {TRANSFER_TO_HOST_2D -> RESOURCE_FLUSH} per frame.
 * ===================================================================== */

#include "types.h"

/* control queue command / response types (subset for 2D) */
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO        0x0100u
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D      0x0101u
#define VIRTIO_GPU_CMD_RESOURCE_UNREF          0x0102u
#define VIRTIO_GPU_CMD_SET_SCANOUT             0x0103u
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH          0x0104u
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D     0x0105u
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106u
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING 0x0107u

/* cursor queue command types (spec §5.7) */
#define VIRTIO_GPU_CMD_UPDATE_CURSOR           0x0300u
#define VIRTIO_GPU_CMD_MOVE_CURSOR             0x0301u

/* response types */
#define VIRTIO_GPU_RESP_OK_NODATA              0x1100u
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO        0x1101u
#define VIRTIO_GPU_RESP_ERR_UNSPEC             0x1200u

/* pixel format: 32bpp BGRA, matches our linear framebuffer layout */
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM       1u

#define VIRTIO_GPU_MAX_SCANOUTS                16u

/* common command header (prepended to every request AND every response) */
typedef struct virtio_gpu_ctrl_hdr {
    uint32_t type;         /* VIRTIO_GPU_CMD_* / VIRTIO_GPU_RESP_* */
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_ctrl_hdr_t;

typedef struct virtio_gpu_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) virtio_gpu_rect_t;

/* GET_DISPLAY_INFO response */
typedef struct virtio_gpu_display_one {
    virtio_gpu_rect_t r;
    uint32_t enabled;
    uint32_t flags;
} __attribute__((packed)) virtio_gpu_display_one_t;

typedef struct virtio_gpu_resp_display_info {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_display_one_t pmodes[VIRTIO_GPU_MAX_SCANOUTS];
} __attribute__((packed)) virtio_gpu_resp_display_info_t;

/* RESOURCE_CREATE_2D request */
typedef struct virtio_gpu_resource_create_2d {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) virtio_gpu_resource_create_2d_t;

/* RESOURCE_ATTACH_BACKING request + one mem entry */
typedef struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_mem_entry_t;

typedef struct virtio_gpu_resource_attach_backing {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
} __attribute__((packed)) virtio_gpu_resource_attach_backing_t;

/* SET_SCANOUT request */
typedef struct virtio_gpu_set_scanout {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed)) virtio_gpu_set_scanout_t;

/* TRANSFER_TO_HOST_2D request */
typedef struct virtio_gpu_transfer_to_host_2d {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_transfer_to_host_2d_t;

/* RESOURCE_FLUSH request */
typedef struct virtio_gpu_resource_flush {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_resource_flush_t;

/* cursor position descriptor (shared by UPDATE_CURSOR / MOVE_CURSOR) */
typedef struct virtio_gpu_cursor_pos {
    uint32_t scanout_id;
    uint32_t x;
    uint32_t y;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_cursor_pos_t;

/* UPDATE_CURSOR / MOVE_CURSOR request (cursor queue).
 * UPDATE_CURSOR sets image (resource_id) + hotspot + position;
 * MOVE_CURSOR reuses the same struct but only pos is honoured. */
typedef struct virtio_gpu_update_cursor {
    virtio_gpu_ctrl_hdr_t   hdr;
    virtio_gpu_cursor_pos_t pos;
    uint32_t                resource_id;
    uint32_t                hot_x;
    uint32_t                hot_y;
    uint32_t                padding;
} __attribute__((packed)) virtio_gpu_update_cursor_t;

/* hardware cursor image is a fixed 64x64 BGRA sprite (spec requirement) */
#define VIRTIO_GPU_CURSOR_W  64u
#define VIRTIO_GPU_CURSOR_H  64u

/* ---- public driver API ---- */

/* Probe PCI for virtio-gpu, bring up the 2D pipeline and register a
 * linear framebuffer backed by host scanout.  Safe to call when no
 * device is present (leaves device_count == 0). */
void virtio_gpu_init(void);

/* Number of virtio-gpu devices successfully initialised (0 or 1). */
uint32_t virtio_gpu_device_count(void);

/* Active scanout geometry (valid only when device_count > 0). */
uint32_t virtio_gpu_width(void);
uint32_t virtio_gpu_height(void);

/* Framebuffer backing store base (kernel virtual == phys, identity). */
void *virtio_gpu_framebuffer(void);

/* Push a dirty rectangle from the backing store to the host display
 * (TRANSFER_TO_HOST_2D + RESOURCE_FLUSH).  Coordinates are clipped to
 * the scanout.  Returns 0 on success. */
int virtio_gpu_flush_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* ---- hardware cursor (cursor queue) ---- */

/* Non-zero once the cursor queue + 64x64 cursor resource are live. */
uint32_t virtio_gpu_cursor_available(void);

/* Upload a cursor sprite and show it.  src is BGRA, src_w x src_h pixels
 * (clamped/padded to 64x64); hot_x/hot_y is the pointer hotspot.
 * Returns 0 on success. */
int virtio_gpu_set_cursor(const uint32_t *src, uint32_t src_w, uint32_t src_h,
                          uint32_t hot_x, uint32_t hot_y);

/* Move the hardware cursor to absolute scanout coordinate (x,y).
 * Cheap: single MOVE_CURSOR on the cursor queue.  Returns 0 on success. */
int virtio_gpu_move_cursor(uint32_t x, uint32_t y);

/* Hide the hardware cursor (UPDATE_CURSOR with resource_id 0). */
int virtio_gpu_hide_cursor(void);

#endif /* OPENOS_KERNEL_VIRTIO_GPU_H */
