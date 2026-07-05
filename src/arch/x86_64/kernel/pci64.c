/* ============================================================
 * openos x86_64 PCI 总线枚举实现（M1.1）
 *
 * 通过 0xCF8/0xCFC 配置机制枚举所有 PCI 总线/设备/功能，
 * 解析 BAR、IRQ line/pin，提供按 class / vendor 查找接口。
 * 是网卡 / AHCI / NVMe / xHCI / 声卡等后续驱动的前置基础。
 * ============================================================ */
#include <stdint.h>
#include "pci.h"
#include "io.h"

/* 日志由内核提供 */
extern void early_serial64_write(const char *s);

/* ---- 简易十六进制日志 ---- */
static void pci_log(const char *s) { early_serial64_write(s); }

static void hex_to_buf(uint64_t v, int nibbles, char *out) {
    const char *h = "0123456789ABCDEF";
    for (int i = 0; i < nibbles; i++)
        out[i] = h[(v >> ((nibbles - 1 - i) * 4)) & 0xF];
    out[nibbles] = 0;
}
static void log_hex(const char *k, uint64_t v, int nibbles) {
    char buf[17];
    hex_to_buf(v, nibbles, buf);
    pci_log(k); pci_log("0x"); pci_log(buf);
}

/* ---- 全局设备表 ---- */
static pci_device_t g_pci_devices[PCI_MAX_DEVICES];
static uint32_t     g_pci_count = 0;

/* ============================================================
 * 配置空间读写（0xCF8 地址端口 / 0xCFC 数据端口）
 * ============================================================ */
uint32_t pci_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    return (uint32_t)(((uint32_t)bus << 16) |
                      ((uint32_t)(dev & 0x1F) << 11) |
                      ((uint32_t)(func & 0x07) << 8) |
                      ((uint32_t)off & 0xFC) |
                      0x80000000u);
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    outl(0xCF8, pci_addr(bus, dev, func, off));
    return inl(0xCFC);
}
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    uint32_t v = pci_read32(bus, dev, func, off & 0xFC);
    return (uint16_t)((v >> ((off & 2) * 8)) & 0xFFFF);
}
uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    uint32_t v = pci_read32(bus, dev, func, off & 0xFC);
    return (uint8_t)((v >> ((off & 3) * 8)) & 0xFF);
}
void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val) {
    outl(0xCF8, pci_addr(bus, dev, func, off));
    outl(0xCFC, val);
}
void pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint16_t val) {
    uint32_t cur = pci_read32(bus, dev, func, off & 0xFC);
    uint32_t shift = (off & 2) * 8;
    cur &= ~(0xFFFFu << shift);
    cur |= ((uint32_t)val << shift);
    pci_write32(bus, dev, func, off & 0xFC, cur);
}
/* __APPEND_MARK__ */

/* ============================================================
 * BAR 解析：写全 1 探测大小，判断 IO/MMIO/64 位
 * ============================================================ */
static void parse_bar(pci_device_t *d, int idx, uint8_t off) {
    uint32_t orig = pci_read32(d->bus, d->dev, d->func, off);
    if (orig == 0) return;
    pci_write32(d->bus, d->dev, d->func, off, 0xFFFFFFFFu);
    uint32_t szmask = pci_read32(d->bus, d->dev, d->func, off);
    pci_write32(d->bus, d->dev, d->func, off, orig); /* 还原 */
    if (szmask == 0) return;

    pci_bar_t *b = &d->bars[idx];
    if (orig & 0x1) {
        /* IO 空间 BAR */
        b->is_mmio = 0;
        b->base = (uint64_t)(orig & 0xFFFFFFFCu);
        b->size = (uint64_t)(~(szmask & 0xFFFFFFFCu) + 1) & 0xFFFF;
    } else {
        /* MMIO BAR */
        b->is_mmio = 1;
        b->prefetch = (orig & 0x8) ? 1 : 0;
        uint8_t type = (orig >> 1) & 0x3;
        if (type == 0x2 && idx < 5) {
            /* 64 位 BAR：占用下一个槽 */
            b->is_64bit = 1;
            uint32_t hi = pci_read32(d->bus, d->dev, d->func, off + 4);
            b->base = ((uint64_t)hi << 32) | (uint64_t)(orig & 0xFFFFFFF0u);
            uint32_t szlo = szmask & 0xFFFFFFF0u;
            b->size = (uint64_t)(~szlo + 1);
            d->bars[idx + 1].size = 0; /* 高半标记占用 */
        } else {
            b->base = (uint64_t)(orig & 0xFFFFFFF0u);
            b->size = (uint64_t)(~(szmask & 0xFFFFFFF0u) + 1);
        }
    }
}

/* ============================================================
 * 探测单个 function：读取基本信息并填入设备表
 * ============================================================ */
static void probe_function(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t vendor = pci_read16(bus, dev, func, 0x00);
    if (vendor == 0xFFFF) return; /* 无设备 */
    if (g_pci_count >= PCI_MAX_DEVICES) return;

    pci_device_t *d = &g_pci_devices[g_pci_count];
    for (int i = 0; i < (int)sizeof(pci_device_t); i++) ((uint8_t *)d)[i] = 0;
    d->bus = bus; d->dev = dev; d->func = func;
    d->vendor_id = vendor;
    d->device_id = pci_read16(bus, dev, func, 0x02);
    d->revision  = pci_read8(bus, dev, func, 0x08);
    d->prog_if   = pci_read8(bus, dev, func, 0x09);
    d->subclass  = pci_read8(bus, dev, func, 0x0A);
    d->class_code= pci_read8(bus, dev, func, 0x0B);
    d->header_type = pci_read8(bus, dev, func, 0x0E);
    d->irq_line  = pci_read8(bus, dev, func, PCI_OFFSET_INTLINE);
    d->irq_pin   = pci_read8(bus, dev, func, PCI_OFFSET_INTPIN);

    /* 仅 general header (type 0) 才有 6 个 BAR */
    if ((d->header_type & PCI_HEADER_TYPE_MASK) == PCI_HEADER_GENERAL) {
        for (int i = 0; i < 6; i++)
            parse_bar(d, i, (uint8_t)(0x10 + i * 4));
    }

    g_pci_count++;

    log_hex("[pci] ", ((uint64_t)bus<<16)|((uint64_t)dev<<8)|func, 6);
    log_hex(" ven=", d->vendor_id, 4);
    log_hex(" dev=", d->device_id, 4);
    log_hex(" cls=", d->class_code, 2);
    log_hex(" sub=", d->subclass, 2);
    log_hex(" irq=", d->irq_line, 2);
    pci_log("\r\n");
}

/* 递归扫描一条总线 */
static void scan_bus(uint8_t bus);

static void probe_device(uint8_t bus, uint8_t dev) {
    uint16_t vendor = pci_read16(bus, dev, 0, 0x00);
    if (vendor == 0xFFFF) return;

    probe_function(bus, dev, 0);

    uint8_t htype = pci_read8(bus, dev, 0, 0x0E);
    if (htype & PCI_HEADER_MULTIFUNC) {
        for (uint8_t func = 1; func < 8; func++)
            probe_function(bus, dev, func);
    }

    /* PCI-to-PCI 桥：递归扫描 secondary bus */
    for (uint8_t func = 0; func < 8; func++) {
        if (pci_read16(bus, dev, func, 0x00) == 0xFFFF) continue;
        uint8_t ht = pci_read8(bus, dev, func, 0x0E) & PCI_HEADER_TYPE_MASK;
        uint8_t cls = pci_read8(bus, dev, func, 0x0B);
        if (ht == PCI_HEADER_BRIDGE && cls == PCI_CLASS_BRIDGE) {
            uint8_t sec = pci_read8(bus, dev, func, PCI_OFFSET_SEC_BUS);
            if (sec != 0 && sec != bus) scan_bus(sec);
        }
    }
}

static void scan_bus(uint8_t bus) {
    for (uint8_t dev = 0; dev < 32; dev++)
        probe_device(bus, dev);
}

/* ============================================================
 * 公共 API
 * ============================================================ */
void pci_scan_all(void) {
    g_pci_count = 0;
    pci_log("[pci] scanning bus...\r\n");

    /* host bridge 可能多 function：多主机桥对应多根总线 */
    uint8_t htype = pci_read8(0, 0, 0, 0x0E);
    if (!(htype & PCI_HEADER_MULTIFUNC)) {
        scan_bus(0);
    } else {
        for (uint8_t func = 0; func < 8; func++) {
            if (pci_read16(0, 0, func, 0x00) == 0xFFFF) continue;
            scan_bus(func);
        }
    }

    log_hex("[pci] total devices=", g_pci_count, 2);
    pci_log("\r\n");
}

int pci_rescan_hotplug(void) {
    uint32_t before = g_pci_count;
    pci_scan_all();
    return (int)g_pci_count - (int)before;
}

uint32_t pci_known_device_count(void) { return g_pci_count; }

const pci_device_t *pci_get_device(uint32_t index) {
    if (index >= g_pci_count) return 0;
    return &g_pci_devices[index];
}

const pci_device_t *pci_find_by_class(uint8_t class_code, uint8_t subclass) {
    for (uint32_t i = 0; i < g_pci_count; i++) {
        pci_device_t *d = &g_pci_devices[i];
        if (d->class_code != class_code) continue;
        if (subclass != 0xFF && d->subclass != subclass) continue;
        return d;
    }
    return 0;
}

const pci_device_t *pci_find_by_id(uint16_t vendor_id, uint16_t device_id) {
    for (uint32_t i = 0; i < g_pci_count; i++) {
        pci_device_t *d = &g_pci_devices[i];
        if (d->vendor_id == vendor_id && d->device_id == device_id)
            return d;
    }
    return 0;
}

/* ---- command 寄存器使能 ---- */
static void set_command_bits(pci_device_t *d, uint16_t bits) {
    uint16_t cmd = pci_read16(d->bus, d->dev, d->func, PCI_OFFSET_COMMAND);
    cmd |= bits;
    pci_write16(d->bus, d->dev, d->func, PCI_OFFSET_COMMAND, cmd);
}
void pci_enable_bus_master(pci_device_t *d) { if (d) set_command_bits(d, PCI_CMD_BUS_MASTER); }
void pci_enable_mmio(pci_device_t *d)       { if (d) set_command_bits(d, PCI_CMD_MEM_SPACE); }
void pci_enable_io(pci_device_t *d)         { if (d) set_command_bits(d, PCI_CMD_IO_SPACE); }

void pci_dump_devices(void) {
    log_hex("[pci] devices=", g_pci_count, 2); pci_log("\r\n");
    for (uint32_t i = 0; i < g_pci_count; i++) {
        pci_device_t *d = &g_pci_devices[i];
        log_hex("  ", d->bus, 2);
        log_hex(":", d->dev, 2);
        log_hex(".", d->func, 1);
        log_hex("  ", d->vendor_id, 4);
        log_hex(":", d->device_id, 4);
        log_hex("  class=", d->class_code, 2);
        log_hex(":", d->subclass, 2);
        pci_log("\r\n");
        for (int b = 0; b < 6; b++) {
            if (d->bars[b].size == 0) continue;
            log_hex("     bar", b, 1);
            pci_log(d->bars[b].is_mmio ? " mmio @" : " io   @");
            log_hex("", d->bars[b].base, 8);
            log_hex(" size=", d->bars[b].size, 8);
            pci_log("\r\n");
        }
    }
}

