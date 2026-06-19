#ifndef PCI_H
#define PCI_H

#include <types.h>

#define PCI_CONFIG_ADDR  0xCF8u
#define PCI_CONFIG_DATA  0xCFCu
#define PCI_VENDOR_INVALID 0xFFFFu

#define PCI_OFFSET_VENDOR    0x00
#define PCI_OFFSET_DEVICE    0x02
#define PCI_OFFSET_REVISION  0x08
#define PCI_OFFSET_PROGIF    0x09
#define PCI_OFFSET_SUBCLASS  0x0A
#define PCI_OFFSET_CLASS     0x0B
#define PCI_OFFSET_HEADER    0x0E
#define PCI_OFFSET_BAR0      0x10
#define PCI_OFFSET_BAR1      0x14
#define PCI_OFFSET_BAR2      0x18
#define PCI_OFFSET_BAR3      0x1C
#define PCI_OFFSET_BAR4      0x20
#define PCI_OFFSET_BAR5      0x24
#define PCI_OFFSET_INTLINE   0x3C

uint32_t pci_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val);
void pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint16_t val);
void pci_write8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint8_t val);

void pci_scan_all(void);
int pci_rescan_hotplug(void);
uint32_t pci_known_device_count(void);

#endif
