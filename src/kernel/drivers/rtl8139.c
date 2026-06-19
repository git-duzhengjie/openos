/* ============================================================
 * openos - Realtek RTL8139 PCI network driver
 * Provides real RX/TX path bridged to the OpenOS protocol stack
 * ============================================================ */
#include "../include/rtl8139.h"
#include "../include/pci.h"
#include "../include/idt.h"
#include "../include/io.h"
#include "../include/serial.h"
#include "../include/string.h"
#include "../net/net.h"

#define RTL8139_MAX_DEVICES 8u

#define PCI_CLASS_NETWORK      0x02u
#define PCI_SUBCLASS_ETHERNET  0x00u
#define PCI_COMMAND_OFFSET     0x04u
#define PCI_COMMAND_IO         0x0001u
#define PCI_COMMAND_MEMORY     0x0002u
#define PCI_COMMAND_BUSMASTER  0x0004u

#define REALTEK_VENDOR_ID      0x10ECu
#define RTL8139_DEVICE_ID      0x8139u
#define RTL8100_DEVICE_ID      0x8138u

#define RTL_REG_IDR0           0x00u
#define RTL_REG_TX_STATUS0     0x10u
#define RTL_REG_TX_ADDR0       0x20u
#define RTL_REG_RX_BUF         0x30u
#define RTL_REG_RX_BUF_PTR     0x38u
#define RTL_REG_COMMAND        0x37u
#define RTL_REG_INTR_MASK      0x3Cu
#define RTL_REG_INTR_STATUS    0x3Eu
#define RTL_REG_RX_CONFIG      0x44u
#define RTL_REG_CONFIG1        0x52u

#define RTL_CMD_BUFE           0x01u
#define RTL_CMD_TE             0x04u
#define RTL_CMD_RE             0x08u
#define RTL_CMD_RESET          0x10u

#define RTL_ISR_RX_OK          0x0001u
#define RTL_ISR_RX_ERR         0x0002u
#define RTL_ISR_TX_OK          0x0004u
#define RTL_ISR_TX_ERR         0x0008u
#define RTL_ISR_RX_OVERFLOW    0x0010u
#define RTL_ISR_RX_FIFO_OVER   0x0040u
#define RTL_ISR_SYSTEM_ERR     0x8000u
#define RTL_ISR_HANDLED        0x805Fu

#define RTL_RX_BUF_LEN         8192u
#define RTL_RX_BUF_PAD         16u
#define RTL_RX_READ_AHEAD      1500u
#define RTL_RX_TOTAL_LEN       (RTL_RX_BUF_LEN + RTL_RX_BUF_PAD + RTL_RX_READ_AHEAD)
#define RTL_TX_DESC_COUNT      4u
#define RTL_TX_BUF_LEN         2048u
#define RTL_ETH_MIN_LEN        60u
#define RTL_ETH_MAX_LEN        1518u

#define RTL_RCR_ACCEPT_ALL     0x00000001u
#define RTL_RCR_ACCEPT_PHYS    0x00000002u
#define RTL_RCR_ACCEPT_MULTI   0x00000004u
#define RTL_RCR_ACCEPT_BCAST   0x00000008u
#define RTL_RCR_WRAP           0x00000080u
#define RTL_RCR_MXDMA_UNLIMIT  0x00000700u

#define RTL_TSD_OWN            0x00002000u
#define RTL_TSD_TOK            0x00008000u

typedef struct rtl8139_device {
    uint8_t present;
    uint8_t initialized;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint8_t irq;
    uint8_t tx_index;
    uint16_t device_id;
    uint16_t rx_offset;
    uint32_t io_base;
    uint32_t mmio_base;
    char name[16];
    net_device_t netdev;
} rtl8139_device_t;

static rtl8139_device_t rtl8139_devices[RTL8139_MAX_DEVICES];
static uint32_t rtl8139_count;
static uint8_t rtl8139_rx_buffers[RTL8139_MAX_DEVICES][RTL_RX_TOTAL_LEN] __attribute__((aligned(4)));
static uint8_t rtl8139_tx_buffers[RTL8139_MAX_DEVICES][RTL_TX_DESC_COUNT][RTL_TX_BUF_LEN] __attribute__((aligned(4)));

static uint8_t rtl_inb(const rtl8139_device_t *rdev, uint16_t reg) {
    return inb((uint16_t)(rdev->io_base + reg));
}

static uint16_t rtl_inw(const rtl8139_device_t *rdev, uint16_t reg) {
    return inw((uint16_t)(rdev->io_base + reg));
}

static uint32_t rtl_inl(const rtl8139_device_t *rdev, uint16_t reg) {
    return inl((uint16_t)(rdev->io_base + reg));
}

static void rtl_outb(const rtl8139_device_t *rdev, uint16_t reg, uint8_t value) {
    outb((uint16_t)(rdev->io_base + reg), value);
}

static void rtl_outw(const rtl8139_device_t *rdev, uint16_t reg, uint16_t value) {
    outw((uint16_t)(rdev->io_base + reg), value);
}

static void rtl_outl(const rtl8139_device_t *rdev, uint16_t reg, uint32_t value) {
    outl((uint16_t)(rdev->io_base + reg), value);
}

static uint32_t rtl8139_index_of(const rtl8139_device_t *rdev) {
    return (uint32_t)(rdev - rtl8139_devices);
}

static uint32_t rtl8139_phys(const void *ptr) {
    return (uint32_t)(uintptr_t)ptr;
}

static int rtl8139_transmit(net_device_t *dev, const uint8_t *frame, uint16_t len) {
    rtl8139_device_t *rdev;
    uint32_t slot;
    uint32_t status;
    uint16_t send_len;
    uint32_t idx;

    if (!dev || !frame || len == 0u || len > RTL_ETH_MAX_LEN) {
        if (dev) dev->tx_dropped++;
        return -1;
    }

    rdev = (rtl8139_device_t *)dev->driver_data;
    if (!rdev || !rdev->initialized || rdev->io_base == 0u) {
        dev->tx_dropped++;
        return -1;
    }

    slot = rdev->tx_index % RTL_TX_DESC_COUNT;
    status = rtl_inl(rdev, (uint16_t)(RTL_REG_TX_STATUS0 + slot * 4u));
    if ((status & (RTL_TSD_OWN | RTL_TSD_TOK)) == 0u) {
        dev->tx_dropped++;
        return -1;
    }

    send_len = len < RTL_ETH_MIN_LEN ? RTL_ETH_MIN_LEN : len;
    idx = rtl8139_index_of(rdev);
    memset(rtl8139_tx_buffers[idx][slot], 0, RTL_TX_BUF_LEN);
    memcpy(rtl8139_tx_buffers[idx][slot], frame, len);

    rtl_outl(rdev, (uint16_t)(RTL_REG_TX_ADDR0 + slot * 4u), rtl8139_phys(rtl8139_tx_buffers[idx][slot]));
    rtl_outl(rdev, (uint16_t)(RTL_REG_TX_STATUS0 + slot * 4u), send_len);
    rdev->tx_index = (uint8_t)((rdev->tx_index + 1u) % RTL_TX_DESC_COUNT);
    dev->tx_packets++;
    return 0;
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

static const char *rtl8139_model_name(uint16_t device) {
    if (device == RTL8139_DEVICE_ID) {
        return "RTL8139";
    }
    if (device == RTL8100_DEVICE_ID) {
        return "RTL8100";
    }
    return "unknown";
}

static void rtl8139_read_mac(rtl8139_device_t *rdev) {
    uint8_t i;
    for (i = 0; i < NET_ETH_ADDR_LEN; i++) {
        rdev->netdev.mac[i] = rtl_inb(rdev, (uint16_t)(RTL_REG_IDR0 + i));
    }
}

static void rtl8139_fallback_mac(rtl8139_device_t *rdev, uint32_t index) {
    rdev->netdev.mac[0] = 0x02;
    rdev->netdev.mac[1] = 0x81;
    rdev->netdev.mac[2] = 0x39;
    rdev->netdev.mac[3] = 0x00;
    rdev->netdev.mac[4] = 0x00;
    rdev->netdev.mac[5] = (uint8_t)(0x30u + (index & 0x0Fu));
}

static int rtl8139_mac_valid(const uint8_t *mac) {
    uint8_t i;
    uint8_t any = 0;
    uint8_t allff = 1;
    for (i = 0; i < NET_ETH_ADDR_LEN; i++) {
        if (mac[i] != 0u) any = 1;
        if (mac[i] != 0xFFu) allff = 0;
    }
    return any && !allff;
}

static int rtl8139_enable_device(rtl8139_device_t *rdev) {
    uint16_t cmd = pci_read16(rdev->bus, rdev->dev, rdev->func, PCI_COMMAND_OFFSET);
    cmd |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_BUSMASTER;
    pci_write16(rdev->bus, rdev->dev, rdev->func, PCI_COMMAND_OFFSET, cmd);
    return 0;
}

static void rtl8139_reset_rx(rtl8139_device_t *rdev) {
    rdev->rx_offset = 0;
    rtl_outl(rdev, RTL_REG_RX_BUF, rtl8139_phys(rtl8139_rx_buffers[rtl8139_index_of(rdev)]));
    rtl_outw(rdev, RTL_REG_RX_BUF_PTR, 0u);
}

static void rtl8139_receive_one(rtl8139_device_t *rdev) {
    uint32_t idx = rtl8139_index_of(rdev);
    uint8_t *rx = rtl8139_rx_buffers[idx];
    uint16_t off = (uint16_t)(rdev->rx_offset % RTL_RX_BUF_LEN);
    uint16_t status = *(uint16_t *)(rx + off);
    uint16_t size = *(uint16_t *)(rx + off + 2u);
    uint16_t frame_len;
    uint8_t frame[RTL_ETH_MAX_LEN];
    uint16_t i;

    if ((status & 0x0001u) == 0u || size < 8u || size > (RTL_ETH_MAX_LEN + 4u)) {
        rdev->netdev.rx_dropped++;
        rtl8139_reset_rx(rdev);
        return;
    }

    frame_len = (uint16_t)(size - 4u);
    if (frame_len > RTL_ETH_MAX_LEN) {
        rdev->netdev.rx_dropped++;
        rtl8139_reset_rx(rdev);
        return;
    }

    for (i = 0; i < frame_len; i++) {
        frame[i] = rx[(off + 4u + i) % RTL_RX_BUF_LEN];
    }
    net_input(&rdev->netdev, frame, frame_len);

    rdev->rx_offset = (uint16_t)((rdev->rx_offset + size + 4u + 3u) & ~3u);
    rtl_outw(rdev, RTL_REG_RX_BUF_PTR, (uint16_t)((rdev->rx_offset - 16u) & 0x1FFFu));
}

static void rtl8139_poll_rx(rtl8139_device_t *rdev) {
    uint32_t guard = 64u;
    while (((rtl_inb(rdev, RTL_REG_COMMAND) & RTL_CMD_BUFE) == 0u) && guard--) {
        rtl8139_receive_one(rdev);
    }
}

static void rtl8139_irq(registers_t *regs) {
    uint32_t i;
    (void)regs;
    for (i = 0; i < rtl8139_count; i++) {
        rtl8139_device_t *rdev = &rtl8139_devices[i];
        uint16_t status;
        if (!rdev->present || !rdev->initialized || rdev->io_base == 0u) {
            continue;
        }
        status = rtl_inw(rdev, RTL_REG_INTR_STATUS);
        if (status == 0u || status == 0xFFFFu) {
            continue;
        }
        rtl_outw(rdev, RTL_REG_INTR_STATUS, status);
        if ((status & (RTL_ISR_RX_OK | RTL_ISR_RX_ERR | RTL_ISR_RX_OVERFLOW | RTL_ISR_RX_FIFO_OVER)) != 0u) {
            rtl8139_poll_rx(rdev);
        }
        if ((status & (RTL_ISR_RX_ERR | RTL_ISR_RX_OVERFLOW | RTL_ISR_RX_FIFO_OVER | RTL_ISR_SYSTEM_ERR)) != 0u) {
            if ((status & RTL_ISR_RX_ERR) != 0u) rdev->netdev.rx_dropped++;
            if ((status & (RTL_ISR_RX_OVERFLOW | RTL_ISR_RX_FIFO_OVER)) != 0u) rtl8139_reset_rx(rdev);
        }
        if ((status & RTL_ISR_TX_ERR) != 0u) {
            rdev->netdev.tx_dropped++;
        }
    }
}

static int rtl8139_hw_init(rtl8139_device_t *rdev) {
    uint32_t timeout;
    uint32_t idx;

    if (rdev->io_base == 0u) {
        return -1;
    }

    rtl8139_enable_device(rdev);
    rtl_outb(rdev, RTL_REG_CONFIG1, 0x00u);
    rtl_outb(rdev, RTL_REG_COMMAND, RTL_CMD_RESET);
    timeout = 100000u;
    while ((rtl_inb(rdev, RTL_REG_COMMAND) & RTL_CMD_RESET) != 0u && timeout--) {
    }
    if (timeout == 0u) {
        return -1;
    }

    rtl8139_read_mac(rdev);
    if (!rtl8139_mac_valid(rdev->netdev.mac)) {
        rtl8139_fallback_mac(rdev, rtl8139_index_of(rdev));
    }

    idx = rtl8139_index_of(rdev);
    memset(rtl8139_rx_buffers[idx], 0, sizeof(rtl8139_rx_buffers[idx]));
    memset(rtl8139_tx_buffers[idx], 0, sizeof(rtl8139_tx_buffers[idx]));
    rtl8139_reset_rx(rdev);

    rtl_outl(rdev, RTL_REG_RX_CONFIG,
             RTL_RCR_ACCEPT_PHYS | RTL_RCR_ACCEPT_BCAST |
             RTL_RCR_ACCEPT_MULTI | RTL_RCR_WRAP | RTL_RCR_MXDMA_UNLIMIT);
    rtl_outw(rdev, RTL_REG_INTR_MASK,
             RTL_ISR_RX_OK | RTL_ISR_RX_ERR | RTL_ISR_TX_OK | RTL_ISR_TX_ERR |
             RTL_ISR_RX_OVERFLOW | RTL_ISR_RX_FIFO_OVER | RTL_ISR_SYSTEM_ERR);
    rtl_outb(rdev, RTL_REG_COMMAND, RTL_CMD_RE | RTL_CMD_TE);
    rdev->initialized = 1;
    return 0;
}

static void rtl8139_register_irq(rtl8139_device_t *rdev) {
    if (rdev->irq < 16u) {
        isr_install_handler((uint8_t)(32u + rdev->irq), rtl8139_irq);
    }
}

static void rtl8139_register(uint8_t bus, uint8_t dev, uint8_t func, uint16_t device) {
    rtl8139_device_t *rdev;
    net_device_t *netdev;
    uint32_t index;

    if (rtl8139_count >= RTL8139_MAX_DEVICES) {
        return;
    }

    index = rtl8139_count;
    rdev = &rtl8139_devices[index];
    memset(rdev, 0, sizeof(*rdev));
    rdev->present = 1;
    rdev->bus = bus;
    rdev->dev = dev;
    rdev->func = func;
    rdev->irq = pci_read8(bus, dev, func, PCI_OFFSET_INTLINE);
    rdev->device_id = device;
    rdev->io_base = rtl8139_bar_addr(bus, dev, func, PCI_OFFSET_BAR0, 1);
    rdev->mmio_base = rtl8139_bar_addr(bus, dev, func, PCI_OFFSET_BAR1, 0);
    if (rdev->mmio_base == 0u) {
        rdev->mmio_base = rtl8139_bar_addr(bus, dev, func, PCI_OFFSET_BAR0, 0);
    }
    rtl8139_make_name(rdev->name, index);

    netdev = &rdev->netdev;
    strcpy(netdev->name, rdev->name);
    rtl8139_fallback_mac(rdev, index);
    netdev->ip = NET_IP4(10, 0, 2, (uint8_t)(50u + index));
    netdev->netmask = NET_IP4(255, 255, 255, 0);
    netdev->gateway = NET_IP4(10, 0, 2, 2);
    netdev->dns = NET_IP4(8, 8, 8, 8);
    netdev->config_mode = NET_CONFIG_MODE_STATIC;
    netdev->link_up = 1;
    netdev->admin_up = 1;
    netdev->transmit = rtl8139_transmit;
    netdev->driver_data = rdev;

    if (rtl8139_hw_init(rdev) == 0) {
        rtl8139_register_irq(rdev);
    }

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
    serial_write(" irq=");
    serial_write_hex(rdev->irq);
    serial_write(rdev->initialized ? " hw=ready" : " hw=unavailable");
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
                    uint8_t class_code = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_CLASS);
                    uint8_t subclass = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_SUBCLASS);
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
