/* ============================================================
 * openos - virtio-blk PCI discovery block driver scaffold
 * ============================================================ */
#include "../include/virtio_blk.h"
#include "../include/blockdev.h"
#include "../include/pci.h"
#include "../include/serial.h"
#include "../include/string.h"
#include "../fs/vfs.h"

#define VIRTIO_BLK_SECTOR_SIZE 512u
#define VIRTIO_BLK_MAX_DEVICES 8u
#define VIRTIO_BLK_MAJOR 5u

#define VIRTIO_PCI_VENDOR_ID        0x1AF4u
#define VIRTIO_LEGACY_BLK_DEVICE   0x1001u
#define VIRTIO_MODERN_DEVICE_MIN   0x1040u
#define VIRTIO_MODERN_DEVICE_MAX   0x107Fu
#define VIRTIO_MODERN_BLK_ID       2u

typedef enum virtio_blk_kind {
    VIRTIO_BLK_KIND_LEGACY = 0,
    VIRTIO_BLK_KIND_MODERN = 1
} virtio_blk_kind_t;

typedef struct virtio_blk_device {
    uint8_t present;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint16_t device_id;
    virtio_blk_kind_t kind;
    uint32_t io_base;
    uint32_t mmio_base;
    uint32_t sectors;
    char name[16];
    blockdev_t *blockdev;
} virtio_blk_device_t;

static virtio_blk_device_t virtio_blk_devices[VIRTIO_BLK_MAX_DEVICES];
static blockdev_ops_t virtio_blk_ops;
static uint32_t virtio_blk_count;

static int virtio_blk_read_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, void *buf) {
    (void)dev;
    (void)lba;
    (void)count;
    (void)buf;
    return -1;
}

static int virtio_blk_write_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, const void *buf) {
    (void)dev;
    (void)lba;
    (void)count;
    (void)buf;
    return -1;
}

static uint32_t virtio_blk_bar_base(uint8_t bus, uint8_t dev, uint8_t func, uint8_t bar_index, uint8_t want_io) {
    uint32_t bar = pci_read32(bus, dev, func, (uint8_t)(PCI_OFFSET_BAR0 + (bar_index * 4u)));
    if (bar == 0u || bar == 0xFFFFFFFFu) {
        return 0u;
    }

    if ((bar & 0x1u) != 0u) {
        return want_io ? (bar & ~0x3u) : 0u;
    }

    return want_io ? 0u : (bar & ~0xFu);
}

static uint8_t virtio_blk_is_supported(uint16_t vendor, uint16_t device, virtio_blk_kind_t *kind) {
    uint16_t modern_id;

    if (vendor != VIRTIO_PCI_VENDOR_ID) {
        return 0;
    }

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
    out[0] = 'v';
    out[1] = 'b';
    out[2] = 'l';
    out[3] = 'k';
    out[4] = (char)('0' + (char)(index % 10u));
    out[5] = '\0';
}

static void virtio_blk_make_dev_path(char *out, const char *name) {
    out[0] = '/';
    out[1] = 'd';
    out[2] = 'e';
    out[3] = 'v';
    out[4] = '/';
    strcpy(out + 5, name);
}

static void virtio_blk_probe_function(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t vendor;
    uint16_t device;
    virtio_blk_kind_t kind = VIRTIO_BLK_KIND_LEGACY;
    virtio_blk_device_t *vdev;
    char path[32];

    if (virtio_blk_count >= VIRTIO_BLK_MAX_DEVICES) {
        return;
    }

    vendor = pci_read16(bus, dev, func, PCI_OFFSET_VENDOR);
    if (vendor == PCI_VENDOR_INVALID) {
        return;
    }

    device = pci_read16(bus, dev, func, PCI_OFFSET_DEVICE);
    if (!virtio_blk_is_supported(vendor, device, &kind)) {
        return;
    }

    vdev = &virtio_blk_devices[virtio_blk_count];
    memset(vdev, 0, sizeof(*vdev));
    vdev->present = 1;
    vdev->bus = bus;
    vdev->dev = dev;
    vdev->func = func;
    vdev->device_id = device;
    vdev->kind = kind;
    vdev->io_base = virtio_blk_bar_base(bus, dev, func, 0, 1);
    vdev->mmio_base = virtio_blk_bar_base(bus, dev, func, 0, 0);
    if (vdev->mmio_base == 0u) {
        vdev->mmio_base = virtio_blk_bar_base(bus, dev, func, 1, 0);
    }
    vdev->sectors = 1u;
    virtio_blk_make_name(vdev->name, virtio_blk_count);

    if (blockdev_register(vdev->name, VIRTIO_BLK_MAJOR, virtio_blk_count,
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
    serial_write(kind == VIRTIO_BLK_KIND_LEGACY ? " legacy pci=" : " modern pci=");
    serial_write_hex(bus);
    serial_write(":");
    serial_write_hex(dev);
    serial_write(":");
    serial_write_hex(func);
    serial_write(" dev=");
    serial_write_hex(device);
    serial_write("\n");

    virtio_blk_count++;
}

void virtio_blk_init(void) {
    uint16_t bus;
    uint16_t dev;
    uint16_t func;

    virtio_blk_count = 0;
    memset(virtio_blk_devices, 0, sizeof(virtio_blk_devices));
    memset(&virtio_blk_ops, 0, sizeof(virtio_blk_ops));
    virtio_blk_ops.read_blocks = virtio_blk_read_blocks;
    virtio_blk_ops.write_blocks = virtio_blk_write_blocks;

    for (bus = 0; bus < 256u; bus++) {
        for (dev = 0; dev < 32u; dev++) {
            for (func = 0; func < 8u; func++) {
                uint16_t vendor = pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_VENDOR);
                uint8_t header_type;
                if (vendor == PCI_VENDOR_INVALID) {
                    if (func == 0u) {
                        break;
                    }
                    continue;
                }

                virtio_blk_probe_function((uint8_t)bus, (uint8_t)dev, (uint8_t)func);

                header_type = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_HEADER);
                if ((header_type & 0x80u) == 0u) {
                    break;
                }
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
