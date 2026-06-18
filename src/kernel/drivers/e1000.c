/* ============================================================
 * openos - Intel e1000 PCI network driver
 * ============================================================ */
#include "../include/e1000.h"
#include "../include/idt.h"
#include "../include/pci.h"
#include "../include/serial.h"
#include "../include/string.h"
#include "../net/net.h"

#define E1000_MAX_DEVICES 8u
#define E1000_RX_DESC_COUNT 32u
#define E1000_TX_DESC_COUNT 32u
#define E1000_RX_BUF_SIZE 2048u
#define E1000_WAIT_LIMIT 1000000u

#define PCI_CLASS_NETWORK      0x02u
#define PCI_SUBCLASS_ETHERNET  0x00u
#define PCI_COMMAND_REG        0x04u
#define PCI_COMMAND_IO         0x0001u
#define PCI_COMMAND_MEM        0x0002u
#define PCI_COMMAND_BUSMASTER  0x0004u

#define INTEL_VENDOR_ID        0x8086u
#define E1000_DEV_82540EM      0x100Eu
#define E1000_DEV_82545EM      0x100Fu
#define E1000_DEV_82546EB      0x1010u
#define E1000_DEV_82541PI      0x107Cu
#define E1000_DEV_82572EI      0x107Du
#define E1000_DEV_82573L       0x109Au
#define E1000_DEV_82574L       0x10D3u
#define E1000_DEV_82576        0x10C9u

#define E1000_REG_CTRL         0x0000u
#define E1000_REG_STATUS       0x0008u
#define E1000_REG_EERD         0x0014u
#define E1000_REG_ICR          0x00C0u
#define E1000_REG_IMS          0x00D0u
#define E1000_REG_RCTL         0x0100u
#define E1000_REG_TCTL         0x0400u
#define E1000_REG_TIPG         0x0410u
#define E1000_REG_RDBAL        0x2800u
#define E1000_REG_RDBAH        0x2804u
#define E1000_REG_RDLEN        0x2808u
#define E1000_REG_RDH          0x2810u
#define E1000_REG_RDT          0x2818u
#define E1000_REG_TDBAL        0x3800u
#define E1000_REG_TDBAH        0x3804u
#define E1000_REG_TDLEN        0x3808u
#define E1000_REG_TDH          0x3810u
#define E1000_REG_TDT          0x3818u
#define E1000_REG_RAL          0x5400u
#define E1000_REG_RAH          0x5404u

#define E1000_CTRL_SLU         0x00000040u
#define E1000_RCTL_EN          0x00000002u
#define E1000_RCTL_SBP         0x00000004u
#define E1000_RCTL_UPE         0x00000008u
#define E1000_RCTL_MPE         0x00000010u
#define E1000_RCTL_BAM         0x00008000u
#define E1000_RCTL_SECRC       0x04000000u
#define E1000_RCTL_BSIZE_2048  0x00000000u
#define E1000_TCTL_EN          0x00000002u
#define E1000_TCTL_PSP         0x00000008u
#define E1000_TCTL_CT_SHIFT    4u
#define E1000_TCTL_COLD_SHIFT  12u
#define E1000_IMS_RXT0         0x00000080u
#define E1000_IMS_RXDMT0       0x00000010u
#define E1000_IMS_TXDW         0x00000001u

#define E1000_RXD_STAT_DD      0x01u
#define E1000_TXD_CMD_EOP      0x01u
#define E1000_TXD_CMD_IFCS     0x02u
#define E1000_TXD_CMD_RS       0x08u
#define E1000_TXD_STAT_DD      0x01u

typedef struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

typedef struct e1000_device {
    uint8_t present;
    uint8_t initialized;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint8_t irq;
    uint16_t device_id;
    uint32_t mmio_base;
    uint32_t io_base;
    uint32_t rx_tail;
    uint32_t tx_tail;
    char name[16];
    net_device_t netdev;
    volatile uint32_t *mmio;
    e1000_rx_desc_t *rx_desc;
    e1000_tx_desc_t *tx_desc;
    uint8_t (*rx_bufs)[E1000_RX_BUF_SIZE];
    uint8_t (*tx_bufs)[NET_FRAME_MAX_SIZE];
} e1000_device_t;

static e1000_device_t e1000_devices[E1000_MAX_DEVICES];
static uint32_t e1000_count;
static e1000_rx_desc_t e1000_rx_descs[E1000_MAX_DEVICES][E1000_RX_DESC_COUNT] __attribute__((aligned(16)));
static e1000_tx_desc_t e1000_tx_descs[E1000_MAX_DEVICES][E1000_TX_DESC_COUNT] __attribute__((aligned(16)));
static uint8_t e1000_rx_buffers[E1000_MAX_DEVICES][E1000_RX_DESC_COUNT][E1000_RX_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t e1000_tx_buffers[E1000_MAX_DEVICES][E1000_TX_DESC_COUNT][NET_FRAME_MAX_SIZE] __attribute__((aligned(16)));

static uint32_t e1000_ptr32(const void *ptr) {
    return (uint32_t)(uintptr_t)ptr;
}

static uint32_t e1000_read(e1000_device_t *edev, uint32_t reg) {
    return edev->mmio[reg / 4u];
}

static void e1000_write(e1000_device_t *edev, uint32_t reg, uint32_t value) {
    edev->mmio[reg / 4u] = value;
}

static void e1000_flush(e1000_device_t *edev) {
    (void)e1000_read(edev, E1000_REG_STATUS);
}

static int e1000_transmit(net_device_t *dev, const uint8_t *frame, uint16_t len) {
    e1000_device_t *edev = dev ? (e1000_device_t *)dev->driver_data : 0;
    e1000_tx_desc_t *desc;
    uint32_t tail;
    uint32_t wait;

    if (!edev || !edev->initialized || !frame || len == 0u || len > NET_FRAME_MAX_SIZE) {
        if (dev) dev->tx_dropped++;
        return -1;
    }

    tail = edev->tx_tail;
    desc = &edev->tx_desc[tail];
    if ((desc->status & E1000_TXD_STAT_DD) == 0u) {
        dev->tx_dropped++;
        return -1;
    }

    memcpy(edev->tx_bufs[tail], frame, len);
    desc->addr = e1000_ptr32(edev->tx_bufs[tail]);
    desc->length = len;
    desc->cso = 0u;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    desc->status = 0u;
    desc->css = 0u;
    desc->special = 0u;

    tail = (tail + 1u) % E1000_TX_DESC_COUNT;
    edev->tx_tail = tail;
    e1000_write(edev, E1000_REG_TDT, tail);
    e1000_flush(edev);

    for (wait = 0; wait < E1000_WAIT_LIMIT; wait++) {
        if ((desc->status & E1000_TXD_STAT_DD) != 0u) {
            dev->tx_packets++;
            return 0;
        }
    }

    dev->tx_dropped++;
    return -1;
}

uint32_t e1000_device_count(void) {
    return e1000_count;
}

static void e1000_make_name(char *out, uint32_t index) {
    out[0] = 'e'; out[1] = '1'; out[2] = '0'; out[3] = '0'; out[4] = '0';
    out[5] = (char)('0' + (char)(index % 10u));
    out[6] = '\0';
}

static uint32_t e1000_bar_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, int want_io) {
    uint32_t bar = pci_read32(bus, dev, func, off);
    if (bar == 0u || bar == 0xFFFFFFFFu) return 0u;
    if ((bar & 0x1u) != 0u) return want_io ? (bar & 0xFFFFFFFCu) : 0u;
    return want_io ? 0u : (bar & 0xFFFFFFF0u);
}

static int e1000_supported_device(uint16_t vendor, uint16_t device,
                                  uint8_t class_code, uint8_t subclass) {
    if (vendor != INTEL_VENDOR_ID || class_code != PCI_CLASS_NETWORK || subclass != PCI_SUBCLASS_ETHERNET) return 0;
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

static void e1000_assign_fallback_mac(e1000_device_t *edev, uint32_t index) {
    edev->netdev.mac[0] = 0x02;
    edev->netdev.mac[1] = 0xe1;
    edev->netdev.mac[2] = 0x00;
    edev->netdev.mac[3] = 0x00;
    edev->netdev.mac[4] = 0x00;
    edev->netdev.mac[5] = (uint8_t)(0x20u + (index & 0x0Fu));
}

static void e1000_read_mac(e1000_device_t *edev) {
    uint32_t ral = e1000_read(edev, E1000_REG_RAL);
    uint32_t rah = e1000_read(edev, E1000_REG_RAH);
    if ((rah & 0x80000000u) == 0u && ral == 0u) return;
    edev->netdev.mac[0] = (uint8_t)(ral & 0xFFu);
    edev->netdev.mac[1] = (uint8_t)((ral >> 8) & 0xFFu);
    edev->netdev.mac[2] = (uint8_t)((ral >> 16) & 0xFFu);
    edev->netdev.mac[3] = (uint8_t)((ral >> 24) & 0xFFu);
    edev->netdev.mac[4] = (uint8_t)(rah & 0xFFu);
    edev->netdev.mac[5] = (uint8_t)((rah >> 8) & 0xFFu);
}

static void e1000_enable_pci(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t command = pci_read16(bus, dev, func, PCI_COMMAND_REG);
    command |= (uint16_t)(PCI_COMMAND_IO | PCI_COMMAND_MEM | PCI_COMMAND_BUSMASTER);
    pci_write16(bus, dev, func, PCI_COMMAND_REG, command);
}

static void e1000_poll_rx_device(e1000_device_t *edev) {
    uint32_t next;
    uint32_t guard = E1000_RX_DESC_COUNT;
    if (!edev || !edev->initialized) return;

    next = (edev->rx_tail + 1u) % E1000_RX_DESC_COUNT;
    while (guard-- && (edev->rx_desc[next].status & E1000_RXD_STAT_DD) != 0u) {
        uint16_t len = edev->rx_desc[next].length;
        if (len > 0u && len <= NET_FRAME_MAX_SIZE) {
            net_input(&edev->netdev, edev->rx_bufs[next], len);
        } else {
            edev->netdev.rx_dropped++;
        }
        edev->rx_desc[next].status = 0u;
        edev->rx_tail = next;
        e1000_write(edev, E1000_REG_RDT, edev->rx_tail);
        next = (edev->rx_tail + 1u) % E1000_RX_DESC_COUNT;
    }
}

static void e1000_irq(registers_t *regs) {
    uint32_t i;
    (void)regs;
    for (i = 0; i < e1000_count; i++) {
        e1000_device_t *edev = &e1000_devices[i];
        uint32_t icr;
        if (!edev->present || !edev->initialized) continue;
        icr = e1000_read(edev, E1000_REG_ICR);
        if (icr != 0u) e1000_poll_rx_device(edev);
    }
}

static int e1000_hw_init(e1000_device_t *edev, uint32_t index) {
    uint32_t i;
    if (edev->mmio_base == 0u) return -1;
    edev->mmio = (volatile uint32_t *)(uintptr_t)edev->mmio_base;
    edev->rx_desc = e1000_rx_descs[index];
    edev->tx_desc = e1000_tx_descs[index];
    edev->rx_bufs = e1000_rx_buffers[index];
    edev->tx_bufs = e1000_tx_buffers[index];

    e1000_write(edev, E1000_REG_CTRL, e1000_read(edev, E1000_REG_CTRL) | E1000_CTRL_SLU);
    e1000_read_mac(edev);

    memset(edev->rx_desc, 0, sizeof(e1000_rx_descs[index]));
    memset(edev->tx_desc, 0, sizeof(e1000_tx_descs[index]));
    memset(edev->rx_bufs, 0, sizeof(e1000_rx_buffers[index]));
    memset(edev->tx_bufs, 0, sizeof(e1000_tx_buffers[index]));

    for (i = 0; i < E1000_RX_DESC_COUNT; i++) {
        edev->rx_desc[i].addr = e1000_ptr32(edev->rx_bufs[i]);
        edev->rx_desc[i].status = 0u;
    }
    for (i = 0; i < E1000_TX_DESC_COUNT; i++) {
        edev->tx_desc[i].status = E1000_TXD_STAT_DD;
    }

    e1000_write(edev, E1000_REG_RDBAL, e1000_ptr32(edev->rx_desc));
    e1000_write(edev, E1000_REG_RDBAH, 0u);
    e1000_write(edev, E1000_REG_RDLEN, sizeof(e1000_rx_descs[index]));
    e1000_write(edev, E1000_REG_RDH, 0u);
    edev->rx_tail = E1000_RX_DESC_COUNT - 1u;
    e1000_write(edev, E1000_REG_RDT, edev->rx_tail);

    e1000_write(edev, E1000_REG_TDBAL, e1000_ptr32(edev->tx_desc));
    e1000_write(edev, E1000_REG_TDBAH, 0u);
    e1000_write(edev, E1000_REG_TDLEN, sizeof(e1000_tx_descs[index]));
    e1000_write(edev, E1000_REG_TDH, 0u);
    edev->tx_tail = 0u;
    e1000_write(edev, E1000_REG_TDT, 0u);

    e1000_write(edev, E1000_REG_RCTL,
                E1000_RCTL_EN | E1000_RCTL_SBP | E1000_RCTL_UPE |
                E1000_RCTL_MPE | E1000_RCTL_BAM | E1000_RCTL_SECRC |
                E1000_RCTL_BSIZE_2048);
    e1000_write(edev, E1000_REG_TCTL,
                E1000_TCTL_EN | E1000_TCTL_PSP |
                (15u << E1000_TCTL_CT_SHIFT) |
                (64u << E1000_TCTL_COLD_SHIFT));
    e1000_write(edev, E1000_REG_TIPG, 0x0060200Au);
    e1000_write(edev, E1000_REG_IMS, E1000_IMS_RXT0 | E1000_IMS_RXDMT0 | E1000_IMS_TXDW);
    (void)e1000_read(edev, E1000_REG_ICR);
    e1000_flush(edev);
    edev->initialized = 1u;
    return 0;
}

static void e1000_register_irq(e1000_device_t *edev) {
    if (edev->irq < 16u) {
        isr_install_handler((uint8_t)(32u + edev->irq), e1000_irq);
    }
}

static void e1000_register(uint8_t bus, uint8_t dev, uint8_t func, uint16_t device) {
    e1000_device_t *edev;
    net_device_t *netdev;
    uint32_t index;

    if (e1000_count >= E1000_MAX_DEVICES) return;

    index = e1000_count;
    edev = &e1000_devices[index];
    memset(edev, 0, sizeof(*edev));
    edev->present = 1;
    edev->bus = bus;
    edev->dev = dev;
    edev->func = func;
    edev->irq = pci_read8(bus, dev, func, PCI_OFFSET_INTLINE);
    edev->device_id = device;
    e1000_enable_pci(bus, dev, func);
    edev->mmio_base = e1000_bar_addr(bus, dev, func, PCI_OFFSET_BAR0, 0);
    edev->io_base = e1000_bar_addr(bus, dev, func, PCI_OFFSET_BAR1, 1);
    if (edev->io_base == 0u) edev->io_base = e1000_bar_addr(bus, dev, func, PCI_OFFSET_BAR0, 1);
    e1000_make_name(edev->name, index);

    netdev = &edev->netdev;
    strcpy(netdev->name, edev->name);
    e1000_assign_fallback_mac(edev, index);
    netdev->ip = NET_IP4(10, 0, 2, (uint8_t)(40u + index));
    netdev->netmask = NET_IP4(255, 255, 255, 0);
    netdev->gateway = NET_IP4(10, 0, 2, 2);
    netdev->dns = NET_IP4(8, 8, 8, 8);
    netdev->config_mode = NET_CONFIG_MODE_STATIC;
    netdev->link_up = 1;
    netdev->admin_up = 1;
    netdev->transmit = e1000_transmit;
    netdev->driver_data = edev;

    if (e1000_hw_init(edev, index) == 0) {
        e1000_register_irq(edev);
    }

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
    serial_write(" irq=");
    serial_write_hex(edev->irq);
    serial_write(edev->initialized ? " hw=ready\n" : " hw=unavailable\n");

    e1000_count++;
}

void e1000_init(void) {
    uint16_t bus;
    uint16_t dev;
    uint16_t func;

    memset(e1000_devices, 0, sizeof(e1000_devices));
    memset(e1000_rx_descs, 0, sizeof(e1000_rx_descs));
    memset(e1000_tx_descs, 0, sizeof(e1000_tx_descs));
    e1000_count = 0;

    serial_write("[e1000] probing PCI devices\n");
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
                    if (e1000_supported_device(vendor, device, class_code, subclass)) {
                        e1000_register((uint8_t)bus, (uint8_t)dev, (uint8_t)func, device);
                        if (e1000_count >= E1000_MAX_DEVICES) return;
                    }
                }

                if ((pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_HEADER) & 0x80u) == 0u) break;
            }
        }
    }

    if (e1000_count == 0u) serial_write("[e1000] no device\n");
    else serial_write("[e1000] probe complete\n");
}
