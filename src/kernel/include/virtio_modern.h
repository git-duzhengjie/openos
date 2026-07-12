#ifndef OPENOS_KERNEL_VIRTIO_MODERN_H
#define OPENOS_KERNEL_VIRTIO_MODERN_H

/* =====================================================================
 * virtio 1.0 modern PCI transport (MMIO capability based)
 *
 * virtio-gpu (1af4:1050) is a modern-only device: it exposes NO legacy
 * IO-port BAR.  The register windows are located through vendor-specific
 * PCI capabilities (cap id 0x09) each carrying a cfg_type + bar + offset.
 * This header defines the capability layout and the memory-mapped
 * common configuration structure per the virtio 1.0 spec (§4.1).
 * ===================================================================== */

#include "types.h"
#include "pci.h"
#include "virtio.h"

/* PCI vendor-specific capability id used by virtio to locate its windows */
#define VIRTIO_PCI_CAP_VENDOR_ID     0x09u

/* virtio_pci_cap.cfg_type values (spec §4.1.4) */
#define VIRTIO_PCI_CAP_COMMON_CFG    1u  /* common configuration */
#define VIRTIO_PCI_CAP_NOTIFY_CFG    2u  /* notifications */
#define VIRTIO_PCI_CAP_ISR_CFG       3u  /* ISR status */
#define VIRTIO_PCI_CAP_DEVICE_CFG    4u  /* device specific configuration */
#define VIRTIO_PCI_CAP_PCI_CFG       5u  /* PCI configuration access */

/* virtio_pci_cap — layout inside PCI config space (spec §4.1.4) */
typedef struct virtio_pci_cap {
    uint8_t  cap_vndr;    /* generic PCI field: 0x09 */
    uint8_t  cap_next;    /* generic PCI field: next cap ptr */
    uint8_t  cap_len;     /* generic PCI field: capability length */
    uint8_t  cfg_type;    /* VIRTIO_PCI_CAP_* */
    uint8_t  bar;         /* which BAR (0..5) contains the window */
    uint8_t  padding[3];
    uint32_t offset;      /* offset within the BAR */
    uint32_t length;      /* length of the window */
} __attribute__((packed)) virtio_pci_cap_t;

/* virtio_pci_common_cfg — MMIO register block (spec §4.1.4.3, all LE) */
typedef struct virtio_pci_common_cfg {
    /* device (host) feature negotiation */
    volatile uint32_t device_feature_select;  /* 0x00 rw */
    volatile uint32_t device_feature;          /* 0x04 ro */
    volatile uint32_t driver_feature_select;   /* 0x08 rw */
    volatile uint32_t driver_feature;          /* 0x0C rw */
    /* MSI-X */
    volatile uint16_t msix_config;             /* 0x10 rw */
    volatile uint16_t num_queues;              /* 0x12 ro */
    /* device status / config generation */
    volatile uint8_t  device_status;           /* 0x14 rw */
    volatile uint8_t  config_generation;        /* 0x15 ro */
    /* per-queue window (selected by queue_select) */
    volatile uint16_t queue_select;            /* 0x16 rw */
    volatile uint16_t queue_size;              /* 0x18 rw */
    volatile uint16_t queue_msix_vector;       /* 0x1A rw */
    volatile uint16_t queue_enable;            /* 0x1C rw */
    volatile uint16_t queue_notify_off;        /* 0x1E ro */
    volatile uint64_t queue_desc;              /* 0x20 rw (phys) */
    volatile uint64_t queue_driver;            /* 0x28 rw (phys, avail ring) */
    volatile uint64_t queue_device;            /* 0x30 rw (phys, used ring) */
} __attribute__((packed)) virtio_pci_common_cfg_t;

/* virtio 1.0 feature bit: device offers modern (v1) semantics */
#define VIRTIO_F_VERSION_1           32u   /* in the 32..63 feature word */

/* Runtime handle for one modern virtio PCI device.  All window pointers
 * are kernel virtual addresses (BAR MMIO must be mapped beforehand). */
typedef struct virtio_modern_dev {
    pci_device_t                     pci;         /* copy of PCI record */
    volatile virtio_pci_common_cfg_t *common;     /* COMMON_CFG window */
    volatile uint8_t                 *notify_base; /* NOTIFY_CFG window base */
    uint32_t                          notify_off_multiplier;
    volatile uint8_t                 *isr;         /* ISR_CFG window */
    volatile uint8_t                 *device_cfg;  /* DEVICE_CFG window */
    uint32_t                          device_cfg_len;
    int                               ready;       /* transport initialised */
} virtio_modern_dev_t;

/* ---- Modern transport API (implemented in virtio_modern64.c) ---- */

/* Locate + map all virtio capability windows for a probed PCI device.
 * Returns 0 on success and fills *out.  Enables MMIO + bus-master. */
int virtio_modern_attach(const pci_device_t *pci, virtio_modern_dev_t *out);

/* Reset device and drive the ACKNOWLEDGE|DRIVER handshake. */
void virtio_modern_reset(virtio_modern_dev_t *d);

/* Read the 64-bit host feature word. */
uint64_t virtio_modern_get_features(virtio_modern_dev_t *d);

/* Write the negotiated driver features and set FEATURES_OK; returns 0 if
 * the device accepted the feature set, negative otherwise. */
int virtio_modern_set_features(virtio_modern_dev_t *d, uint64_t features);

/* Set DRIVER_OK — device becomes live. */
void virtio_modern_set_driver_ok(virtio_modern_dev_t *d);

/* Mark device FAILED. */
void virtio_modern_fail(virtio_modern_dev_t *d);

/* Set up split virtqueue #index: allocates page-aligned ring memory,
 * programs desc/driver/device phys addresses and enables the queue.
 * Fills *vq.  Returns 0 on success. */
int virtio_modern_setup_queue(virtio_modern_dev_t *d, uint16_t index,
                              virtqueue_t *vq, uint16_t *notify_off_out);

/* Notify the device that queue #index has new available buffers. */
void virtio_modern_notify(virtio_modern_dev_t *d, uint16_t index,
                          uint16_t notify_off);

/* Read/write a byte range of the device-specific config window. */
void virtio_modern_cfg_read(virtio_modern_dev_t *d, uint32_t off,
                            void *buf, uint32_t len);
void virtio_modern_cfg_write(virtio_modern_dev_t *d, uint32_t off,
                             const void *buf, uint32_t len);

#endif /* OPENOS_KERNEL_VIRTIO_MODERN_H */
