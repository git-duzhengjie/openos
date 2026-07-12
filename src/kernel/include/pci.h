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
#define PCI_CMD_INTX_DISABLE 0x0400u  /* 屏蔽 legacy INTx（使用 MSI 时置位） */

/* Status register bits */
#define PCI_STATUS_CAP_LIST  0x0010u  /* bit4: 支持 Capabilities List */

/* Capability IDs */
#define PCI_CAP_ID_MSI       0x05u
#define PCI_CAP_ID_MSIX      0x11u

/* MSI Capability register offsets（相对 cap 起始） */
#define PCI_MSI_CTRL         0x02u  /* Message Control (16-bit) */
#define PCI_MSI_ADDR_LO      0x04u  /* Message Address (low 32) */
#define PCI_MSI_ADDR_HI      0x08u  /* Message Address high（仅 64-bit capable） */
#define PCI_MSI_DATA_32      0x08u  /* Message Data（32-bit capable 时） */
#define PCI_MSI_DATA_64      0x0Cu  /* Message Data（64-bit capable 时） */

/* MSI Message Control bits */
#define PCI_MSI_CTRL_ENABLE  0x0001u  /* bit0: MSI Enable */
#define PCI_MSI_CTRL_64BIT   0x0080u  /* bit7: 64-bit Address capable */

/* MSI Message Address 固定基址（x86 LAPIC，bits[19:12]=dest APIC ID） */
#define PCI_MSI_ADDR_BASE    0xFEE00000u

/* ---- MSI-X Capability ---- */
#define PCI_MSIX_CTRL        0x02u  /* Message Control (16-bit) */
#define PCI_MSIX_TABLE       0x04u  /* Table Offset / BIR (32-bit) */
#define PCI_MSIX_PBA         0x08u  /* PBA Offset / BIR (32-bit) */
#define PCI_MSIX_CTRL_ENABLE 0x8000u/* bit15: MSI-X Enable */
#define PCI_MSIX_CTRL_MASK   0x4000u/* bit14: Function Mask */
#define PCI_MSIX_CTRL_TSIZE  0x07FFu/* bits[10:0]: Table Size minus 1 */
#define PCI_MSIX_BIR_MASK    0x7u   /* Table/PBA 低 3 bit = BAR index (BIR) */
/* MSI-X table entry（16B）字段偏移 */
#define PCI_MSIX_ENT_ADDR_LO 0x0u
#define PCI_MSIX_ENT_ADDR_HI 0x4u
#define PCI_MSIX_ENT_DATA    0x8u
#define PCI_MSIX_ENT_VCTRL   0xCu   /* Vector Control: bit0 = Mask */

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

/* 在 capability 链中查找指定 cap_id，返回其在配置空间的偏移（0=未找到） */
uint8_t pci_find_capability(pci_device_t *d, uint8_t cap_id);

/* 为设备配置 MSI，路由到指定中断向量（apic_id=目标 LAPIC ID，一般为 0）。
 * 成功返回 1（已使能 MSI 并屏蔽 INTx），设备不支持 MSI 返回 0。 */
int pci_msi_enable(pci_device_t *d, uint8_t vector, uint8_t apic_id);

/* 为设备配置 MSI-X，将 table entry 0 路由到指定中断向量。
 * 需设备 BAR 已解析（bars[BIR].base 为有效 MMIO 物理地址）。
 * 成功返回 1（已使能 MSI-X、解除 entry0/Function 屏蔽、屏蔽 INTx），否则 0。
 * mmio_map: 将 MSI-X table 物理地址映射为可访问虚拟地址的回调（可传 0 表示直映射）。 */
int pci_msix_enable(pci_device_t *d, uint8_t vector, uint8_t apic_id);

void pci_scan_all(void);
int pci_rescan_hotplug(void);
uint32_t pci_known_device_count(void);

/* 枚举结果访问 */
const pci_device_t *pci_get_device(uint32_t index);
/* 按 class/subclass 查找，subclass 传 0xFF 表示不限；返回匹配设备或 NULL */
const pci_device_t *pci_find_by_class(uint8_t class_code, uint8_t subclass);
/* 按 vendor/device 精确查找 */
const pci_device_t *pci_find_by_id(uint16_t vendor_id, uint16_t device_id);
/* 按 index 返回第 n 个匹配 (vendor,device) 的设备；用于同类多设备枚举 */
const pci_device_t *pci_find_nth_by_id(uint16_t vendor_id, uint16_t device_id, uint32_t index);

/* 打印设备清单到串口/终端（lspci 风格） */
void pci_dump_devices(void);

#endif
