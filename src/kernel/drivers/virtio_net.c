/* ============================================================
 * openos - virtio-net discovery network driver scaffold
 * ============================================================ */
#include "../include/virtio_net.h"
#include "../include/pci.h"
#include "../include/serial.h"
#include "../include/string.h"
#include "../net/net.h"

#define VIRTIO_NET_MAX_DEVICES 8u

#define PCI_CLASS_NETWORK      0x02u
#define PCI_SUBCLASS_ETHERNET  0x00u

#define VIRTIO_VENDOR_ID       0x1AF4u
#define VIRTIO_DEVICE_NET_BASE 0x1000u
#define VIRTIO_DEVICE_NET_MIN  0x1040u
#define VIRTIO_DEVICE_NET_MAX  0x107Fu
#define VIRTIO_DEVICE_NET_ID   0x0001u

typedef struct virtio_net_device {
    uint8_t present;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t io_base;
    uint32_t mmio_base;
    char name[16];
    net_device_t netdev;
} virtio_net_device_t;

static virtio_net_device_t virtio_net_devices[VIRTIO_NET_MAX_DEVICES];
static uint32_t virtio_net_count;

static int virtio_net_transmit(net_device_t *dev, const uint8_t *frame, uint16_t len) {
    (void)frame;
    (void)len;
    if (dev) dev->tx_dropped++;
    return -1;
}

uint32_t virtio_net_device_count(void) {
    return virtio_net_count;
}

static void virtio_net_make_name(char *out, uint32_t index) {
    out[0] = 'v';
    out[1] = 'n';
    out[2] = 'e';
    out[3] = 't';
    out[4] = (char)('0' + (char)(index % 10u));
    out[5] = '\0';
}

static uint32_t virtio_bar_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, int want_io) {
    uint32_t bar = pci_read32(bus, dev, func, off);
    if ((bar & 0x1u) != 0u) {
        if (want_io) return bar & 0xFFFFFFFCu;
        return 0u;
    }
    if (!want_io) return bar & 0xFFFFFFF0u;
    return 0u;
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
    if (vendor != VIRTIO_VENDOR_ID || device < VIRTIO_DEVICE_NET_MIN || device > VIRTIO_DEVICE_NET_MAX) {
        return 0;
    }
    modern_id = (uint16_t)(device - VIRTIO_DEVICE_NET_MIN);
    return modern_id == VIRTIO_DEVICE_NET_ID;
}

static void virtio_net_assign_mac(virtio_net_device_t *vdev, uint32_t index) {
    vdev->netdev.mac[0] = 0x02;
    vdev->netdev.mac[1] = 0x76;
    vdev->netdev.mac[2] = 0x69;
    vdev->netdev.mac[3] = 0x6e;
    vdev->netdev.mac[4] = 0x00;
    vdev->netdev.mac[5] = (uint8_t)(0x10u + (index & 0x0Fu));
}

static void virtio_net_register(uint8_t bus, uint8_t dev, uint8_t func,
                                uint16_t vendor, uint16_t device) {
    virtio_net_device_t *vdev;
    net_device_t *netdev;

    if (virtio_net_count >= VIRTIO_NET_MAX_DEVICES) return;

    vdev = &virtio_net_devices[virtio_net_count];
    memset(vdev, 0, sizeof(*vdev));
    vdev->present = 1;
    vdev->bus = bus;
    vdev->dev = dev;
    vdev->func = func;
    vdev->vendor_id = vendor;
    vdev->device_id = device;
    vdev->io_base = virtio_bar_addr(bus, dev, func, PCI_OFFSET_BAR0, 1);
    vdev->mmio_base = virtio_bar_addr(bus, dev, func, PCI_OFFSET_BAR0, 0);
    if (vdev->mmio_base == 0u) {
        vdev->mmio_base = virtio_bar_addr(bus, dev, func, PCI_OFFSET_BAR1, 0);
    }
    virtio_net_make_name(vdev->name, virtio_net_count);

    netdev = &vdev->netdev;
    strcpy(netdev->name, vdev->name);
    virtio_net_assign_mac(vdev, virtio_net_count);
    netdev->ip = NET_IP4(10, 0, 2, (uint8_t)(30u + virtio_net_count));
    netdev->netmask = NET_IP4(255, 255, 255, 0);
    netdev->gateway = NET_IP4(10, 0, 2, 2);
    netdev->transmit = virtio_net_transmit;
    netdev->driver_data = vdev;

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
    serial_write(" mmio=0x");
    serial_write_hex(vdev->mmio_base);
    serial_write("\n");

    virtio_net_count++;
}

void virtio_net_init(void) {
    uint16_t bus;
    uint16_t dev;
    uint16_t func;

    memset(virtio_net_devices, 0, sizeof(virtio_net_devices));
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
                    int is_net = virtio_net_is_legacy(vendor, device, class_code, subclass) ||
                                 virtio_net_is_modern(vendor, device);

                    if (is_net) {
                        virtio_net_register((uint8_t)bus, (uint8_t)dev, (uint8_t)func, vendor, device);
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
