/* ============================================================
 * openos - Linear Framebuffer Driver
 * Backend: Bochs/QEMU BGA
 * ============================================================ */

#include "framebuffer.h"
#include "io.h"
#include "vmm.h"
#include "serial.h"

#define BGA_INDEX_PORT          0x01CEu
#define BGA_DATA_PORT           0x01CFu

#define BGA_INDEX_ID            0x00u
#define BGA_INDEX_XRES          0x01u
#define BGA_INDEX_YRES          0x02u
#define BGA_INDEX_BPP           0x03u
#define BGA_INDEX_ENABLE        0x04u
#define BGA_INDEX_BANK          0x05u
#define BGA_INDEX_VIRT_WIDTH    0x06u
#define BGA_INDEX_VIRT_HEIGHT   0x07u
#define BGA_INDEX_X_OFFSET      0x08u
#define BGA_INDEX_Y_OFFSET      0x09u

#define BGA_ENABLED             0x0001u
#define BGA_LFB_ENABLED         0x0040u
#define BGA_NOCLEARMEM          0x0080u

#define BGA_ID0                 0xB0C0u
#define BGA_ID1                 0xB0C1u
#define BGA_ID2                 0xB0C2u
#define BGA_ID3                 0xB0C3u
#define BGA_ID4                 0xB0C4u
#define BGA_ID5                 0xB0C5u

/* QEMU/Bochs BGA linear framebuffer 回退物理地址；优先使用 PCI BAR 自动发现。 */
#define BGA_LFB_PHYS_FALLBACK   0xE0000000u
#define FB_VIRT_BASE            0xF0000000u

#ifndef FRAMEBUFFER_DEBUG_LOG
#define FRAMEBUFFER_DEBUG_LOG   0
#endif

#define PCI_CONFIG_ADDRESS      0x0CF8u
#define PCI_CONFIG_DATA         0x0CFCu
#define PCI_INVALID_VENDOR      0xFFFFu
#define PCI_CLASS_DISPLAY       0x03u
#define PCI_BAR_MEM_MASK        0xFFFFFFF0u

static framebuffer_info_t g_fb_info;
static framebuffer_driver_t g_bga_driver;
static framebuffer_driver_t *g_active_driver = 0;

static void fb_serial_write_dec(uint32_t value) {
    char buf[11];
    int i = 0;

    if (value == 0) {
        serial_write("0");
        return;
    }

    while (value > 0 && i < 10) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        char ch[2];
        ch[0] = buf[--i];
        ch[1] = '\0';
        serial_write(ch);
    }
}

static uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t address;

    address = 0x80000000u |
              ((uint32_t)bus << 16) |
              ((uint32_t)dev << 11) |
              ((uint32_t)func << 8) |
              ((uint32_t)offset & 0xFCu);

    outl((uint16_t)PCI_CONFIG_ADDRESS, address);
    return inl((uint16_t)PCI_CONFIG_DATA);
}

static uint16_t pci_vendor_id(uint8_t bus, uint8_t dev, uint8_t func) {
    return (uint16_t)(pci_config_read32(bus, dev, func, 0x00) & 0xFFFFu);
}

static int pci_is_multifunction(uint8_t bus, uint8_t dev) {
    uint32_t v = pci_config_read32(bus, dev, 0, 0x0C);
    return ((v >> 16) & 0x80u) != 0;
}

static uint32_t pci_find_display_lfb(void) {
    uint32_t best_bar = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint8_t max_func = pci_is_multifunction((uint8_t)bus, dev) ? 8 : 1;

            for (uint8_t func = 0; func < max_func; func++) {
                uint32_t class_reg;
                uint8_t class_code;
#if FRAMEBUFFER_DEBUG_LOG
                uint32_t id;
                uint8_t subclass;
#endif

                if (pci_vendor_id((uint8_t)bus, dev, func) == PCI_INVALID_VENDOR) {
                    continue;
                }

#if FRAMEBUFFER_DEBUG_LOG
                id = pci_config_read32((uint8_t)bus, dev, func, 0x00);
#endif
                class_reg = pci_config_read32((uint8_t)bus, dev, func, 0x08);
                class_code = (uint8_t)((class_reg >> 24) & 0xFFu);
#if FRAMEBUFFER_DEBUG_LOG
                subclass = (uint8_t)((class_reg >> 16) & 0xFFu);
#endif

                if (class_code != PCI_CLASS_DISPLAY) {
                    continue;
                }

                for (uint8_t bar_index = 0; bar_index < 6; bar_index++) {
                    uint32_t bar = pci_config_read32((uint8_t)bus, dev, func, (uint8_t)(0x10 + bar_index * 4));
                    uint32_t addr;

                    if (bar == 0 || bar == 0xFFFFFFFFu) {
                        continue;
                    }

                    if (bar & 0x1u) {
                        continue; /* I/O BAR */
                    }

                    addr = bar & PCI_BAR_MEM_MASK;
                    if (addr == 0) {
                        continue;
                    }

                    /* QEMU stdvga/bochs 通常是 vendor=1234, device=1111，BAR0 是 LFB。 */
                    best_bar = addr;

#if FRAMEBUFFER_DEBUG_LOG
                    serial_write("[FB] PCI display bus=");
                    fb_serial_write_dec(bus);
                    serial_write(" dev=");
                    fb_serial_write_dec(dev);
                    serial_write(" func=");
                    fb_serial_write_dec(func);
                    serial_write(" class=");
                    serial_write_hex(class_code);
                    serial_write(" subclass=");
                    serial_write_hex(subclass);
                    serial_write(" vendor=");
                    serial_write_hex((uint16_t)(id & 0xFFFFu));
                    serial_write(" device=");
                    serial_write_hex((uint16_t)((id >> 16) & 0xFFFFu));
                    serial_write(" bar");
                    fb_serial_write_dec(bar_index);
                    serial_write("=");
                    serial_write_hex(best_bar);
                    serial_write("\n");
#endif

                    return best_bar;
                }
            }
        }
    }

#if FRAMEBUFFER_DEBUG_LOG
    serial_write("[FB] PCI display LFB not found, fallback=");
    serial_write_hex(BGA_LFB_PHYS_FALLBACK);
    serial_write("\n");
#endif
    return BGA_LFB_PHYS_FALLBACK;
}

static uint16_t bga_read(uint16_t index) {
    outw(BGA_INDEX_PORT, index);
    return inw(BGA_DATA_PORT);
}

static void bga_write(uint16_t index, uint16_t value) {
    outw(BGA_INDEX_PORT, index);
    outw(BGA_DATA_PORT, value);
}

static int bga_valid_id(uint16_t id) {
    return id == BGA_ID0 || id == BGA_ID1 || id == BGA_ID2 ||
           id == BGA_ID3 || id == BGA_ID4 || id == BGA_ID5;
}

static int bga_probe(framebuffer_driver_t *drv) {
    (void)drv;
    uint16_t id = bga_read(BGA_INDEX_ID);
    if (!bga_valid_id(id)) {
        serial_write("[FB] Bochs/QEMU BGA not found, id=");
        serial_write_hex(id);
        serial_write("\n");
        return -1;
    }

    g_fb_info.available = 1;
    g_fb_info.driver_name = "bochs-bga";
#if FRAMEBUFFER_DEBUG_LOG
    serial_write("[FB] Bochs/QEMU BGA found, id=");
    serial_write_hex(id);
    serial_write("\n");
#endif
    return 0;
}

static int bga_set_mode(framebuffer_driver_t *drv, uint32_t width, uint32_t height, uint32_t bpp) {
    (void)drv;

    if (!g_fb_info.available) {
        return -1;
    }
    if (width == 0 || height == 0 || bpp != 32) {
        serial_write("[FB] unsupported mode\n");
        return -1;
    }
    if (width > 4096 || height > 2160) {
        serial_write("[FB] mode too large\n");
        return -1;
    }

    uint32_t bytes_per_pixel = bpp / 8;
    uint32_t pitch = width * bytes_per_pixel;
    uint32_t size = pitch * height;
    uint32_t lfb_phys = pci_find_display_lfb();

    /* 先禁用，再配置模式，最后启用 LFB。 */
    bga_write(BGA_INDEX_ENABLE, 0);
    bga_write(BGA_INDEX_XRES, (uint16_t)width);
    bga_write(BGA_INDEX_YRES, (uint16_t)height);
    bga_write(BGA_INDEX_BPP, (uint16_t)bpp);
    bga_write(BGA_INDEX_VIRT_WIDTH, (uint16_t)width);
    bga_write(BGA_INDEX_VIRT_HEIGHT, (uint16_t)height);
    bga_write(BGA_INDEX_X_OFFSET, 0);
    bga_write(BGA_INDEX_Y_OFFSET, 0);
    bga_write(BGA_INDEX_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED);

    vmm_map_range(FB_VIRT_BASE, lfb_phys, size, PTE_PRESENT | PTE_RW);

    g_fb_info.width = width;
    g_fb_info.height = height;
    g_fb_info.pitch = pitch;
    g_fb_info.bpp = bpp;
    g_fb_info.bytes_per_pixel = bytes_per_pixel;
    g_fb_info.phys_addr = lfb_phys;
    g_fb_info.virt_addr = FB_VIRT_BASE;
    g_fb_info.size = size;
    g_fb_info.mode_set = 1;
    g_fb_info.driver_name = "bochs-bga";

#if FRAMEBUFFER_DEBUG_LOG
    serial_write("[FB] mode set: ");
    fb_serial_write_dec(width);
    serial_write("x");
    fb_serial_write_dec(height);
    serial_write("x");
    fb_serial_write_dec(bpp);
    serial_write(", lfb=");
    serial_write_hex(lfb_phys);
    serial_write(" virt=");
    serial_write_hex(FB_VIRT_BASE);
    serial_write("\n");
#endif

    return 0;
}

static void bga_put_pixel(framebuffer_driver_t *drv, uint32_t x, uint32_t y, uint32_t color) {
    (void)drv;
    if (!g_fb_info.mode_set || x >= g_fb_info.width || y >= g_fb_info.height) {
        return;
    }

    volatile uint32_t *pixel = (volatile uint32_t *)(g_fb_info.virt_addr + y * g_fb_info.pitch + x * 4u);
    *pixel = color;
}

static void bga_clear(framebuffer_driver_t *drv, uint32_t color) {
    (void)drv;
    if (!g_fb_info.mode_set) {
        return;
    }

    volatile uint32_t *fb = (volatile uint32_t *)g_fb_info.virt_addr;
    uint32_t pixels = g_fb_info.width * g_fb_info.height;
    for (uint32_t i = 0; i < pixels; i++) {
        fb[i] = color;
    }
}

static void bga_fill_rect(framebuffer_driver_t *drv, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    (void)drv;
    if (!g_fb_info.mode_set || w == 0 || h == 0) {
        return;
    }
    if (x >= g_fb_info.width || y >= g_fb_info.height) {
        return;
    }
    if (x + w > g_fb_info.width) {
        w = g_fb_info.width - x;
    }
    if (y + h > g_fb_info.height) {
        h = g_fb_info.height - y;
    }

    for (uint32_t yy = 0; yy < h; yy++) {
        volatile uint32_t *row = (volatile uint32_t *)(g_fb_info.virt_addr + (y + yy) * g_fb_info.pitch + x * 4u);
        for (uint32_t xx = 0; xx < w; xx++) {
            row[xx] = color;
        }
    }
}

static int abs_i(int v) {
    return v < 0 ? -v : v;
}

static void bga_draw_line(framebuffer_driver_t *drv, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs_i(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs_i(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        if (x0 >= 0 && y0 >= 0) {
            bga_put_pixel(drv, (uint32_t)x0, (uint32_t)y0, color);
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void framebuffer_refresh_driver(void) {
    g_bga_driver.name = "bochs-bga";
    g_bga_driver.probe = bga_probe;
    g_bga_driver.set_mode = bga_set_mode;
    g_bga_driver.clear = bga_clear;
    g_bga_driver.put_pixel = bga_put_pixel;
    g_bga_driver.fill_rect = bga_fill_rect;
    g_bga_driver.draw_line = bga_draw_line;
    g_bga_driver.state = 0;
}

void framebuffer_init(void) {
    framebuffer_refresh_driver();
    g_active_driver = 0;
    g_fb_info.available = 0;
    g_fb_info.mode_set = 0;
    g_fb_info.driver_name = 0;

    if (bga_probe(&g_bga_driver) == 0) {
        g_active_driver = &g_bga_driver;
        serial_write("[OK] framebuffer driver ready\n");
        return;
    }

    serial_write("[WARN] no framebuffer driver available\n");
}

int framebuffer_is_available(void) {
    return g_active_driver != 0 && g_fb_info.available;
}

int framebuffer_set_mode(uint32_t width, uint32_t height, uint32_t bpp) {
    if (!g_active_driver || !g_fb_info.available) {
        return -1;
    }
    return bga_set_mode(&g_bga_driver, width, height, bpp);
}

const framebuffer_info_t *framebuffer_get_info(void) {
    return &g_fb_info;
}

void framebuffer_clear(uint32_t color) {
    if (g_active_driver && g_fb_info.available) {
        bga_clear(&g_bga_driver, color);
    }
}

void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (g_active_driver && g_fb_info.available) {
        bga_put_pixel(&g_bga_driver, x, y, color);
    }
}

void framebuffer_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (g_active_driver && g_fb_info.available) {
        bga_fill_rect(&g_bga_driver, x, y, w, h, color);
    }
}

void framebuffer_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    if (g_active_driver && g_fb_info.available) {
        bga_draw_line(&g_bga_driver, x0, y0, x1, y1, color);
    }
}

void framebuffer_test_pattern(void) {
    if (!framebuffer_is_available()) {
        serial_write("[FB] test failed: no driver\n");
        return;
    }
    if (!g_fb_info.mode_set) {
        if (framebuffer_set_mode(FB_DEFAULT_WIDTH, FB_DEFAULT_HEIGHT, FB_DEFAULT_BPP) < 0) {
            serial_write("[FB] test failed: set mode\n");
            return;
        }
    }

    framebuffer_clear(FB_COLOR_BLACK);

    uint32_t stripe_w = g_fb_info.width / 8u;
    uint32_t colors[8] = {
        FB_COLOR_RED, FB_COLOR_GREEN, FB_COLOR_BLUE, FB_COLOR_CYAN,
        FB_COLOR_MAGENTA, FB_COLOR_YELLOW, FB_COLOR_WHITE, FB_COLOR_GRAY
    };

    for (uint32_t i = 0; i < 8; i++) {
        framebuffer_fill_rect(i * stripe_w, 0, stripe_w, g_fb_info.height / 2u, colors[i]);
    }

    framebuffer_fill_rect(40, g_fb_info.height / 2u + 40u, 220, 120, FB_COLOR_RED);
    framebuffer_fill_rect(300, g_fb_info.height / 2u + 40u, 220, 120, FB_COLOR_GREEN);
    framebuffer_fill_rect(560, g_fb_info.height / 2u + 40u, 220, 120, FB_COLOR_BLUE);

    framebuffer_draw_line(0, 0, (int)g_fb_info.width - 1, (int)g_fb_info.height - 1, FB_COLOR_WHITE);
    framebuffer_draw_line((int)g_fb_info.width - 1, 0, 0, (int)g_fb_info.height - 1, FB_COLOR_WHITE);
    framebuffer_draw_line(0, (int)g_fb_info.height / 2, (int)g_fb_info.width - 1, (int)g_fb_info.height / 2, FB_COLOR_WHITE);
    framebuffer_draw_line((int)g_fb_info.width / 2, 0, (int)g_fb_info.width / 2, (int)g_fb_info.height - 1, FB_COLOR_WHITE);

    serial_write("[FB] test pattern drawn\n");
}

void framebuffer_print_info(void) {
    serial_write("[FB] driver=");
    serial_write(g_fb_info.driver_name ? g_fb_info.driver_name : "none");
    serial_write(" available=");
    fb_serial_write_dec((uint32_t)g_fb_info.available);
    serial_write(" mode=");
    fb_serial_write_dec((uint32_t)g_fb_info.mode_set);
    serial_write(" width=");
    fb_serial_write_dec(g_fb_info.width);
    serial_write(" height=");
    fb_serial_write_dec(g_fb_info.height);
    serial_write(" pitch=");
    fb_serial_write_dec(g_fb_info.pitch);
    serial_write(" bpp=");
    fb_serial_write_dec(g_fb_info.bpp);
    serial_write(" phys=");
    serial_write_hex(g_fb_info.phys_addr);
    serial_write(" virt=");
    serial_write_hex(g_fb_info.virt_addr);
    serial_write("\n");
}
