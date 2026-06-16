/* ============================================================
 * openos - USB common core
 *
 * This module provides the common USB bus model used by class and
 * controller drivers: PCI host-controller discovery, controller/device
 * registries, descriptor ABI definitions and diagnostics.
 * ============================================================ */

#include "usb.h"
#include "pci.h"
#include "serial.h"
#include "string.h"

#define PCI_USB_CLASS      0x0C
#define PCI_USB_SUBCLASS   0x03
#define USB_BAR_IO_MASK    0xFFFFFFFCu
#define USB_BAR_MEM_MASK   0xFFFFFFF0u

static usb_host_controller_t g_hosts[USB_MAX_HOST_CONTROLLERS];
static usb_device_t g_devices[USB_MAX_DEVICES];
static usb_stats_t g_stats;
static uint8_t g_initialized;

static void usb_clear_registry(void) {
    memset(g_hosts, 0, sizeof(g_hosts));
    memset(g_devices, 0, sizeof(g_devices));
    g_stats.host_count = 0;
    g_stats.device_count = 0;
    g_stats.uhci_count = 0;
    g_stats.ohci_count = 0;
    g_stats.ehci_count = 0;
    g_stats.xhci_count = 0;
}

const char *usb_hc_type_name(usb_hc_type_t type) {
    switch (type) {
        case USB_HC_UHCI: return "UHCI";
        case USB_HC_OHCI: return "OHCI";
        case USB_HC_EHCI: return "EHCI";
        case USB_HC_XHCI: return "xHCI";
        default: return "unknown";
    }
}

const char *usb_speed_name(usb_speed_t speed) {
    switch (speed) {
        case USB_SPEED_LOW: return "low";
        case USB_SPEED_FULL: return "full";
        case USB_SPEED_HIGH: return "high";
        case USB_SPEED_SUPER: return "super";
        default: return "unknown";
    }
}

const char *usb_device_state_name(usb_device_state_t state) {
    switch (state) {
        case USB_DEV_ATTACHED: return "attached";
        case USB_DEV_POWERED: return "powered";
        case USB_DEV_DEFAULT: return "default";
        case USB_DEV_ADDRESS: return "address";
        case USB_DEV_CONFIGURED: return "configured";
        default: return "empty";
    }
}

static usb_hc_type_t usb_type_from_prog_if(uint8_t prog_if) {
    switch (prog_if) {
        case 0x00: return USB_HC_UHCI;
        case 0x10: return USB_HC_OHCI;
        case 0x20: return USB_HC_EHCI;
        case 0x30: return USB_HC_XHCI;
        default: return USB_HC_UNKNOWN;
    }
}

static uint8_t usb_bar_index_for_type(usb_hc_type_t type) {
    if (type == USB_HC_UHCI) {
        return PCI_OFFSET_BAR4;
    }
    return PCI_OFFSET_BAR0;
}

static void usb_count_host_type(usb_hc_type_t type) {
    if (type == USB_HC_UHCI) g_stats.uhci_count++;
    else if (type == USB_HC_OHCI) g_stats.ohci_count++;
    else if (type == USB_HC_EHCI) g_stats.ehci_count++;
    else if (type == USB_HC_XHCI) g_stats.xhci_count++;
}

static void usb_register_root_device(uint8_t host_index) {
    usb_host_controller_t *host;
    usb_device_t *dev;
    if (host_index >= USB_MAX_HOST_CONTROLLERS || g_stats.device_count >= USB_MAX_DEVICES) {
        return;
    }
    host = &g_hosts[host_index];
    dev = &g_devices[g_stats.device_count];
    memset(dev, 0, sizeof(*dev));
    dev->used = 1;
    dev->address = 0;
    dev->host_index = host_index;
    dev->port = 0;
    dev->state = USB_DEV_ATTACHED;
    dev->interface_class = USB_CLASS_HUB;
    dev->max_packet0 = 8;
    if (host->type == USB_HC_EHCI) dev->speed = USB_SPEED_HIGH;
    else if (host->type == USB_HC_XHCI) dev->speed = USB_SPEED_SUPER;
    else dev->speed = USB_SPEED_FULL;
    host->devices_seen++;
    g_stats.device_count++;
}

static void usb_register_host(uint8_t bus, uint8_t dev, uint8_t func, uint32_t class_reg) {
    usb_host_controller_t *host;
    usb_hc_type_t type;
    uint8_t prog_if;
    uint8_t bar_index;
    uint32_t bar;
    uint32_t slot;

    if (g_stats.host_count >= USB_MAX_HOST_CONTROLLERS) {
        return;
    }

    prog_if = (uint8_t)(class_reg >> 8);
    type = usb_type_from_prog_if(prog_if);
    slot = g_stats.host_count;
    host = &g_hosts[slot];
    memset(host, 0, sizeof(*host));
    host->used = 1;
    host->type = type;
    host->bus = bus;
    host->dev = dev;
    host->func = func;
    host->irq = pci_read8(bus, dev, func, PCI_OFFSET_INTLINE);
    host->pci_class = class_reg;

    bar_index = usb_bar_index_for_type(type);
    bar = pci_read32(bus, dev, func, bar_index);
    if (bar & 1u) {
        host->io_base = (uint16_t)(bar & USB_BAR_IO_MASK);
        host->mem_base = 0;
    } else {
        host->io_base = 0;
        host->mem_base = bar & USB_BAR_MEM_MASK;
    }

    g_stats.host_count++;
    usb_count_host_type(type);
    usb_register_root_device((uint8_t)slot);
}

void usb_rescan(void) {
    g_stats.scans++;
    usb_clear_registry();
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint8_t header;
            uint8_t func_count;
            uint16_t vendor = pci_read16((uint8_t)bus, dev, 0, PCI_OFFSET_VENDOR);
            if (vendor == PCI_VENDOR_INVALID) {
                continue;
            }
            header = pci_read8((uint8_t)bus, dev, 0, PCI_OFFSET_HEADER);
            func_count = (header & 0x80) ? 8 : 1;
            for (uint8_t func = 0; func < func_count; func++) {
                uint32_t class_reg;
                uint8_t class_code;
                uint8_t subclass;
                vendor = pci_read16((uint8_t)bus, dev, func, PCI_OFFSET_VENDOR);
                if (vendor == PCI_VENDOR_INVALID) {
                    continue;
                }
                class_reg = pci_read32((uint8_t)bus, dev, func, PCI_OFFSET_CLASS);
                class_code = (uint8_t)(class_reg >> 24);
                subclass = (uint8_t)(class_reg >> 16);
                if (class_code == PCI_USB_CLASS && subclass == PCI_USB_SUBCLASS) {
                    usb_register_host((uint8_t)bus, dev, func, class_reg);
                }
            }
        }
    }
}

void usb_init(void) {
    memset(&g_stats, 0, sizeof(g_stats));
    usb_clear_registry();
    g_initialized = 1;
    usb_rescan();
    serial_write("[OK] USB core hosts=");
    serial_write_hex(g_stats.host_count);
    serial_write(" devices=");
    serial_write_hex(g_stats.device_count);
    serial_write("\n");
}

uint32_t usb_host_count(void) {
    return g_stats.host_count;
}

uint32_t usb_device_count(void) {
    return g_stats.device_count;
}

const usb_host_controller_t *usb_get_host(uint32_t index) {
    if (index >= g_stats.host_count || index >= USB_MAX_HOST_CONTROLLERS) {
        return 0;
    }
    return &g_hosts[index];
}

const usb_device_t *usb_get_device(uint32_t index) {
    if (index >= g_stats.device_count || index >= USB_MAX_DEVICES) {
        return 0;
    }
    return &g_devices[index];
}

const usb_stats_t *usb_get_stats(void) {
    return &g_stats;
}

void usb_print_info(void) {
    if (!g_initialized) {
        serial_write("[USB] core not initialized\n");
        return;
    }
    serial_write("[USB] scans="); serial_write_hex(g_stats.scans);
    serial_write(" hosts="); serial_write_hex(g_stats.host_count);
    serial_write(" devices="); serial_write_hex(g_stats.device_count);
    serial_write(" uhci="); serial_write_hex(g_stats.uhci_count);
    serial_write(" ohci="); serial_write_hex(g_stats.ohci_count);
    serial_write(" ehci="); serial_write_hex(g_stats.ehci_count);
    serial_write(" xhci="); serial_write_hex(g_stats.xhci_count);
    serial_write("\n");
    for (uint32_t i = 0; i < g_stats.host_count; i++) {
        const usb_host_controller_t *h = &g_hosts[i];
        serial_write("[USB] hc#"); serial_write_hex(i);
        serial_write(" "); serial_write(usb_hc_type_name(h->type));
        serial_write(" pci="); serial_write_hex(h->bus); serial_write(":");
        serial_write_hex(h->dev); serial_write("."); serial_write_hex(h->func);
        serial_write(" irq="); serial_write_hex(h->irq);
        serial_write(" io="); serial_write_hex(h->io_base);
        serial_write(" mem="); serial_write_hex(h->mem_base);
        serial_write(" root_devices="); serial_write_hex(h->devices_seen);
        serial_write("\n");
    }
    for (uint32_t i = 0; i < g_stats.device_count; i++) {
        const usb_device_t *d = &g_devices[i];
        serial_write("[USB] dev#"); serial_write_hex(i);
        serial_write(" host="); serial_write_hex(d->host_index);
        serial_write(" addr="); serial_write_hex(d->address);
        serial_write(" port="); serial_write_hex(d->port);
        serial_write(" speed="); serial_write(usb_speed_name(d->speed));
        serial_write(" state="); serial_write(usb_device_state_name(d->state));
        serial_write(" class="); serial_write_hex(d->interface_class);
        serial_write(" vendor="); serial_write_hex(d->vendor_id);
        serial_write(" product="); serial_write_hex(d->product_id);
        serial_write("\n");
    }
}
