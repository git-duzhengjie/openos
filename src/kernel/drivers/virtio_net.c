/* ============================================================
 * openos - virtio-net legacy PCI network driver
 * ============================================================ */
#include "../include/virtio_net.h"
#include "../include/idt.h"
#include "../include/io.h"
#include "../include/pci.h"
#include "../include/serial.h"
#include "../include/string.h"
#include "../net/net.h"

#define VIRTIO_NET_MAX_DEVICES 8u
#define VIRTIO_NET_QUEUE_SIZE 8u
#define VIRTIO_NET_RX_BUFS 4u
#define VIRTIO_NET_FRAME_BUF 2048u
#define VIRTIO_NET_WAIT_LIMIT 1000000u

#define PCI_CLASS_NETWORK      0x02u
#define PCI_SUBCLASS_ETHERNET  0x00u

#define VIRTIO_VENDOR_ID       0x1AF4u
#define VIRTIO_DEVICE_NET_BASE 0x1000u
#define VIRTIO_DEVICE_NET_MIN  0x1040u
#define VIRTIO_DEVICE_NET_MAX  0x107Fu
#define VIRTIO_DEVICE_NET_ID   0x0001u

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

#define VIRTIO_NET_F_MAC            5u

typedef struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) virtq_desc_t;

typedef struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_NET_QUEUE_SIZE];
} __attribute__((packed)) virtq_avail_t;

typedef struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) virtq_used_elem_t;

typedef struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[VIRTIO_NET_QUEUE_SIZE];
} __attribute__((packed)) virtq_used_t;

typedef struct virtio_net_queue {
    virtq_desc_t desc[VIRTIO_NET_QUEUE_SIZE] __attribute__((aligned(16)));
    virtq_avail_t avail __attribute__((aligned(2)));
    uint8_t pad[4096];
    virtq_used_t used __attribute__((aligned(4)));
} __attribute__((aligned(4096))) virtio_net_queue_t;

typedef struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed)) virtio_net_hdr_t;

typedef enum virtio_net_kind {
    VIRTIO_NET_KIND_LEGACY = 0,
    VIRTIO_NET_KIND_MODERN = 1
} virtio_net_kind_t;

typedef struct virtio_net_device {
    uint8_t present;
    uint8_t initialized;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint8_t irq;
    uint16_t vendor_id;
    uint16_t device_id;
    virtio_net_kind_t kind;
    uint32_t io_base;
    uint32_t mmio_base;
    uint16_t rx_last_used;
    uint16_t tx_last_used;
    uint16_t rx_next_buf;
    char name[16];
    net_device_t netdev;
    virtio_net_queue_t *rxq;
    virtio_net_queue_t *txq;
} virtio_net_device_t;

static virtio_net_device_t virtio_net_devices[VIRTIO_NET_MAX_DEVICES];
static uint32_t virtio_net_count;
static virtio_net_queue_t virtio_net_rxq[VIRTIO_NET_MAX_DEVICES] __attribute__((aligned(4096)));
static virtio_net_queue_t virtio_net_txq[VIRTIO_NET_MAX_DEVICES] __attribute__((aligned(4096)));
static uint8_t virtio_net_rx_buffers[VIRTIO_NET_MAX_DEVICES][VIRTIO_NET_RX_BUFS][VIRTIO_NET_FRAME_BUF] __attribute__((aligned(16)));
static uint8_t virtio_net_tx_buffers[VIRTIO_NET_MAX_DEVICES][VIRTIO_NET_FRAME_BUF] __attribute__((aligned(16)));

static void virtio_mb(void) {
    __asm__ volatile ("" ::: "memory");
}

static uint32_t virtio_ptr32(const void *ptr) {
    return (uint32_t)(uintptr_t)ptr;
}

static uint32_t virtio_bar_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, int want_io) {
    uint32_t bar = pci_read32(bus, dev, func, off);
    if (bar == 0u || bar == 0xFFFFFFFFu) return 0u;
    if ((bar & 0x1u) != 0u) return want_io ? (bar & 0xFFFFFFFCu) : 0u;
    return want_io ? 0u : (bar & 0xFFFFFFF0u);
}

static void virtio_net_set_status(virtio_net_device_t *vdev, uint8_t status) {
    outb((uint16_t)(vdev->io_base + VIRTIO_PCI_STATUS), status);
}

static void virtio_net_fail(virtio_net_device_t *vdev) {
    if (vdev && vdev->io_base) virtio_net_set_status(vdev, VIRTIO_STATUS_FAILED);
}

static int virtio_net_is_legacy(uint16_t vendor, uint16_t device,
                                uint8_t class_code, uint8_t subclass) {
    return vendor == VIRTIO_VENDOR_ID &&
           device == VIRTIO_DEVICE_NET_BASE &&
           class_code == PCI_CLASS_NETWORK &&
           subclass == PCI_SUBCLASS_ETHERNET;
}

static int virtio_net_is_modern(uint16_t vendor, uint16_t device) {
    uint16_t modern_id;
    if (vendor != VIRTIO_VENDOR_ID || device < VIRTIO_DEVICE_NET_MIN || device > VIRTIO_DEVICE_NET_MAX) return 0;
    modern_id = (uint16_t)(device - VIRTIO_DEVICE_NET_MIN);
    return modern_id == VIRTIO_DEVICE_NET_ID;
}

static void virtio_net_make_name(char *out, uint32_t index) {
    out[0] = 'v'; out[1] = 'n'; out[2] = 'e'; out[3] = 't';
    out[4] = (char)('0' + (char)(index % 10u));
    out[5] = '\0';
}

static uint32_t virtio_net_index_of(virtio_net_device_t *vdev) {
    return (uint32_t)(vdev - virtio_net_devices);
}

static void virtio_net_assign_fallback_mac(virtio_net_device_t *vdev, uint32_t index) {
    vdev->netdev.mac[0] = 0x02;
    vdev->netdev.mac[1] = 0x76;
    vdev->netdev.mac[2] = 0x69;
    vdev->netdev.mac[3] = 0x6e;
    vdev->netdev.mac[4] = 0x00;
    vdev->netdev.mac[5] = (uint8_t)(0x10u + (index & 0x0Fu));
}

static void virtio_net_read_mac(virtio_net_device_t *vdev) {
    uint32_t features = inl((uint16_t)(vdev->io_base + VIRTIO_PCI_HOST_FEATURES));
    uint16_t base = (uint16_t)(vdev->io_base + VIRTIO_PCI_CONFIG);
    uint32_t i;
    if ((features & (1u << VIRTIO_NET_F_MAC)) == 0u) return;
    for (i = 0; i < NET_ETH_ADDR_LEN; i++) {
        vdev->netdev.mac[i] = inb((uint16_t)(base + i));
    }
}

static int virtio_net_setup_queue(virtio_net_device_t *vdev, uint16_t queue_index,
                                  virtio_net_queue_t *queue) {
    uint16_t queue_num;
    memset(queue, 0, sizeof(*queue));
    outw((uint16_t)(vdev->io_base + VIRTIO_PCI_QUEUE_SEL), queue_index);
    queue_num = inw((uint16_t)(vdev->io_base + VIRTIO_PCI_QUEUE_NUM));
    if (queue_num == 0u || queue_num < VIRTIO_NET_QUEUE_SIZE) return -1;
    outl((uint16_t)(vdev->io_base + VIRTIO_PCI_QUEUE_PFN), virtio_ptr32(queue) >> 12);
    return inl((uint16_t)(vdev->io_base + VIRTIO_PCI_QUEUE_PFN)) == 0u ? -1 : 0;
}

static void virtio_net_rx_post(virtio_net_device_t *vdev, uint16_t desc_id) {
    uint32_t idx = virtio_net_index_of(vdev);
    uint16_t buf_id = (uint16_t)(desc_id % VIRTIO_NET_RX_BUFS);
    virtio_net_queue_t *q = vdev->rxq;
    uint16_t slot;

    q->desc[desc_id].addr = virtio_ptr32(virtio_net_rx_buffers[idx][buf_id]);
    q->desc[desc_id].len = VIRTIO_NET_FRAME_BUF;
    q->desc[desc_id].flags = VIRTQ_DESC_F_WRITE;
    q->desc[desc_id].next = 0u;

    slot = (uint16_t)(q->avail.idx % VIRTIO_NET_QUEUE_SIZE);
    q->avail.ring[slot] = desc_id;
    virtio_mb();
    q->avail.idx++;
}

static void virtio_net_poll_rx_device(virtio_net_device_t *vdev) {
    virtio_net_queue_t *q;
    uint32_t idx;
    uint32_t guard = VIRTIO_NET_QUEUE_SIZE;

    if (!vdev || !vdev->initialized) return;
    q = vdev->rxq;
    idx = virtio_net_index_of(vdev);

    while (q->used.idx != vdev->rx_last_used && guard--) {
        uint16_t used_slot = (uint16_t)(vdev->rx_last_used % VIRTIO_NET_QUEUE_SIZE);
        uint16_t desc_id = (uint16_t)q->used.ring[used_slot].id;
        uint32_t len = q->used.ring[used_slot].len;
        if (desc_id < VIRTIO_NET_RX_BUFS && len > sizeof(virtio_net_hdr_t)) {
            uint8_t *buf = virtio_net_rx_buffers[idx][desc_id];
            uint32_t frame_len = len - sizeof(virtio_net_hdr_t);
            if (frame_len <= NET_FRAME_MAX_SIZE) {
                net_input(&vdev->netdev, buf + sizeof(virtio_net_hdr_t), (uint16_t)frame_len);
            } else {
                vdev->netdev.rx_dropped++;
            }
        } else {
            vdev->netdev.rx_dropped++;
        }
        vdev->rx_last_used++;
        virtio_net_rx_post(vdev, desc_id);
    }
    virtio_mb();
    outw((uint16_t)(vdev->io_base + VIRTIO_PCI_QUEUE_NOTIFY), 0u);
}

static int virtio_net_transmit(net_device_t *dev, const uint8_t *frame, uint16_t len) {
    virtio_net_device_t *vdev = dev ? (virtio_net_device_t *)dev->driver_data : 0;
    virtio_net_queue_t *q;
    uint8_t *txbuf;
    uint16_t start_used;
    uint32_t i;
    uint32_t idx;

    if (!vdev || !vdev->initialized || !frame || len == 0u || len > NET_FRAME_MAX_SIZE) {
        if (dev) dev->tx_dropped++;
        return -1;
    }

    q = vdev->txq;
    idx = virtio_net_index_of(vdev);
    txbuf = virtio_net_tx_buffers[idx];
    memset(txbuf, 0, sizeof(virtio_net_hdr_t));
    memcpy(txbuf + sizeof(virtio_net_hdr_t), frame, len);

    q->desc[0].addr = virtio_ptr32(txbuf);
    q->desc[0].len = (uint32_t)sizeof(virtio_net_hdr_t) + len;
    q->desc[0].flags = 0u;
    q->desc[0].next = 0u;

    q->avail.ring[q->avail.idx % VIRTIO_NET_QUEUE_SIZE] = 0u;
    start_used = q->used.idx;
    virtio_mb();
    q->avail.idx++;
    virtio_mb();
    outw((uint16_t)(vdev->io_base + VIRTIO_PCI_QUEUE_NOTIFY), 1u);

    for (i = 0; i < VIRTIO_NET_WAIT_LIMIT; i++) {
        if (q->used.idx != start_used) {
            vdev->tx_last_used = q->used.idx;
            (void)inb((uint16_t)(vdev->io_base + VIRTIO_PCI_ISR));
            dev->tx_packets++;
            return 0;
        }
        io_wait();
    }

    dev->tx_dropped++;
    return -1;
}

static void virtio_net_irq(registers_t *regs) {
    uint32_t i;
    (void)regs;
    for (i = 0; i < virtio_net_count; i++) {
        virtio_net_device_t *vdev = &virtio_net_devices[i];
        uint8_t isr;
        if (!vdev->present || !vdev->initialized || vdev->io_base == 0u) continue;
        isr = inb((uint16_t)(vdev->io_base + VIRTIO_PCI_ISR));
        if (isr != 0u) virtio_net_poll_rx_device(vdev);
    }
}

static int virtio_net_hw_init(virtio_net_device_t *vdev, uint32_t index) {
    uint8_t status;
    uint32_t i;

    if (vdev->io_base == 0u) return -1;

    outb((uint16_t)(vdev->io_base + VIRTIO_PCI_STATUS), 0u);
    status = VIRTIO_STATUS_ACKNOWLEDGE;
    virtio_net_set_status(vdev, status);
    status |= VIRTIO_STATUS_DRIVER;
    virtio_net_set_status(vdev, status);

    virtio_net_read_mac(vdev);
    outl((uint16_t)(vdev->io_base + VIRTIO_PCI_GUEST_FEATURES), 0u);
    status |= VIRTIO_STATUS_FEATURES_OK;
    virtio_net_set_status(vdev, status);

    vdev->rxq = &virtio_net_rxq[index];
    vdev->txq = &virtio_net_txq[index];
    if (virtio_net_setup_queue(vdev, 0u, vdev->rxq) != 0 ||
        virtio_net_setup_queue(vdev, 1u, vdev->txq) != 0) {
        virtio_net_fail(vdev);
        return -1;
    }

    vdev->rx_last_used = 0u;
    vdev->tx_last_used = 0u;
    for (i = 0; i < VIRTIO_NET_RX_BUFS; i++) {
        virtio_net_rx_post(vdev, (uint16_t)i);
    }
    virtio_mb();
    outw((uint16_t)(vdev->io_base + VIRTIO_PCI_QUEUE_NOTIFY), 0u);

    status |= VIRTIO_STATUS_DRIVER_OK;
    virtio_net_set_status(vdev, status);
    vdev->initialized = 1u;
    return 0;
}

static void virtio_net_register_irq(virtio_net_device_t *vdev) {
    if (vdev->irq < 16u) {
        isr_install_handler((uint8_t)(32u + vdev->irq), virtio_net_irq);
    }
}

static void virtio_net_register(uint8_t bus, uint8_t dev, uint8_t func,
                                uint16_t vendor, uint16_t device, virtio_net_kind_t kind) {
    virtio_net_device_t *vdev;
    net_device_t *netdev;
    uint32_t index;

    if (virtio_net_count >= VIRTIO_NET_MAX_DEVICES) return;
    if (kind != VIRTIO_NET_KIND_LEGACY) {
        serial_write("[virtio-net] modern device found; common-cfg transport not enabled yet\n");
        return;
    }

    index = virtio_net_count;
    vdev = &virtio_net_devices[index];
    memset(vdev, 0, sizeof(*vdev));
    vdev->present = 1;
    vdev->bus = bus;
    vdev->dev = dev;
    vdev->func = func;
    vdev->irq = pci_read8(bus, dev, func, PCI_OFFSET_INTLINE);
    vdev->vendor_id = vendor;
    vdev->device_id = device;
    vdev->kind = kind;
    vdev->io_base = virtio_bar_addr(bus, dev, func, PCI_OFFSET_BAR0, 1);
    vdev->mmio_base = virtio_bar_addr(bus, dev, func, PCI_OFFSET_BAR0, 0);
    if (vdev->mmio_base == 0u) vdev->mmio_base = virtio_bar_addr(bus, dev, func, PCI_OFFSET_BAR1, 0);
    virtio_net_make_name(vdev->name, index);

    netdev = &vdev->netdev;
    strcpy(netdev->name, vdev->name);
    virtio_net_assign_fallback_mac(vdev, index);
    netdev->ip = NET_IP4(10, 0, 2, (uint8_t)(30u + index));
    netdev->netmask = NET_IP4(255, 255, 255, 0);
    netdev->gateway = NET_IP4(10, 0, 2, 2);
    netdev->dns = NET_IP4(8, 8, 8, 8);
    netdev->config_mode = NET_CONFIG_MODE_STATIC;
    netdev->link_up = 1;
    netdev->transmit = virtio_net_transmit;
    netdev->driver_data = vdev;

    if (virtio_net_hw_init(vdev, index) == 0) {
        virtio_net_register_irq(vdev);
    }

    if (net_register_device(netdev) < 0) {
        memset(vdev, 0, sizeof(*vdev));
        return;
    }

    serial_write("[virtio-net] registered ");
    serial_write(vdev->name);
    serial_write(" dev=0x");
    serial_write_hex(device);
    serial_write(" io=0x");
    serial_write_hex(vdev->io_base);
    serial_write(" irq=");
    serial_write_hex(vdev->irq);
    serial_write(vdev->initialized ? " hw=ready\n" : " hw=unavailable\n");

    virtio_net_count++;
}

uint32_t virtio_net_device_count(void) {
    return virtio_net_count;
}

void virtio_net_init(void) {
    uint16_t bus;
    uint16_t dev;
    uint16_t func;

    memset(virtio_net_devices, 0, sizeof(virtio_net_devices));
    memset(virtio_net_rxq, 0, sizeof(virtio_net_rxq));
    memset(virtio_net_txq, 0, sizeof(virtio_net_txq));
    memset(virtio_net_rx_buffers, 0, sizeof(virtio_net_rx_buffers));
    memset(virtio_net_tx_buffers, 0, sizeof(virtio_net_tx_buffers));
    virtio_net_count = 0;

    serial_write("[virtio-net] probing PCI devices\n");
    for (bus = 0; bus < 256u; bus++) {
        for (dev = 0; dev < 32u; dev++) {
            for (func = 0; func < 8u; func++) {
                uint16_t vendor = pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_VENDOR);
                if (vendor == PCI_VENDOR_INVALID) {
                    if (func == 0u) break;
                    continue;
                }

                {
                    uint16_t device = pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_DEVICE);
                    uint8_t class_code = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_CLASS + 2u);
                    uint8_t subclass = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_CLASS + 1u);
                    virtio_net_kind_t kind = VIRTIO_NET_KIND_LEGACY;
                    int is_net = 0;
                    if (virtio_net_is_legacy(vendor, device, class_code, subclass)) {
                        kind = VIRTIO_NET_KIND_LEGACY;
                        is_net = 1;
                    } else if (virtio_net_is_modern(vendor, device)) {
                        kind = VIRTIO_NET_KIND_MODERN;
                        is_net = 1;
                    }
                    if (is_net) {
                        virtio_net_register((uint8_t)bus, (uint8_t)dev, (uint8_t)func, vendor, device, kind);
                        if (virtio_net_count >= VIRTIO_NET_MAX_DEVICES) return;
                    }
                }

                if ((pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_HEADER) & 0x80u) == 0u) break;
            }
        }
    }

    if (virtio_net_count == 0u) serial_write("[virtio-net] no device\n");
    else serial_write("[virtio-net] probe complete\n");
}
