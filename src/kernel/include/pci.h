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
#define PCI_OFFSET_INTPIN    0x3D
#define PCI_OFFSET_COMMAND   0x04
#define PCI_OFFSET_STATUS    0x06
#define PCI_OFFSET_BIST      0x0F
#define PCI_OFFSET_CAP_PTR   0x34
#define PCI_OFFSET_SEC_BUS   0x19  /* PCI-to-PCI bridge: secondary bus number */

/* Command register bits */
#define PCI_CMD_IO_SPACE     0x0001u
#define PCI_CMD_MEM_SPACE    0x0002u
#define PCI_CMD_BUS_MASTER   0x0004u

/* Header type */
#define PCI_HEADER_MULTIFUNC 0x80u
#define PCI_HEADER_TYPE_MASK 0x7Fu
#define PCI_HEADER_GENERAL   0x00u
#define PCI_HEADER_BRIDGE    0x01u

/* Class codes (常用) */
#define PCI_CLASS_STORAGE    0x01u
#define PCI_CLASS_NETWORK    0x02u
#define PCI_CLASS_DISPLAY    0x03u
#define PCI_CLASS_MULTIMEDIA 0x04u
#define PCI_CLASS_BRIDGE     0x06u
#define PCI_CLASS_SERIAL_BUS 0x0Cu

/* Subclass (storage) */
#define PCI_SUBCLASS_IDE     0x01u
#define PCI_SUBCLASS_SATA    0x06u  /* AHCI */
#define PCI_SUBCLASS_NVME    0x08u
/* Subclass (serial bus) */
#define PCI_SUBCLASS_USB     0x03u

#define PCI_MAX_DEVICES      64u

/* BAR 描述 */
typedef struct pci_bar {
    uint64_t base;      /* 解析后的基址（IO 端口或 MMIO 物理地址） */
    uint64_t size;      /* 区域大小（字节），0 表示未使用 */
    uint8_t  is_mmio;   /* 1=MMIO, 0=IO 端口 */
    uint8_t  is_64bit;  /* 1=64 位 BAR（占用两个槽） */
    uint8_t  prefetch;  /* 可预取 */
} pci_bar_t;

/* 单个 PCI 设备记录 */
typedef struct pci_device {
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint8_t  header_type;
    uint8_t  irq_line;
    uint8_t  irq_pin;
    pci_bar_t bars[6];
} pci_device_t;

uint32_t pci_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val);
void pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint16_t val);
void pci_write8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint8_t val);

/* 使能设备的总线主控（DMA）与内存/IO 空间访问 */
void pci_enable_bus_master(pci_device_t *d);
void pci_enable_mmio(pci_device_t *d);
void pci_enable_io(pci_device_t *d);

void pci_scan_all(void);
int pci_rescan_hotplug(void);
uint32_t pci_known_device_count(void);

/* 枚举结果访问 */
const pci_device_t *pci_get_device(uint32_t index);
/* 按 class/subclass 查找，subclass 传 0xFF 表示不限；返回匹配设备或 NULL */
const pci_device_t *pci_find_by_class(uint8_t class_code, uint8_t subclass);
/* 按 vendor/device 精确查找 */
const pci_device_t *pci_find_by_id(uint16_t vendor_id, uint16_t device_id);

/* 打印设备清单到串口/终端（lspci 风格） */
void pci_dump_devices(void);

#endif
