/* ============================================================
 * openos - PCI bus driver with devmgr hotplug tracking
 * ============================================================ */
#include "../include/pci.h"
#include "../include/devmgr.h"
#include "../include/serial.h"
#include "../include/io.h"
#include "../include/string.h"

#define PCI_TRACK_MAX 128u

typedef struct pci_snapshot_entry {
    uint8_t present;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint8_t class_code;
    uint8_t sub_class;
    uint8_t prog_if;
    uint16_t vendor;
    uint16_t device;
    char name[DEVMGR_NAME_MAX];
} pci_snapshot_entry_t;

static pci_snapshot_entry_t pci_known[PCI_TRACK_MAX];
static uint32_t pci_known_count;

uint32_t pci_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    return (1u << 31)
         | ((uint32_t)bus << 16)
         | ((uint32_t)dev << 11)
         | ((uint32_t)func << 8)
         | (off & 0xFCu);
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, off));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    uint32_t val = pci_read32(bus, dev, func, off);
    return (uint16_t)((val >> ((off & 3) * 8)) & 0xFFFFu);
}

uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    uint32_t val = pci_read32(bus, dev, func, off);
    return (uint8_t)((val >> ((off & 3) * 8)) & 0xFFu);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, off));
    outl(PCI_CONFIG_DATA, val);
}

void pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint16_t val) {
    uint32_t old = pci_read32(bus, dev, func, off);
    uint32_t mask = ~((uint32_t)0xFFFFu << ((off & 3) * 8));
    uint32_t nv = (old & mask) | ((uint32_t)val << ((off & 3) * 8));
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, off));
    outl(PCI_CONFIG_DATA, nv);
}

void pci_write8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint8_t val) {
    uint32_t old = pci_read32(bus, dev, func, off);
    uint32_t mask = ~((uint32_t)0xFFu << ((off & 3) * 8));
    uint32_t nv = (old & mask) | ((uint32_t)val << ((off & 3) * 8));
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, off));
    outl(PCI_CONFIG_DATA, nv);
}

static char hex_digit(uint8_t value) {
    value &= 0x0Fu;
    if (value < 10u) return (char)('0' + (char)value);
    return (char)('a' + (char)(value - 10u));
}

static void pci_make_name(char *name, uint8_t bus, uint8_t dev, uint8_t func) {
    name[0] = 'p'; name[1] = 'c'; name[2] = 'i';
    name[3] = hex_digit((uint8_t)(bus >> 4));
    name[4] = hex_digit(bus);
    name[5] = ':';
    name[6] = hex_digit((uint8_t)(dev >> 4));
    name[7] = hex_digit(dev);
    name[8] = '.';
    name[9] = hex_digit(func);
    name[10] = '\0';
}

static devmgr_type_t pci_type_from_class(uint8_t class_code) {
    if (class_code == 0x01u) return DEVMGR_TYPE_STORAGE;
    if (class_code == 0x02u) return DEVMGR_TYPE_NET;
    if (class_code == 0x03u) return DEVMGR_TYPE_CHAR;
    return DEVMGR_TYPE_UNKNOWN;
}

static int pci_entry_same_slot(const pci_snapshot_entry_t *a, const pci_snapshot_entry_t *b) {
    return a->bus == b->bus && a->dev == b->dev && a->func == b->func;
}

static int pci_entry_same_identity(const pci_snapshot_entry_t *a, const pci_snapshot_entry_t *b) {
    return a->vendor == b->vendor && a->device == b->device &&
           a->class_code == b->class_code && a->sub_class == b->sub_class &&
           a->prog_if == b->prog_if;
}

static uint32_t pci_collect_snapshot(pci_snapshot_entry_t *out, uint32_t max) {
    uint16_t bus, dev, func;
    uint32_t count = 0;

    for (bus = 0; bus < 256; bus++) {
        for (dev = 0; dev < 32; dev++) {
            for (func = 0; func < 8; func++) {
                uint16_t vendor = pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_VENDOR);
                if (vendor == PCI_VENDOR_INVALID) {
                    if (func == 0) break;
                    continue;
                }

                if (count < max) {
                    pci_snapshot_entry_t *entry = &out[count++];
                    memset(entry, 0, sizeof(*entry));
                    entry->present = 1;
                    entry->bus = (uint8_t)bus;
                    entry->dev = (uint8_t)dev;
                    entry->func = (uint8_t)func;
                    entry->vendor = vendor;
                    entry->device = pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_DEVICE);
                    entry->class_code = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_CLASS);
                    entry->sub_class = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_SUBCLASS);
                    entry->prog_if = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_PROGIF);
                    pci_make_name(entry->name, entry->bus, entry->dev, entry->func);
                }

                if ((pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_HEADER) & 0x80u) == 0) break;
            }
        }
    }
    return count;
}

static void pci_register_entry(const pci_snapshot_entry_t *entry) {
    uint32_t major = ((uint32_t)entry->vendor << 16) | entry->device;
    uint32_t minor = ((uint32_t)entry->bus << 16) | ((uint32_t)entry->dev << 8) | entry->func;
    if (!devmgr_find(entry->name)) {
        (void)devmgr_register(entry->name, "pci", pci_type_from_class(entry->class_code),
                              major, minor, 0, 0);
    }
}

uint32_t pci_known_device_count(void) {
    return pci_known_count;
}

int pci_rescan_hotplug(void) {
    pci_snapshot_entry_t now[PCI_TRACK_MAX];
    uint8_t matched_old[PCI_TRACK_MAX];
    uint32_t now_count;
    uint32_t i, j;
    int changes = 0;

    memset(now, 0, sizeof(now));
    memset(matched_old, 0, sizeof(matched_old));
    now_count = pci_collect_snapshot(now, PCI_TRACK_MAX);

    for (i = 0; i < now_count; i++) {
        int found = -1;
        for (j = 0; j < pci_known_count; j++) {
            if (pci_entry_same_slot(&now[i], &pci_known[j])) {
                found = (int)j;
                matched_old[j] = 1;
                break;
            }
        }

        if (found < 0) {
            pci_register_entry(&now[i]);
            changes++;
        } else if (!pci_entry_same_identity(&now[i], &pci_known[(uint32_t)found])) {
            pci_register_entry(&now[i]);
            (void)devmgr_notify_change(now[i].name);
            changes++;
        }
    }

    for (j = 0; j < pci_known_count; j++) {
        if (!matched_old[j]) {
            (void)devmgr_unregister(pci_known[j].name);
            changes++;
        }
    }

    memcpy(pci_known, now, sizeof(pci_known));
    pci_known_count = now_count;
    return changes;
}

void pci_scan_all(void) {
    uint16_t bus, dev, func;

    serial_write("=====================================\n");
    serial_write("PCI Bus Scan\n");
    serial_write("=====================================\n");
    serial_write("BUS DEV FUNC VENDOR DEVICE CLASS\n");
    serial_write("-------------------------------------\n");

    for (bus = 0; bus < 256; bus++) {
        for (dev = 0; dev < 32; dev++) {
            for (func = 0; func < 8; func++) {
                uint16_t vendor = pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_VENDOR);
                if (vendor == PCI_VENDOR_INVALID) {
                    if (func == 0) break;
                    else continue;
                }

                uint16_t device = pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_DEVICE);
                uint8_t class_code = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_CLASS);
                uint8_t sub_class = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_SUBCLASS);
                uint8_t prog_if = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_PROGIF);

                serial_write_hex(bus);
                serial_write("  ");
                serial_write_hex(dev);
                serial_write("  ");
                serial_write_hex(func);
                serial_write("    ");
                serial_write_hex(vendor);
                serial_write("  ");
                serial_write_hex(device);
                serial_write("  ");
                serial_write_hex(class_code);
                serial_write(":");
                serial_write_hex(sub_class);
                serial_write(":");
                serial_write_hex(prog_if);
                serial_write("\n");

                uint8_t header_type = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_HEADER);
                if ((header_type & 0x80) == 0) break;
            }
        }
    }

    (void)pci_rescan_hotplug();
    serial_write("=====================================\n");
}
