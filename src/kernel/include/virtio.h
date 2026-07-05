#ifndef OPENOS_KERNEL_VIRTIO_H
#define OPENOS_KERNEL_VIRTIO_H

#include "types.h"
#include "pci.h"

#define VIRTIO_PCI_VENDOR_ID        0x1AF4u

#define VIRTIO_LEGACY_NET_DEVICE   0x1000u
#define VIRTIO_LEGACY_BLK_DEVICE   0x1001u
#define VIRTIO_LEGACY_GPU_DEVICE   0x1050u
#define VIRTIO_LEGACY_INPUT_DEVICE 0x1052u

#define VIRTIO_MODERN_DEVICE_MIN   0x1040u
#define VIRTIO_MODERN_DEVICE_MAX   0x107Fu
#define VIRTIO_MODERN_NET_ID       0x0001u
#define VIRTIO_MODERN_BLK_ID       0x0002u
#define VIRTIO_MODERN_GPU_ID       0x0010u
#define VIRTIO_MODERN_INPUT_ID     0x0012u

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
#define VIRTQ_DESC_F_INDIRECT       4u

#define VIRTQ_AVAIL_F_NO_INTERRUPT  1u
#define VIRTQ_USED_F_NO_NOTIFY      1u

/* legacy virtqueue 对齐要求：avail/used 之间按 4096 页对齐 */
#define VIRTIO_PCI_VRING_ALIGN      4096u

/* ---- Split Virtqueue 结构（legacy layout，全部小端） ---- */

/* 描述符：指向一段 guest 物理内存 */
typedef struct virtq_desc {
    uint64_t addr;   /* guest 物理地址 */
    uint32_t len;    /* 长度 */
    uint16_t flags;  /* VIRTQ_DESC_F_* */
    uint16_t next;   /* 链式下一个描述符索引 */
} __attribute__((packed)) virtq_desc_t;

/* 可用环：driver 填入待处理描述符索引 */
typedef struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[]; /* [queue_size] */
} __attribute__((packed)) virtq_avail_t;

/* 已用环元素 */
typedef struct virtq_used_elem {
    uint32_t id;     /* 起始描述符索引 */
    uint32_t len;    /* 设备写入的字节数 */
} __attribute__((packed)) virtq_used_elem_t;

/* 已用环：device 填入处理完成的描述符 */
typedef struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[]; /* [queue_size] */
} __attribute__((packed)) virtq_used_t;

/* 运行时管理的 split virtqueue（指针指向物理连续、页对齐的内存） */
typedef struct virtqueue {
    uint16_t         queue_size;   /* 描述符数量（2 的幂） */
    uint16_t         queue_index;  /* 队列号 */
    uint16_t         free_head;    /* 空闲描述符链表头 */
    uint16_t         num_free;     /* 空闲描述符数量 */
    uint16_t         last_used;    /* 上次消费到的 used->idx */
    volatile virtq_desc_t  *desc;  /* 描述符表 */
    volatile virtq_avail_t *avail; /* 可用环 */
    volatile virtq_used_t  *used;  /* 已用环 */
    uint64_t         ring_phys;    /* 整块 ring 的物理基址（用于 PFN） */
    uint32_t         ring_bytes;   /* 整块 ring 占用字节数 */
} virtqueue_t;

typedef enum virtio_transport_kind {
    VIRTIO_TRANSPORT_LEGACY_PCI = 0,
    VIRTIO_TRANSPORT_MODERN_PCI = 1
} virtio_transport_kind_t;

typedef struct virtio_pci_device {
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    virtio_transport_kind_t kind;
    uint32_t io_base;
    uint32_t mmio_base;
} virtio_pci_device_t;

void virtio_mb(void);
uint32_t virtio_ptr32(const void *ptr);
uint64_t virtio_io_read64(uint16_t port);
uint32_t virtio_pci_bar_base(uint8_t bus, uint8_t dev, uint8_t func,
                             uint8_t bar_index, uint8_t want_io);
int virtio_pci_is_modern_device(uint16_t vendor, uint16_t device,
                                uint16_t modern_device_id);
void virtio_pci_reset_legacy(uint32_t io_base);
void virtio_pci_set_status_legacy(uint32_t io_base, uint8_t status);
uint8_t virtio_pci_get_status_legacy(uint32_t io_base);
uint32_t virtio_pci_read_features_legacy(uint32_t io_base);
void virtio_pci_write_features_legacy(uint32_t io_base, uint32_t features);
uint32_t virtio_pci_config_read32_legacy(uint32_t io_base, uint16_t offset);
uint64_t virtio_pci_config_read64_legacy(uint32_t io_base, uint16_t offset);
int virtio_pci_setup_queue_legacy(uint32_t io_base, uint16_t queue_index,
                                  void *queue, uint16_t min_queue_size,
                                  uint16_t *host_queue_size);
void virtio_pci_notify_queue_legacy(uint32_t io_base, uint16_t queue_index);
void virtio_pci_fail_legacy(uint32_t io_base);
int virtio_pci_probe_device(uint16_t legacy_device_id, uint16_t modern_device_id,
                            virtio_pci_device_t *out, uint32_t max_devices,
                            int (*legacy_extra_match)(uint8_t class_code, uint8_t subclass));

#endif /* OPENOS_KERNEL_VIRTIO_H */
