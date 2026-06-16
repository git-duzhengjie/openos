/* ============================================================
 * openos - PCI bus driver
 * ============================================================ */
#include "../include/pci.h"
#include "../include/serial.h"
#include "../include/io.h"

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
                uint8_t class_code = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_CLASS + 2);
                uint8_t sub_class = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_CLASS + 1);
                uint8_t prog_if = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_CLASS + 0);

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

    serial_write("=====================================\n");
}
