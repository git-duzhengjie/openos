/* ============================================================
 * openos - QEMU USB Tablet absolute pointer driver
 *
 * 最小 UHCI + HID boot/protocol 驱动。目标设备是 QEMU -usb -device usb-tablet。
 * 该驱动只在检测到 UHCI 和可用 root-port 设备后工作；失败时保持静默禁用，
 * 不影响现有 PS/2 鼠标路径。
 * ============================================================ */

#include "usb_tablet.h"
#include "io.h"
#include "pmm.h"
#include "mouse.h"
#include "serial.h"
#include "string.h"

#ifndef USB_TABLET_DEBUG_LOG
#define USB_TABLET_DEBUG_LOG 1
#endif

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define UHCI_CMD       0x00
#define UHCI_STS       0x02
#define UHCI_INTR      0x04
#define UHCI_FRNUM     0x06
#define UHCI_FLBASEADD 0x08
#define UHCI_SOFMOD    0x0C
#define UHCI_PORTSC1   0x10
#define UHCI_PORTSC2   0x12

#define UHCI_CMD_RS        0x0001
#define UHCI_CMD_HCRESET   0x0002
#define UHCI_CMD_GRESET    0x0004
#define UHCI_CMD_CF        0x0040

#define UHCI_STS_USBINT    0x0001
#define UHCI_STS_ERROR     0x0002
#define UHCI_STS_RD        0x0004
#define UHCI_STS_HSE       0x0008
#define UHCI_STS_HCPE      0x0010
#define UHCI_STS_HCH       0x0020

#define UHCI_PORT_CCS      0x0001
#define UHCI_PORT_CSC      0x0002
#define UHCI_PORT_PE       0x0004
#define UHCI_PORT_PEC      0x0008
#define UHCI_PORT_LSDA     0x0100
#define UHCI_PORT_RESET    0x0200

#define TD_LINK_TERMINATE  0x00000001u
#define TD_CTRL_ACTIVE     0x00800000u
#define TD_CTRL_IOC        0x01000000u
#define TD_CTRL_C_ERR3     0x18000000u
#define TD_CTRL_LS         0x04000000u

#define USB_PID_OUT   0xE1
#define USB_PID_IN    0x69
#define USB_PID_SETUP 0x2D

#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_ADDRESS    0x05
#define USB_REQ_SET_CONFIG     0x09
#define USB_REQ_SET_PROTOCOL   0x0B
#define USB_REQ_SET_IDLE       0x0A

#define USB_DESC_DEVICE        0x01
#define USB_DESC_CONFIG        0x02
#define USB_DESC_HID_REPORT    0x22

#define USB_DIR_IN             0x80
#define USB_TYPE_STANDARD      0x00
#define USB_TYPE_CLASS         0x20
#define USB_RECIP_DEVICE       0x00
#define USB_RECIP_INTERFACE    0x01

#define USB_MAX_PACKET0        8
#define USB_TABLET_ADDR        2
#define USB_MAX_TDS            16
#define USB_POLL_INTERVAL      1
#define USB_TABLET_AXIS_MAX    0x7FFF

typedef struct uhci_td {
    volatile uint32_t link;
    volatile uint32_t ctrl;
    volatile uint32_t token;
    volatile uint32_t buffer;
} __attribute__((packed, aligned(16))) uhci_td_t;

typedef struct usb_setup_packet {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_packet_t;

typedef struct usb_tablet_state {
    int present;
    int ready;
    uint16_t io_base;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint8_t port;
    uint8_t address;
    uint8_t interface_num;
    uint8_t endpoint_in;
    uint8_t max_packet_in;
    uint8_t data_toggle_in;
    uint32_t polls;
    uint32_t reports;
    uint32_t errors;
    uint32_t last_status;
    uint8_t last_report[16];
} usb_tablet_state_t;

static usb_tablet_state_t g_tab;
static uint32_t *g_frame_list;
static uhci_td_t *g_tds;
static uint8_t *g_usb_buf;

static void usb_delay(uint32_t count) {
    for (volatile uint32_t i = 0; i < count; i++) {
        __asm__ volatile("pause");
    }
}

static uint32_t pci_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
           ((uint32_t)func << 8) | (off & 0xFC);
}

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, off));
    return inl(PCI_CONFIG_DATA);
}

static void pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint16_t val) {
    uint32_t shift = (off & 2) * 8;
    uint32_t old = pci_read32(bus, dev, func, off);
    uint32_t nv = (old & ~(0xFFFFu << shift)) | ((uint32_t)val << shift);
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, off));
    outl(PCI_CONFIG_DATA, nv);
}

static int uhci_find(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id = pci_read32((uint8_t)bus, dev, func, 0x00);
                if (id == 0xFFFFFFFFu) {
                    if (func == 0) break;
                    continue;
                }
                uint32_t class_reg = pci_read32((uint8_t)bus, dev, func, 0x08);
                uint8_t class_code = (uint8_t)(class_reg >> 24);
                uint8_t sub_class = (uint8_t)(class_reg >> 16);
                uint8_t prog_if = (uint8_t)(class_reg >> 8);
                if (class_code == 0x0C && sub_class == 0x03 && prog_if == 0x00) {
                    uint32_t bar4 = pci_read32((uint8_t)bus, dev, func, 0x20);
                    uint16_t io_base = (uint16_t)(bar4 & ~0x1Fu);
                    if (io_base) {
                        g_tab.bus = (uint8_t)bus;
                        g_tab.dev = dev;
                        g_tab.func = func;
                        g_tab.io_base = io_base;
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

static void td_fill(uhci_td_t *td, uint32_t link, uint32_t ctrl, uint8_t pid,
                    uint8_t addr, uint8_t endp, uint8_t toggle, uint16_t len,
                    void *buf) {
    uint32_t maxlen = (len == 0) ? 0x7FFu : ((uint32_t)(len - 1) << 21);
    td->link = link;
    td->ctrl = ctrl;
    td->token = ((uint32_t)pid) |
                ((uint32_t)addr << 8) |
                ((uint32_t)endp << 15) |
                ((uint32_t)(toggle ? 1 : 0) << 19) |
                maxlen;
    td->buffer = (uint32_t)buf;
}

static int uhci_wait_td(uhci_td_t *last_td, uint32_t timeout) {
    while (timeout--) {
        uint32_t st = last_td->ctrl;
        if (!(st & TD_CTRL_ACTIVE)) {
            g_tab.last_status = st;
            return ((st & 0x007F0000u) == 0) ? 1 : 0;
        }
        usb_delay(200);
    }
    g_tab.last_status = last_td->ctrl;
    return 0;
}

static int uhci_submit_tds(int count, uhci_td_t *wait_td) {
    if (!g_frame_list || !g_tds || count <= 0) return 0;
    outw(g_tab.io_base + UHCI_STS, 0x003F);
    g_frame_list[0] = (uint32_t)&g_tds[0];
    int ok = uhci_wait_td(wait_td, 30000);
    g_frame_list[0] = TD_LINK_TERMINATE;
    outw(g_tab.io_base + UHCI_STS, 0x003F);
    if (!ok) g_tab.errors++;
    return ok;
}

static int usb_control(uint8_t addr, uint8_t req_type, uint8_t req, uint16_t value,
                       uint16_t index, uint16_t length, void *data) {
    usb_setup_packet_t *setup = (usb_setup_packet_t *)g_usb_buf;
    uint8_t *data_buf = g_usb_buf + 64;
    int td_count = 0;
    uint8_t data_pid;
    uint8_t status_pid;

    setup->bmRequestType = req_type;
    setup->bRequest = req;
    setup->wValue = value;
    setup->wIndex = index;
    setup->wLength = length;

    if ((req_type & USB_DIR_IN) == USB_DIR_IN) {
        memset(data_buf, 0, length ? length : 1);
        data_pid = USB_PID_IN;
        status_pid = USB_PID_OUT;
    } else {
        if (length && data) memcpy(data_buf, data, length);
        data_pid = USB_PID_OUT;
        status_pid = USB_PID_IN;
    }

    td_fill(&g_tds[td_count], (uint32_t)&g_tds[td_count + 1], TD_CTRL_ACTIVE | TD_CTRL_C_ERR3,
            USB_PID_SETUP, addr, 0, 0, 8, setup);
    td_count++;

    if (length > 0) {
        uint16_t done = 0;
        uint8_t toggle = 1;
        while (done < length && td_count < (USB_MAX_TDS - 1)) {
            uint16_t chunk = length - done;
            if (chunk > USB_MAX_PACKET0) chunk = USB_MAX_PACKET0;
            td_fill(&g_tds[td_count], (uint32_t)&g_tds[td_count + 1], TD_CTRL_ACTIVE | TD_CTRL_C_ERR3,
                    data_pid, addr, 0, toggle, chunk, data_buf + done);
            td_count++;
            done += chunk;
            toggle ^= 1;
        }
        if (done != length) return 0;
    }

    td_fill(&g_tds[td_count], TD_LINK_TERMINATE, TD_CTRL_ACTIVE | TD_CTRL_C_ERR3 | TD_CTRL_IOC,
            status_pid, addr, 0, (uint8_t)((length / USB_MAX_PACKET0 + 1) & 1), 0, NULL);
    td_count++;

    for (int i = 0; i < td_count - 1; i++) g_tds[i].link = (uint32_t)&g_tds[i + 1];
    g_tds[td_count - 1].link = TD_LINK_TERMINATE;

    if (!uhci_submit_tds(td_count, &g_tds[td_count - 1])) return 0;
    if ((req_type & USB_DIR_IN) == USB_DIR_IN && length && data) memcpy(data, data_buf, length);
    return 1;
}

static int uhci_port_reset(uint8_t port_index) {
    uint16_t port_reg = g_tab.io_base + (port_index == 0 ? UHCI_PORTSC1 : UHCI_PORTSC2);
    uint16_t ps = inw(port_reg);
    if (!(ps & UHCI_PORT_CCS)) return 0;

    outw(port_reg, (ps & ~UHCI_PORT_PE) | UHCI_PORT_RESET);
    usb_delay(50000);
    ps = inw(port_reg);
    outw(port_reg, (ps & ~UHCI_PORT_RESET) | UHCI_PORT_CSC | UHCI_PORT_PEC);
    usb_delay(30000);
    ps = inw(port_reg);
    outw(port_reg, (ps | UHCI_PORT_PE | UHCI_PORT_CSC | UHCI_PORT_PEC) & ~UHCI_PORT_RESET);
    usb_delay(30000);
    ps = inw(port_reg);
    if (!(ps & UHCI_PORT_PE)) return 0;
    g_tab.port = port_index;
    return 1;
}

static int uhci_init_controller(void) {
    if (!g_frame_list || !g_tds || !g_usb_buf) return 0;

    uint16_t cmd = (uint16_t)pci_read32(g_tab.bus, g_tab.dev, g_tab.func, 0x04);
    cmd |= 0x0005; /* I/O space + bus master */
    pci_write16(g_tab.bus, g_tab.dev, g_tab.func, 0x04, cmd);

    outw(g_tab.io_base + UHCI_CMD, UHCI_CMD_HCRESET);
    usb_delay(50000);
    outw(g_tab.io_base + UHCI_INTR, 0x0000);
    outw(g_tab.io_base + UHCI_STS, 0x003F);
    outw(g_tab.io_base + UHCI_FRNUM, 0);

    for (int i = 0; i < 1024; i++) g_frame_list[i] = TD_LINK_TERMINATE;
    outl(g_tab.io_base + UHCI_FLBASEADD, (uint32_t)g_frame_list);
    outb(g_tab.io_base + UHCI_SOFMOD, 0x40);
    outw(g_tab.io_base + UHCI_CMD, UHCI_CMD_RS | UHCI_CMD_CF);
    usb_delay(50000);
    return (inw(g_tab.io_base + UHCI_STS) & UHCI_STS_HCH) ? 0 : 1;
}

static int usb_parse_config(uint8_t *cfg, uint16_t len) {
    uint16_t i = 0;
    g_tab.interface_num = 0;
    g_tab.endpoint_in = 0;
    g_tab.max_packet_in = 8;

    while (i + 2 <= len) {
        uint8_t blen = cfg[i];
        uint8_t dtype = cfg[i + 1];
        if (blen < 2 || i + blen > len) break;

        if (dtype == 4 && blen >= 9) { /* interface */
            uint8_t cls = cfg[i + 5];
            uint8_t sub = cfg[i + 6];
            uint8_t proto = cfg[i + 7];
            if (cls == 3) {
                g_tab.interface_num = cfg[i + 2];
                (void)sub;
                (void)proto;
            }
        } else if (dtype == 5 && blen >= 7) { /* endpoint */
            uint8_t ep = cfg[i + 2];
            uint8_t attr = cfg[i + 3];
            if ((ep & 0x80) && ((attr & 0x03) == 0x03)) {
                g_tab.endpoint_in = ep & 0x0F;
                g_tab.max_packet_in = cfg[i + 4];
                if (g_tab.max_packet_in == 0 || g_tab.max_packet_in > 16) g_tab.max_packet_in = 8;
                return 1;
            }
        }
        i += blen;
    }
    return g_tab.endpoint_in ? 1 : 0;
}

static int usb_enumerate_tablet(void) {
    uint8_t dev_desc[18];
    uint8_t cfg_desc[128];
    memset(dev_desc, 0, sizeof(dev_desc));
    memset(cfg_desc, 0, sizeof(cfg_desc));

    if (!usb_control(0, USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                     USB_REQ_GET_DESCRIPTOR, (USB_DESC_DEVICE << 8), 0, 8, dev_desc)) return 0;

    if (!usb_control(0, USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                     USB_REQ_SET_ADDRESS, USB_TABLET_ADDR, 0, 0, NULL)) return 0;
    usb_delay(50000);
    g_tab.address = USB_TABLET_ADDR;

    if (!usb_control(g_tab.address, USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                     USB_REQ_GET_DESCRIPTOR, (USB_DESC_DEVICE << 8), 0, sizeof(dev_desc), dev_desc)) return 0;

    if (!usb_control(g_tab.address, USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                     USB_REQ_GET_DESCRIPTOR, (USB_DESC_CONFIG << 8), 0, 9, cfg_desc)) return 0;
    uint16_t total_len = cfg_desc[2] | ((uint16_t)cfg_desc[3] << 8);
    if (total_len > sizeof(cfg_desc)) total_len = sizeof(cfg_desc);
    if (!usb_control(g_tab.address, USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                     USB_REQ_GET_DESCRIPTOR, (USB_DESC_CONFIG << 8), 0, total_len, cfg_desc)) return 0;
    if (!usb_parse_config(cfg_desc, total_len)) return 0;

    uint8_t config_value = cfg_desc[5] ? cfg_desc[5] : 1;
    if (!usb_control(g_tab.address, USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                     USB_REQ_SET_CONFIG, config_value, 0, 0, NULL)) return 0;

    /* HID: boot protocol + idle 0。部分 tablet 会忽略失败，不作为致命错误。 */
    usb_control(g_tab.address, USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                USB_REQ_SET_PROTOCOL, 0, g_tab.interface_num, 0, NULL);
    usb_control(g_tab.address, USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                USB_REQ_SET_IDLE, 0, g_tab.interface_num, 0, NULL);

    g_tab.data_toggle_in = 0;
    return 1;
}

void usb_tablet_init(void) {
    memset(&g_tab, 0, sizeof(g_tab));

    if (!uhci_find()) {
#if USB_TABLET_DEBUG_LOG
        serial_write("[USB] UHCI controller not found; usb-tablet disabled\n");
#endif
        return;
    }

    g_frame_list = (uint32_t *)pmm_alloc_page();
    g_tds = (uhci_td_t *)pmm_alloc_page();
    g_usb_buf = (uint8_t *)pmm_alloc_page();
    if (!g_frame_list || !g_tds || !g_usb_buf) {
#if USB_TABLET_DEBUG_LOG
        serial_write("[USB] DMA allocation failed; usb-tablet disabled\n");
#endif
        return;
    }
    memset(g_frame_list, 0, 4096);
    memset(g_tds, 0, 4096);
    memset(g_usb_buf, 0, 4096);

#if USB_TABLET_DEBUG_LOG
    serial_write("[USB] UHCI io=");
    serial_write_hex(g_tab.io_base);
    serial_write("\n");
#endif

    if (!uhci_init_controller()) {
#if USB_TABLET_DEBUG_LOG
        serial_write("[USB] UHCI init failed; usb-tablet disabled\n");
#endif
        return;
    }

    if (!uhci_port_reset(0) && !uhci_port_reset(1)) {
#if USB_TABLET_DEBUG_LOG
        serial_write("[USB] No device on UHCI root ports; usb-tablet disabled\n");
#endif
        return;
    }

    if (!usb_enumerate_tablet()) {
#if USB_TABLET_DEBUG_LOG
        serial_write("[USB] usb-tablet enumerate failed; disabled\n");
#endif
        return;
    }

    g_tab.present = 1;
    g_tab.ready = 1;
    serial_write("[OK] USB tablet absolute pointer\n");
}

int usb_tablet_is_ready(void) {
    return g_tab.ready;
}

void usb_tablet_poll(int screen_width, int screen_height) {
    uint8_t *report = g_usb_buf + 256;
    uint8_t len;
    int x_raw;
    int y_raw;
    int x;
    int y;
    uint8_t buttons;

    if (!g_tab.ready || screen_width <= 0 || screen_height <= 0) return;
    g_tab.polls++;

    memset(report, 0, 16);
    len = g_tab.max_packet_in;
    if (len == 0 || len > 16) len = 8;

    td_fill(&g_tds[0], TD_LINK_TERMINATE, TD_CTRL_ACTIVE | TD_CTRL_C_ERR3 | TD_CTRL_IOC,
            USB_PID_IN, g_tab.address, g_tab.endpoint_in, g_tab.data_toggle_in, len, report);

    if (!uhci_submit_tds(1, &g_tds[0])) return;
    g_tab.data_toggle_in ^= 1;

    memcpy(g_tab.last_report, report, 16);
    buttons = report[0] & 0x07;

    /* QEMU usb-tablet 常见 HID report: buttons, x16, y16, wheel... */
    x_raw = report[1] | ((int)report[2] << 8);
    y_raw = report[3] | ((int)report[4] << 8);

    if (x_raw < 0) x_raw = 0;
    if (y_raw < 0) y_raw = 0;
    if (x_raw > USB_TABLET_AXIS_MAX) x_raw = USB_TABLET_AXIS_MAX;
    if (y_raw > USB_TABLET_AXIS_MAX) y_raw = USB_TABLET_AXIS_MAX;

    x = (x_raw * (screen_width - 1)) / USB_TABLET_AXIS_MAX;
    y = (y_raw * (screen_height - 1)) / USB_TABLET_AXIS_MAX;

    mouse_set_absolute_position(x, y, buttons);
    g_tab.reports++;
}

void usb_tablet_print_info(void) {
    serial_write("[USB TABLET] ready="); serial_write_hex((uint32_t)g_tab.ready);
    serial_write(" io="); serial_write_hex((uint32_t)g_tab.io_base);
    serial_write(" addr="); serial_write_hex((uint32_t)g_tab.address);
    serial_write(" ep="); serial_write_hex((uint32_t)g_tab.endpoint_in);
    serial_write(" polls="); serial_write_hex(g_tab.polls);
    serial_write(" reports="); serial_write_hex(g_tab.reports);
    serial_write(" errors="); serial_write_hex(g_tab.errors);
    serial_write(" last="); serial_write_hex(g_tab.last_status);
    serial_write("\n");
}
