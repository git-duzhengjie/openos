/* ============================================================
 * openos - Intel e1000 PCI discovery network driver scaffold
 * ============================================================ */
#include "../include/e1000.h"
#include "../include/pci.h"
#include "../include/serial.h"
#include "../include/string.h"
#include "../net/net.h"

#define E1000_MAX_DEVICES 8u

#define PCI_CLASS_NETWORK      0x02u
#define PCI_SUBCLASS_ETHERNET  0x00u

#define INTEL_VENDOR_ID        0x8086u

#define E1000_DEV_82540EM      0x100Eu
#define E1000_DEV_82545EM      0x100Fu
#define E1000_DEV_82546EB      0x1010u
#define E1000_DEV_82541PI      0x107Cu
#define E1000_DEV_82572EI      0x107Du
#define E1000_DEV_82573L       0x109Au
#define E1000_DEV_82574L       0x10D3u
#define E1000_DEV_82576        0x10C9u

typedef struct e1000_device {
    uint8_t present;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint16_t device_id;
    uint32_t mmio_base;
    uint32_t io_base;
    char name[16];
    net_device_t netdev;
} e1000_device_t;

static e1000_device_t e1000_devices[E1000_MAX_DEVICES];
static uint32_t e1000_count;

static int e1000_transmit(net_device_t *dev, const uint8_t *frame, uint16_t len) {
    (void)frame;
    (void)len;
    if (dev) {
        dev->tx_dropped++;
    }
    return -1;
}

uint32_t e1000_device_count(void) {
    return e1000_count;
}

static void e1000_make_name(char *out, uint32_t index) {
    out[0] = 'e';
    out[1] = '1';
    out[2] = '0';
    out[3] = '0';
    out[4] = '0';
    out[5] = (char)('0' + (char)(index % 10u));
    out[6] = '\0';
}

static uint32_t e1000_bar_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, int want_io) {
    uint32_t bar = pci_read32(bus, dev, func, off);
    if (bar == 0u || bar == 0xFFFFFFFFu) {
        return 0u;
    }
    if ((bar & 0x1u) != 0u) {
        return want_io ? (bar & 0xFFFFFFFCu) : 0u;
    }
    return want_io ? 0u : (bar & 0xFFFFFFF0u);
}

static int e1000_supported_device(uint16_t vendor, uint16_t device,
                                  uint8_t class_code, uint8_t subclass) {
    if (vendor != INTEL_VENDOR_ID || class_code != PCI_CLASS_NETWORK || subclass != PCI_SUBCLASS_ETHERNET) {
        return 0;
    }

    switch (device) {
        case E1000_DEV_82540EM:
        case E1000_DEV_82545EM:
        case E1000_DEV_82546EB:
        case E1000_DEV_82541PI:
        case E1000_DEV_82572EI:
        case E1000_DEV_82573L:
        case E1000_DEV_82574L:
        case E1000_DEV_82576:
            return 1;
        default:
            return 0;
    }
}

static void e1000_assign_mac(e1000_device_t *edev, uint32_t index) {
    edev->netdev.mac[0] = 0x02;
    edev->netdev.mac[1] = 0xe1;
    edev->netdev.mac[2] = 0x00;
    edev->netdev.mac[3] = 0x00;
    edev->netdev.mac[4] = 0x00;
    edev->netdev.mac[5] = (uint8_t)(0x20u + (index & 0x0Fu));
}

static void e1000_register(uint8_t bus, uint8_t dev, uint8_t func, uint16_t device) {
    e1000_device_t *edev;
    net_device_t *netdev;

    if (e1000_count >= E1000_MAX_DEVICES) {
        return;
    }

    edev = &e1000_devices[e1000_count];
    memset(edev, 0, sizeof(*edev));
    edev->present = 1;
    edev->bus = bus;
    edev->dev = dev;
    edev->func = func;
    edev->device_id = device;
    edev->mmio_base = e1000_bar_addr(bus, dev, func, PCI_OFFSET_BAR0, 0);
    edev->io_base = e1000_bar_addr(bus, dev, func, PCI_OFFSET_BAR1, 1);
    if (edev->io_base == 0u) {
        edev->io_base = e1000_bar_addr(bus, dev, func, PCI_OFFSET_BAR0, 1);
    }
    e1000_make_name(edev->name, e1000_count);

    netdev = &edev->netdev;
    strcpy(netdev->name, edev->name);
    e1000_assign_mac(edev, e1000_count);
    netdev->ip = NET_IP4(10, 0, 2, (uint8_t)(40u + e1000_count));
    netdev->netmask = NET_IP4(255, 255, 255, 0);
    netdev->gateway = NET_IP4(10, 0, 2, 2);
    netdev->transmit = e1000_transmit;
    netdev->driver_data = edev;

    if (net_register_device(netdev) < 0) {
        memset(edev, 0, sizeof(*edev));
        return;
    }

    serial_write("[e1000] registered ");
    serial_write(edev->name);
    serial_write(" dev=0x");
    serial_write_hex(device);
    serial_write(" mmio=0x");
    serial_write_hex(edev->mmio_base);
    serial_write(" io=0x");
    serial_write_hex(edev->io_base);
    serial_write("\n");

    e1000_count++;
}

void e1000_init(void) {
    uint16_t bus;
    uint16_t dev;
    uint16_t func;

    memset(e1000_devices, 0, sizeof(e1000_devices));
    e1000_count = 0;

    serial_write("[e1000] probing PCI devices\n");
    for (bus = 0; bus < 256u; bus++) {
        for (dev = 0; dev < 32u; dev++) {
            for (func = 0; func < 8u; func++) {
                uint16_t vendor = pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_VENDOR);
                if (vendor == PCI_VENDOR_INVALID) {
                    if (func == 0u) {
                        break;
                    }
                    continue;
                }

                {
                    uint16_t device = pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_DEVICE);
                    uint8_t class_code = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_CLASS + 2u);
                    uint8_t subclass = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_CLASS + 1u);
                    if (e1000_supported_device(vendor, device, class_code, subclass)) {
                        e1000_register((uint8_t)bus, (uint8_t)dev, (uint8_t)func, device);
                        if (e1000_count >= E1000_MAX_DEVICES) {
                            return;
                        }
                    }
                }

                if ((pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_HEADER) & 0x80u) == 0u) {
                    break;
                }
            }
        }
    }

    if (e1000_count == 0u) {
        serial_write("[e1000] no device\n");
    } else {
        serial_write("[e1000] probe complete\n");
    }
}
