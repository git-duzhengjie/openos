/* ============================================================
 * openos - Realtek RTL8139 PCI discovery network driver scaffold
 * ============================================================ */
#include "../include/rtl8139.h"
#include "../include/pci.h"
#include "../include/serial.h"
#include "../include/string.h"
#include "../net/net.h"

#define RTL8139_MAX_DEVICES 8u

#define PCI_CLASS_NETWORK      0x02u
#define PCI_SUBCLASS_ETHERNET  0x00u

#define REALTEK_VENDOR_ID      0x10ECu
#define RTL8139_DEVICE_ID      0x8139u
#define RTL8100_DEVICE_ID      0x8138u

typedef struct rtl8139_device {
    uint8_t present;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint16_t device_id;
    uint32_t io_base;
    uint32_t mmio_base;
    char name[16];
    net_device_t netdev;
} rtl8139_device_t;

static rtl8139_device_t rtl8139_devices[RTL8139_MAX_DEVICES];
static uint32_t rtl8139_count;

static int rtl8139_transmit(net_device_t *dev, const uint8_t *frame, uint16_t len) {
    (void)frame;
    (void)len;
    if (dev) {
        dev->tx_dropped++;
    }
    return -1;
}

uint32_t rtl8139_device_count(void) {
    return rtl8139_count;
}

static void rtl8139_make_name(char *out, uint32_t index) {
    out[0] = 'r';
    out[1] = 't';
    out[2] = 'l';
    out[3] = '0';
    out[4] = (char)('0' + (char)(index % 10u));
    out[5] = '\0';
}

static uint32_t rtl8139_bar_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, int want_io) {
    uint32_t bar = pci_read32(bus, dev, func, off);
    if (bar == 0u || bar == 0xFFFFFFFFu) {
        return 0u;
    }
    if ((bar & 0x1u) != 0u) {
        return want_io ? (bar & 0xFFFFFFFCu) : 0u;
    }
    return want_io ? 0u : (bar & 0xFFFFFFF0u);
}

static int rtl8139_supported_device(uint16_t vendor, uint16_t device,
                                    uint8_t class_code, uint8_t subclass) {
    if (vendor != REALTEK_VENDOR_ID || class_code != PCI_CLASS_NETWORK || subclass != PCI_SUBCLASS_ETHERNET) {
        return 0;
    }
    return device == RTL8139_DEVICE_ID || device == RTL8100_DEVICE_ID;
}

static void rtl8139_assign_mac(rtl8139_device_t *rdev, uint32_t index) {
    rdev->netdev.mac[0] = 0x02;
    rdev->netdev.mac[1] = 0x81;
    rdev->netdev.mac[2] = 0x39;
    rdev->netdev.mac[3] = 0x00;
    rdev->netdev.mac[4] = 0x00;
    rdev->netdev.mac[5] = (uint8_t)(0x30u + (index & 0x0Fu));
}

static const char *rtl8139_model_name(uint16_t device) {
    if (device == RTL8139_DEVICE_ID) {
        return "RTL8139";
    }
    if (device == RTL8100_DEVICE_ID) {
        return "RTL8100";
    }
    return "unknown";
}

static void rtl8139_register(uint8_t bus, uint8_t dev, uint8_t func, uint16_t device) {
    rtl8139_device_t *rdev;
    net_device_t *netdev;

    if (rtl8139_count >= RTL8139_MAX_DEVICES) {
        return;
    }

    rdev = &rtl8139_devices[rtl8139_count];
    memset(rdev, 0, sizeof(*rdev));
    rdev->present = 1;
    rdev->bus = bus;
    rdev->dev = dev;
    rdev->func = func;
    rdev->device_id = device;
    rdev->io_base = rtl8139_bar_addr(bus, dev, func, PCI_OFFSET_BAR0, 1);
    rdev->mmio_base = rtl8139_bar_addr(bus, dev, func, PCI_OFFSET_BAR1, 0);
    if (rdev->mmio_base == 0u) {
        rdev->mmio_base = rtl8139_bar_addr(bus, dev, func, PCI_OFFSET_BAR0, 0);
    }
    rtl8139_make_name(rdev->name, rtl8139_count);

    netdev = &rdev->netdev;
    strcpy(netdev->name, rdev->name);
    rtl8139_assign_mac(rdev, rtl8139_count);
    netdev->ip = NET_IP4(10, 0, 2, (uint8_t)(50u + rtl8139_count));
    netdev->netmask = NET_IP4(255, 255, 255, 0);
    netdev->gateway = NET_IP4(10, 0, 2, 2);
    netdev->transmit = rtl8139_transmit;
    netdev->driver_data = rdev;

    if (net_register_device(netdev) < 0) {
        memset(rdev, 0, sizeof(*rdev));
        return;
    }

    serial_write("[rtl8139] registered ");
    serial_write(rdev->name);
    serial_write(" model=");
    serial_write(rtl8139_model_name(device));
    serial_write(" dev=0x");
    serial_write_hex(device);
    serial_write(" io=0x");
    serial_write_hex(rdev->io_base);
    serial_write(" mmio=0x");
    serial_write_hex(rdev->mmio_base);
    serial_write("\n");

    rtl8139_count++;
}

void rtl8139_init(void) {
    uint16_t bus;
    uint16_t dev;
    uint16_t func;

    memset(rtl8139_devices, 0, sizeof(rtl8139_devices));
    rtl8139_count = 0;

    serial_write("[rtl8139] probing PCI devices\n");
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
                    if (rtl8139_supported_device(vendor, device, class_code, subclass)) {
                        rtl8139_register((uint8_t)bus, (uint8_t)dev, (uint8_t)func, device);
                        if (rtl8139_count >= RTL8139_MAX_DEVICES) {
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

    if (rtl8139_count == 0u) {
        serial_write("[rtl8139] no device\n");
    } else {
        serial_write("[rtl8139] probe complete\n");
    }
}
