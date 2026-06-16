/* ============================================================
 * openos - virtio-blk legacy PCI block driver
 * ============================================================ */
#include "../include/virtio_blk.h"
#include "../include/blockdev.h"
#include "../include/io.h"
#include "../include/pci.h"
#include "../include/serial.h"
#include "../include/string.h"
#include "../fs/vfs.h"

#define VIRTIO_BLK_SECTOR_SIZE 512u
#define VIRTIO_BLK_MAX_DEVICES 8u
#define VIRTIO_BLK_MAJOR 5u
#define VIRTIO_BLK_QUEUE_SIZE 8u
#define VIRTIO_BLK_WAIT_LIMIT 1000000u

#define VIRTIO_PCI_VENDOR_ID        0x1AF4u
#define VIRTIO_LEGACY_BLK_DEVICE   0x1001u
#define VIRTIO_MODERN_DEVICE_MIN   0x1040u
#define VIRTIO_MODERN_DEVICE_MAX   0x107Fu
#define VIRTIO_MODERN_BLK_ID       2u

#define VIRTIO_PCI_HOST_FEATURES    0x00u
#define VIRTIO_PCI_GUEST_FEATURES   0x04u
#define VIRTIO_PCI_QUEUE_PFN        0x08u
#define VIRTIO_PCI_QUEUE_NUM        0x0Cu
#define VIRTIO_PCI_QUEUE_SEL        0x0Eu
#define VIRTIO_PCI_QUEUE_NOTIFY     0x10u
#define VIRTIO_PCI_STATUS           0x12u
#define VIRTIO_PCI_ISR              0x13u
#define VIRTIO_PCI_CONFIG           0x14u

#define VIRTIO_STATUS_ACKNOWLEDGE   0x01u
#define VIRTIO_STATUS_DRIVER        0x02u
#define VIRTIO_STATUS_DRIVER_OK     0x04u
#define VIRTIO_STATUS_FEATURES_OK   0x08u
#define VIRTIO_STATUS_FAILED        0x80u

#define VIRTQ_DESC_F_NEXT           1u
#define VIRTQ_DESC_F_WRITE          2u

#define VIRTIO_BLK_F_RO             5u
#define VIRTIO_BLK_T_IN             0u
#define VIRTIO_BLK_T_OUT            1u

typedef enum virtio_blk_kind {
    VIRTIO_BLK_KIND_LEGACY = 0,
    VIRTIO_BLK_KIND_MODERN = 1
} virtio_blk_kind_t;

typedef struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) virtq_desc_t;

typedef struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_BLK_QUEUE_SIZE];
} __attribute__((packed)) virtq_avail_t;

typedef struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) virtq_used_elem_t;

typedef struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[VIRTIO_BLK_QUEUE_SIZE];
} __attribute__((packed)) virtq_used_t;

typedef struct virtio_blk_req_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed)) virtio_blk_req_header_t;

typedef struct virtio_blk_queue {
    virtq_desc_t desc[VIRTIO_BLK_QUEUE_SIZE] __attribute__((aligned(16)));
    virtq_avail_t avail __attribute__((aligned(2)));
    uint8_t pad[4096];
    virtq_used_t used __attribute__((aligned(4)));
} __attribute__((aligned(4096))) virtio_blk_queue_t;

typedef struct virtio_blk_device {
    uint8_t present;
    uint8_t readonly;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint16_t device_id;
    virtio_blk_kind_t kind;
    uint32_t io_base;
    uint32_t mmio_base;
    uint32_t sectors;
    uint16_t last_used_idx;
    char name[16];
    blockdev_t *blockdev;
    virtio_blk_queue_t *queue;
    virtio_blk_req_header_t request_header;
    uint8_t request_status;
} virtio_blk_device_t;

static virtio_blk_device_t virtio_blk_devices[VIRTIO_BLK_MAX_DEVICES];
static blockdev_ops_t virtio_blk_ops;
static uint32_t virtio_blk_count;
static virtio_blk_queue_t virtio_blk_queues[VIRTIO_BLK_MAX_DEVICES] __attribute__((aligned(4096)));

static void virtio_mb(void) {
    __asm__ volatile ("" ::: "memory");
}

static uint32_t virtio_ptr32(const void *ptr) {
    return (uint32_t)(uintptr_t)ptr;
}

static uint64_t virtio_io_read64(uint16_t port) {
    uint32_t lo = inl(port);
    uint32_t hi = inl((uint16_t)(port + 4u));
    return ((uint64_t)hi << 32) | lo;
}

static uint32_t virtio_blk_config_read32(virtio_blk_device_t *vdev, uint16_t offset) {
    return inl((uint16_t)(vdev->io_base + VIRTIO_PCI_CONFIG + offset));
}

static uint64_t virtio_blk_config_read64(virtio_blk_device_t *vdev, uint16_t offset) {
    return virtio_io_read64((uint16_t)(vdev->io_base + VIRTIO_PCI_CONFIG + offset));
}

static int virtio_blk_read_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, void *buf);
static int virtio_blk_write_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, const void *buf);

static uint32_t virtio_blk_bar_base(uint8_t bus, uint8_t dev, uint8_t func, uint8_t bar_index, uint8_t want_io) {
    uint32_t bar = pci_read32(bus, dev, func, (uint8_t)(PCI_OFFSET_BAR0 + (bar_index * 4u)));
    if (bar == 0u || bar == 0xFFFFFFFFu) return 0u;
    if ((bar & 0x1u) != 0u) return want_io ? (bar & ~0x3u) : 0u;
    return want_io ? 0u : (bar & ~0xFu);
}

static uint8_t virtio_blk_is_supported(uint16_t vendor, uint16_t device, virtio_blk_kind_t *kind) {
    uint16_t modern_id;
    if (vendor != VIRTIO_PCI_VENDOR_ID) return 0;
    if (device == VIRTIO_LEGACY_BLK_DEVICE) {
        *kind = VIRTIO_BLK_KIND_LEGACY;
        return 1;
    }
    if (device >= VIRTIO_MODERN_DEVICE_MIN && device <= VIRTIO_MODERN_DEVICE_MAX) {
        modern_id = (uint16_t)(device - VIRTIO_MODERN_DEVICE_MIN);
        if (modern_id == VIRTIO_MODERN_BLK_ID) {
            *kind = VIRTIO_BLK_KIND_MODERN;
            return 1;
        }
    }
    return 0;
}

static void virtio_blk_make_name(char *out, uint32_t index) {
    out[0] = 'v'; out[1] = 'b'; out[2] = 'l'; out[3] = 'k';
    out[4] = (char)('0' + (char)(index % 10u));
    out[5] = '\0';
}

static void virtio_blk_make_dev_path(char *out, const char *name) {
    out[0] = '/'; out[1] = 'd'; out[2] = 'e'; out[3] = 'v'; out[4] = '/';
    strcpy(out + 5, name);
}

static void virtio_blk_fail_device(virtio_blk_device_t *vdev) {
    if (vdev && vdev->io_base) {
        outb((uint16_t)(vdev->io_base + VIRTIO_PCI_STATUS), VIRTIO_STATUS_FAILED);
    }
}

static int virtio_blk_setup_legacy_queue(virtio_blk_device_t *vdev, uint32_t index) {
    uint16_t queue_num;
    uint32_t queue_pfn;

    vdev->queue = &virtio_blk_queues[index];
    memset(vdev->queue, 0, sizeof(*vdev->queue));

    outw((uint16_t)(vdev->io_base + VIRTIO_PCI_QUEUE_SEL), 0u);
    queue_num = inw((uint16_t)(vdev->io_base + VIRTIO_PCI_QUEUE_NUM));
    if (queue_num == 0u || queue_num < VIRTIO_BLK_QUEUE_SIZE) return -1;

    queue_pfn = virtio_ptr32(vdev->queue) >> 12;
    outl((uint16_t)(vdev->io_base + VIRTIO_PCI_QUEUE_PFN), queue_pfn);
    if (inl((uint16_t)(vdev->io_base + VIRTIO_PCI_QUEUE_PFN)) == 0u) return -1;

    vdev->last_used_idx = 0u;
    return 0;
}

static int virtio_blk_setup_legacy(virtio_blk_device_t *vdev, uint32_t index) {
    uint32_t host_features;
    uint32_t guest_features = 0u;
    uint8_t status;
    uint64_t capacity;

    if (vdev->io_base == 0u) return -1;

    outb((uint16_t)(vdev->io_base + VIRTIO_PCI_STATUS), 0u);
    outb((uint16_t)(vdev->io_base + VIRTIO_PCI_STATUS), VIRTIO_STATUS_ACKNOWLEDGE);
    outb((uint16_t)(vdev->io_base + VIRTIO_PCI_STATUS), VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    host_features = inl((uint16_t)(vdev->io_base + VIRTIO_PCI_HOST_FEATURES));
    if ((host_features & (1u << VIRTIO_BLK_F_RO)) != 0u) vdev->readonly = 1u;
    outl((uint16_t)(vdev->io_base + VIRTIO_PCI_GUEST_FEATURES), guest_features);

    status = (uint8_t)(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
    outb((uint16_t)(vdev->io_base + VIRTIO_PCI_STATUS), status);

    if (virtio_blk_setup_legacy_queue(vdev, index) != 0) {
        virtio_blk_fail_device(vdev);
        return -1;
    }

    capacity = virtio_blk_config_read64(vdev, 0u);
    if (capacity == 0u) {
        capacity = (uint64_t)virtio_blk_config_read32(vdev, 0u);
    }
    vdev->sectors = capacity > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)capacity;
    if (vdev->sectors == 0u) {
        virtio_blk_fail_device(vdev);
        return -1;
    }

    outb((uint16_t)(vdev->io_base + VIRTIO_PCI_STATUS),
         (uint8_t)(status | VIRTIO_STATUS_DRIVER_OK));
    return 0;
}

static int virtio_blk_request(virtio_blk_device_t *vdev, uint32_t type,
                              uint32_t lba, uint32_t count, void *buf) {
    virtio_blk_queue_t *q;
    uint16_t avail_slot;
    uint16_t start_used;
    uint32_t i;

    if (!vdev || !vdev->present || !buf || count == 0u) return -1;
    if (lba >= vdev->sectors || count > (vdev->sectors - lba)) return -1;
    if (type == VIRTIO_BLK_T_OUT && vdev->readonly) return -1;

    q = vdev->queue;
    memset(&vdev->request_header, 0, sizeof(vdev->request_header));
    vdev->request_header.type = type;
    vdev->request_header.sector = (uint64_t)lba;
    vdev->request_status = 0xFFu;

    q->desc[0].addr = virtio_ptr32(&vdev->request_header);
    q->desc[0].len = sizeof(vdev->request_header);
    q->desc[0].flags = VIRTQ_DESC_F_NEXT;
    q->desc[0].next = 1u;

    q->desc[1].addr = virtio_ptr32(buf);
    q->desc[1].len = count * VIRTIO_BLK_SECTOR_SIZE;
    q->desc[1].flags = (uint16_t)(VIRTQ_DESC_F_NEXT | (type == VIRTIO_BLK_T_IN ? VIRTQ_DESC_F_WRITE : 0u));
    q->desc[1].next = 2u;

    q->desc[2].addr = virtio_ptr32(&vdev->request_status);
    q->desc[2].len = 1u;
    q->desc[2].flags = VIRTQ_DESC_F_WRITE;
    q->desc[2].next = 0u;

    avail_slot = (uint16_t)(q->avail.idx % VIRTIO_BLK_QUEUE_SIZE);
    q->avail.ring[avail_slot] = 0u;
    virtio_mb();
    start_used = q->used.idx;
    q->avail.idx++;
    virtio_mb();
    outw((uint16_t)(vdev->io_base + VIRTIO_PCI_QUEUE_NOTIFY), 0u);

    for (i = 0; i < VIRTIO_BLK_WAIT_LIMIT; i++) {
        if (q->used.idx != start_used) {
            vdev->last_used_idx = q->used.idx;
            (void)inb((uint16_t)(vdev->io_base + VIRTIO_PCI_ISR));
            return vdev->request_status == 0u ? 0 : -1;
        }
        io_wait();
    }

    return -1;
}

static int virtio_blk_read_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, void *buf) {
    virtio_blk_device_t *vdev = dev ? (virtio_blk_device_t *)dev->private_data : 0;
    return virtio_blk_request(vdev, VIRTIO_BLK_T_IN, lba, count, buf);
}

static int virtio_blk_write_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, const void *buf) {
    virtio_blk_device_t *vdev = dev ? (virtio_blk_device_t *)dev->private_data : 0;
    return virtio_blk_request(vdev, VIRTIO_BLK_T_OUT, lba, count, (void *)buf);
}

static void virtio_blk_probe_function(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t vendor;
    uint16_t device;
    virtio_blk_kind_t kind = VIRTIO_BLK_KIND_LEGACY;
    virtio_blk_device_t *vdev;
    char path[32];
    uint32_t index;

    if (virtio_blk_count >= VIRTIO_BLK_MAX_DEVICES) return;

    vendor = pci_read16(bus, dev, func, PCI_OFFSET_VENDOR);
    if (vendor == PCI_VENDOR_INVALID) return;

    device = pci_read16(bus, dev, func, PCI_OFFSET_DEVICE);
    if (!virtio_blk_is_supported(vendor, device, &kind)) return;

    if (kind != VIRTIO_BLK_KIND_LEGACY) {
        serial_write("virtio-blk: modern device found but common-cfg transport is not enabled yet\n");
        return;
    }

    index = virtio_blk_count;
    vdev = &virtio_blk_devices[index];
    memset(vdev, 0, sizeof(*vdev));
    vdev->present = 1;
    vdev->bus = bus;
    vdev->dev = dev;
    vdev->func = func;
    vdev->device_id = device;
    vdev->kind = kind;
    vdev->io_base = virtio_blk_bar_base(bus, dev, func, 0, 1);
    vdev->mmio_base = virtio_blk_bar_base(bus, dev, func, 0, 0);
    virtio_blk_make_name(vdev->name, index);

    if (virtio_blk_setup_legacy(vdev, index) != 0) {
        serial_write("virtio-blk: legacy setup failed\n");
        memset(vdev, 0, sizeof(*vdev));
        return;
    }

    if (blockdev_register(vdev->name, VIRTIO_BLK_MAJOR, index,
                          VIRTIO_BLK_SECTOR_SIZE, vdev->sectors,
                          &virtio_blk_ops, vdev) != 0) {
        serial_write("virtio-blk: failed to register blockdev\n");
        memset(vdev, 0, sizeof(*vdev));
        return;
    }

    vdev->blockdev = blockdev_find(vdev->name);
    if (!vdev->blockdev) {
        (void)blockdev_unregister(vdev->name);
        memset(vdev, 0, sizeof(*vdev));
        return;
    }

    virtio_blk_make_dev_path(path, vdev->name);
    (void)vfs_mknod(path, FS_BLOCK_DEVICE | 0660, vdev->name);

    serial_write("virtio-blk: registered ");
    serial_write(vdev->name);
    serial_write(" pci=");
    serial_write_hex(bus);
    serial_write(":");
    serial_write_hex(dev);
    serial_write(":");
    serial_write_hex(func);
    serial_write(" sectors=");
    serial_write_hex(vdev->sectors);
    serial_write(vdev->readonly ? " ro\n" : " rw\n");

    virtio_blk_count++;
}

void virtio_blk_init(void) {
    uint16_t bus;
    uint16_t dev;
    uint16_t func;

    virtio_blk_count = 0;
    memset(virtio_blk_devices, 0, sizeof(virtio_blk_devices));
    memset(virtio_blk_queues, 0, sizeof(virtio_blk_queues));
    memset(&virtio_blk_ops, 0, sizeof(virtio_blk_ops));
    virtio_blk_ops.read_blocks = virtio_blk_read_blocks;
    virtio_blk_ops.write_blocks = virtio_blk_write_blocks;

    for (bus = 0; bus < 256u; bus++) {
        for (dev = 0; dev < 32u; dev++) {
            for (func = 0; func < 8u; func++) {
                uint16_t vendor = pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_VENDOR);
                uint8_t header_type;
                if (vendor == PCI_VENDOR_INVALID) {
                    if (func == 0u) break;
                    continue;
                }

                virtio_blk_probe_function((uint8_t)bus, (uint8_t)dev, (uint8_t)func);

                header_type = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_HEADER);
                if ((header_type & 0x80u) == 0u) break;
            }
        }
    }

    serial_write("virtio-blk: devices=");
    serial_write_hex(virtio_blk_count);
    serial_write("\n");
}

uint32_t virtio_blk_device_count(void) {
    return virtio_blk_count;
}
