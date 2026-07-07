/*
 * xhci64.c — xHCI USB 主机控制器驱动（M2.3, MVP / polling + MSI）
 *
 * 复用套路（对齐 nvme64.c / ahci64.c）：
 *   - PCI 枚举：class 0x0C March 0x03 progif 0x30 (USB xHCI)
 *   - BAR0 64-bit MMIO，显式 identity-map（phys==virt）
 *   - pmm64 分配物理连续 DMA 缓冲（DCBAA/命令环/事件环/ERST）
 *   - MSI/MSI-X enable（pci64 提供），interrupter 0 IMAN.IE
 *
 * MVP 目标：初始化控制器 + 命令/事件环，复位并枚举 root hub 端口，
 *           上报设备连接与速率；不实现完整 USB 设备栈。
 */
#include <stdint.h>
#include <stddef.h>
#include "../include/xhci64.h"
#include "../include/pmm64.h"
#include "../include/vmm64.h"
#include "../../../kernel/include/pci.h"

/* ---- 外部依赖（对齐 nvme64.c 的真实接口名）---- */
extern uint64_t arch_x86_64_pmm_alloc_pages(uint64_t count);
extern void     early_serial64_write(const char *s);
extern void     arch_x86_64_proc_yield(void);
extern int      arch_x86_64_vmm_map_range(uint64_t virt, uint64_t phys,
                                          uint64_t length, uint64_t flags);

/* ---- PCI（M1.1 枚举 + M2.x MSI/MSI-X）---- */
extern const pci_device_t *pci_find_by_class(uint8_t class_code, uint8_t subclass);
extern void pci_enable_bus_master(pci_device_t *d);
extern void pci_enable_mmio(pci_device_t *d);
extern int  pci_msi_enable(pci_device_t *d, uint8_t vector, uint8_t apic_id);
extern int  pci_msix_enable(pci_device_t *d, uint8_t vector, uint8_t apic_id);

/* ---- 中断依赖（M2.x MSI）---- */
extern int  arch_x86_64_idt_register_irq(unsigned char cpu_vector, void (*handler)(void));
extern int  arch_x86_64_lapic_is_ready(void);
extern void arch_x86_64_lapic_send_eoi(void);
extern unsigned char arch_x86_64_lapic_id(void);
extern void x86_64_irq_xhci(void);   /* isr64.S 汇编 stub */

/* MSI 向量段 0x30-0x3F：AHCI=0x30, NVMe=0x31, xHCI=0x32 */
#define OPENOS_X86_64_XHCI_VECTOR 0x32u

/* ---- 日志辅助 ---- */
static void klog_hex(uint64_t v) {
    char buf[19]; buf[0]='0'; buf[1]='x'; buf[18]='\0';
    for (int i = 0; i < 16; i++) {
        int nib = (int)((v >> ((15 - i) * 4)) & 0xF);
        buf[2 + i] = (char)(nib < 10 ? ('0' + nib) : ('a' + nib - 10));
    }
    early_serial64_write(buf);
}
static void klog_dec(uint64_t v) {
    char buf[21]; int i = 20; buf[20] = '\0';
    if (v == 0) { buf[--i] = '0'; }
    else { while (v && i > 0) { buf[--i] = (char)('0' + (v % 10)); v /= 10; } }
    early_serial64_write(&buf[i]);
}
#define XLOG(s)      early_serial64_write("[xhci] " s "\n")
#define XLOG_NN(s)   early_serial64_write("[xhci] " s)
#define XHEX(v)      klog_hex((uint64_t)(v))
#define XDEC(v)      klog_dec((uint64_t)(v))
#define serial_write early_serial64_write

/* ============================================================
 * xHCI 寄存器映射（xHCI spec 1.2 §5）
 *   BAR0 起始 = Capability Registers
 *   Operational Registers 起始 = BAR0 + CAPLENGTH
 *   Runtime Registers 起始      = BAR0 + RTSOFF
 *   Doorbell Array 起始         = BAR0 + DBOFF
 * ============================================================ */

/* ---- Capability Registers（相对 BAR0）---- */
#define XCAP_CAPLENGTH   0x00  /* [7:0] cap 长度; [31:16] HCIVERSION */
#define XCAP_HCSPARAMS1  0x04  /* [31:24] MaxPorts; [7:0] MaxSlots */
#define XCAP_HCSPARAMS2  0x08  /* ERST Max, scratchpad bufs */
#define XCAP_HCSPARAMS3  0x0C
#define XCAP_HCCPARAMS1  0x10  /* [0] AC64; [16:31] xECP 偏移(dword) */
#define XCAP_DBOFF       0x14  /* Doorbell Array 偏移(对齐 4) */
#define XCAP_RTSOFF      0x18  /* Runtime 偏移(对齐 32) */
#define XCAP_HCCPARAMS2  0x1C

/* ---- Operational Registers（相对 op base）---- */
#define XOP_USBCMD       0x00  /* [0]RS run; [1]HCRST reset; [2]INTE; [3]HSEE */
#define XOP_USBSTS       0x04  /* [0]HCH halted; [11]CNR controller-not-ready */
#define XOP_PAGESIZE     0x08
#define XOP_DNCTRL       0x14
#define XOP_CRCR         0x18  /* Command Ring Control (64-bit) */
#define XOP_DCBAAP       0x30  /* Device Context Base Addr Array Ptr (64-bit) */
#define XOP_CONFIG       0x38  /* [7:0] MaxSlotsEn */
#define XOP_PORTS        0x400 /* Port Register Set 起始，每端口 0x10 */
#define XPORT_PORTSC     0x00  /* 相对每端口基址：[0]CCS;[1]PED;[4]OCA;[9:10]PLS部分;[3:3]PR reset;[10:13]速率 */
#define XPORT_STRIDE     0x10

/* USBCMD 位 */
#define USBCMD_RS        (1u << 0)
#define USBCMD_HCRST     (1u << 1)
#define USBCMD_INTE      (1u << 2)
/* USBSTS 位 */
#define USBSTS_HCH       (1u << 0)
#define USBSTS_EINT      (1u << 3)
#define USBSTS_PCD       (1u << 4)
#define USBSTS_CNR       (1u << 11)
/* PORTSC 位 */
#define PORTSC_CCS       (1u << 0)  /* Current Connect Status */
#define PORTSC_PED       (1u << 1)  /* Port Enabled/Disabled */
#define PORTSC_PR        (1u << 4)  /* Port Reset */
#define PORTSC_PP        (1u << 9)  /* Port Power */
#define PORTSC_CSC       (1u << 17) /* Connect Status Change (RW1C) */
#define PORTSC_PRC       (1u << 21) /* Port Reset Change (RW1C) */
#define PORTSC_SPEED_SHIFT 10
#define PORTSC_SPEED_MASK  0xF
/* PORTSC RW1C 变更位掩码（写回清除时保留，勿误触 PED 等 RW 位）*/
#define PORTSC_CHANGE_BITS (0x00FE0000u)

/* ---- Runtime Registers（相对 rt base）---- */
#define XRT_IMAN(i)      (0x20 + (i)*0x20 + 0x00) /* Interrupter Management */
#define XRT_IMOD(i)      (0x20 + (i)*0x20 + 0x04) /* Interrupter Moderation */
#define XRT_ERSTSZ(i)    (0x20 + (i)*0x20 + 0x08) /* Event Ring Seg Table Size */
#define XRT_ERSTBA(i)    (0x20 + (i)*0x20 + 0x10) /* ERST Base Addr (64-bit) */
#define XRT_ERDP(i)      (0x20 + (i)*0x20 + 0x18) /* Event Ring Dequeue Ptr (64-bit) */
#define IMAN_IP          (1u << 0)  /* Interrupt Pending (RW1C) */
#define IMAN_IE          (1u << 1)  /* Interrupt Enable */
#define ERDP_EHB         (1u << 3)  /* Event Handler Busy (RW1C) */

/* ---- MMIO 读写 helper（xHCI 寄存器均按 32/64 位对齐访问）---- */
static inline uint32_t mmio_r32(uint64_t base, uint32_t off) {
    return *(volatile uint32_t *)(base + off);
}
static inline void mmio_w32(uint64_t base, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(base + off) = v;
}
static inline uint64_t mmio_r64(uint64_t base, uint32_t off) {
    return *(volatile uint64_t *)(base + off);
}
static inline void mmio_w64(uint64_t base, uint32_t off, uint64_t v) {
    *(volatile uint64_t *)(base + off) = v;
}

/* ============================================================
 * TRB（Transfer Request Block）— xHCI 环的基本单元，16 字节
 * 通用布局：[0..7]parameter [8..11]status [12..15]control
 *   control: [0]C cycle; [10..15]TRB Type; 其余类型相关
 * ============================================================ */
typedef struct {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} __attribute__((packed)) xhci_trb_t;

/* TRB Type（control[10:15]） */
#define TRB_TYPE_SHIFT     10
#define TRB_NORMAL         1
#define TRB_LINK           6
#define TRB_ENABLE_SLOT    9
#define TRB_ADDRESS_DEV    11
#define TRB_NOOP_CMD       23
/* Event TRB Type */
#define TRB_TRANSFER_EVT   32
#define TRB_CMD_COMPL_EVT  33
#define TRB_PORT_STAT_EVT  34
#define TRB_HOST_CTRL_EVT  37
/* control 位 */
#define TRB_CYCLE          (1u << 0)
#define TRB_LINK_TC        (1u << 1)  /* Link TRB Toggle Cycle */
#define TRB_IOC            (1u << 5)  /* Interrupt On Completion */
/* Event TRB status 中 completion code（[24:31]） */
#define TRB_CC_SHIFT       24
#define TRB_CC_SUCCESS     1
/* Event TRB control 中 slot id（[24:31]） */
#define TRB_SLOT_SHIFT     24

#define TRB_GET_TYPE(t)    (((t)->control >> TRB_TYPE_SHIFT) & 0x3F)
#define TRB_GET_CC(t)      (((t)->status  >> TRB_CC_SHIFT) & 0xFF)
#define TRB_GET_SLOT(t)    (((t)->control >> TRB_SLOT_SHIFT) & 0xFF)

/* ============================================================
 * ERST（Event Ring Segment Table）条目：64-bit 基址 + size
 * ============================================================ */
typedef struct {
    uint64_t ring_base;   /* 事件环段物理基址（对齐 64） */
    uint32_t ring_size;   /* TRB 个数 */
    uint32_t reserved;
} __attribute__((packed)) xhci_erst_entry_t;

/* 环尺寸（TRB 个数）— MVP 均取单页可容纳 256 个 TRB(4KiB/16) */
#define CMD_RING_TRBS      256
#define EVT_RING_TRBS      256

/* ============================================================
 * 驱动全局状态
 * ============================================================ */
typedef struct {
    int          present;
    const pci_device_t *pci;  /* PCI 设备记录指针（枚举结果） */
    uint64_t     mmio;        /* BAR0 虚拟=物理基址 */
    uint64_t     op;          /* Operational 基址 = mmio + CAPLENGTH */
    uint64_t     rt;          /* Runtime 基址 = mmio + RTSOFF */
    uint64_t     db;          /* Doorbell 基址 = mmio + DBOFF */
    uint32_t     max_ports;   /* HCSPARAMS1 MaxPorts */
    uint32_t     max_slots;   /* HCSPARAMS1 MaxSlots */
    uint32_t     ac64;        /* 64-bit 寻址支持 */

    /* DCBAA（Device Context Base Addr Array）— (max_slots+1) 个 64-bit 条目 */
    volatile uint64_t *dcbaa;

    /* Command Ring */
    volatile xhci_trb_t *cmd_ring;
    uint64_t     cmd_ring_phys;
    uint32_t     cmd_enqueue;  /* 当前入队索引 */
    uint32_t     cmd_cycle;    /* producer cycle state */

    /* Event Ring */
    volatile xhci_trb_t     *evt_ring;
    uint64_t                 evt_ring_phys;
    volatile xhci_erst_entry_t *erst;
    uint64_t                 erst_phys;
    uint32_t                 evt_dequeue;  /* 当前出队索引 */
    uint32_t                 evt_cycle;    /* consumer cycle state */

    uint32_t     connected;   /* 已连接端口计数 */
    volatile uint32_t irq_count; /* MSI 中断触发次数 */
    int          irq_installed;
} xhci_ctrl_t;

static xhci_ctrl_t g_xhci;

/* ============================================================
 * 中断 handler（MSI 向量 0x32，由 isr64.S 的 x86_64_irq_xhci stub 调用）
 *   xHCI event 到达时触发；清 USBSTS.EINT + IMAN.IP（均 RW1C）
 *   事件处理仍由 polling 路径完成（MVP），这里仅记数 + 应答硬件
 *   —— 证明 MSI 路径真实生效（对齐 AHCI）
 * ============================================================ */
void arch_x86_64_xhci_irq_trampoline(void) {
    xhci_ctrl_t *x = &g_xhci;
    if (!x->present) return;
    x->irq_count++;
    /* 清 USBSTS.EINT（RW1C）*/
    uint32_t sts = mmio_r32(x->op, XOP_USBSTS);
    if (sts & USBSTS_EINT)
        mmio_w32(x->op, XOP_USBSTS, USBSTS_EINT);
    /* 清 interrupter0 IMAN.IP（RW1C），保留 IE */
    uint32_t iman = mmio_r32(x->rt, XRT_IMAN(0));
    mmio_w32(x->rt, XRT_IMAN(0), iman | IMAN_IP);
    /* LAPIC EOI（对齐 ahci/nvme 的 MSI handler 契约）*/
    arch_x86_64_lapic_send_eoi();
}

/* 粗糙 busy-wait 延时（无 timer 依赖，对齐 nvme64.c 风格） */
static void xhci_delay(uint64_t loops) {
    for (volatile uint64_t i = 0; i < loops; i++) __asm__ volatile("pause");
}

/* 等待 op 寄存器某位变为期望值；want=1 等置位，want=0 等清零。
 * 返回 0 成功，-1 超时。 */
static int xhci_wait_bit(uint32_t off, uint32_t bit, int want, uint32_t spins) {
    for (uint32_t i = 0; i < spins; i++) {
        uint32_t v = mmio_r32(g_xhci.op, off);
        int set = (v & bit) ? 1 : 0;
        if (set == want) return 0;
        xhci_delay(2000);
    }
    return -1;
}

/* ============================================================
 * 命令环 / 事件环操作
 * ============================================================ */

/* 敏命令环铃（Doorbell 0 = Command Ring，写 0） */
static void xhci_ring_cmd_doorbell(void) {
    mmio_w32(g_xhci.db, 0, 0);
}

/* 向命令环入队一个 TRB（自动处理 Link TRB 绕回 + cycle 翻转）。
 * 返回该命令 TRB 的物理地址（供匹配完成事件）。 */
static uint64_t xhci_cmd_enqueue(uint64_t param, uint32_t status, uint32_t ctrl_type) {
    xhci_ctrl_t *x = &g_xhci;
    volatile xhci_trb_t *trb = &x->cmd_ring[x->cmd_enqueue];
    uint64_t trb_phys = x->cmd_ring_phys + x->cmd_enqueue * sizeof(xhci_trb_t);

    trb->parameter = param;
    trb->status    = status;
    /* 组装 control：type + 当前 cycle（+ IOC 以便产生完成事件）*/
    uint32_t ctrl = (ctrl_type << TRB_TYPE_SHIFT) | TRB_IOC;
    if (x->cmd_cycle) ctrl |= TRB_CYCLE;
    trb->control = ctrl;

    /* 推进 enqueue */
    x->cmd_enqueue++;
    if (x->cmd_enqueue >= CMD_RING_TRBS - 1) {
        /* 最后一个 slot 为 Link TRB，绕回起始并翻转 cycle */
        volatile xhci_trb_t *link = &x->cmd_ring[CMD_RING_TRBS - 1];
        link->parameter = x->cmd_ring_phys;
        link->status    = 0;
        uint32_t lctrl = (TRB_LINK << TRB_TYPE_SHIFT) | TRB_LINK_TC;
        if (x->cmd_cycle) lctrl |= TRB_CYCLE;
        link->control = lctrl;
        x->cmd_enqueue = 0;
        x->cmd_cycle ^= 1;
    }
    return trb_phys;
}

/* 轮询事件环，等待与 cmd_trb_phys 匹配的命令完成事件。
 * 成功时通过 out_cc/out_slot 回传。返回 0 命中，-1 超时。 */
static int xhci_wait_cmd_complete(uint64_t cmd_trb_phys, uint32_t *out_cc, uint32_t *out_slot) {
    xhci_ctrl_t *x = &g_xhci;
    for (uint32_t spin = 0; spin < 200000; spin++) {
        volatile xhci_trb_t *evt = &x->evt_ring[x->evt_dequeue];
        uint32_t c = evt->control;
        int cyc = (c & TRB_CYCLE) ? 1 : 0;
        if (cyc == (int)x->evt_cycle) {
            /* 有新事件 */
            uint32_t type = (c >> TRB_TYPE_SHIFT) & 0x3F;
            if (type == TRB_CMD_COMPL_EVT && evt->parameter == cmd_trb_phys) {
                if (out_cc)   *out_cc   = TRB_GET_CC(evt);
                if (out_slot) *out_slot = TRB_GET_SLOT(evt);
                /* 推进 dequeue + 更新 ERDP */
                x->evt_dequeue++;
                if (x->evt_dequeue >= EVT_RING_TRBS) {
                    x->evt_dequeue = 0;
                    x->evt_cycle ^= 1;
                }
                uint64_t erdp = x->evt_ring_phys + x->evt_dequeue * sizeof(xhci_trb_t);
                mmio_w64(x->rt, XRT_ERDP(0), erdp | ERDP_EHB);
                return 0;
            }
            /* 其他事件（端口状态等）：消费并继续 */
            x->evt_dequeue++;
            if (x->evt_dequeue >= EVT_RING_TRBS) {
                x->evt_dequeue = 0;
                x->evt_cycle ^= 1;
            }
            uint64_t erdp = x->evt_ring_phys + x->evt_dequeue * sizeof(xhci_trb_t);
            mmio_w64(x->rt, XRT_ERDP(0), erdp | ERDP_EHB);
        } else {
            xhci_delay(2000);
        }
    }
    return -1;
}

/* ============================================================
 * 控制器初始化
 * ============================================================ */

/* 分配单页或多页物理连续内存并清零（phys==virt）。失败返回 0。 */
static uint64_t xhci_zalloc_pages(uint64_t pages) {
    uint64_t p = arch_x86_64_pmm_alloc_pages(pages);
    if (!p) return 0;
    volatile uint8_t *b = (volatile uint8_t *)p;
    for (uint64_t i = 0; i < pages * 4096; i++) b[i] = 0;
    return p;
}

/* 探测 PCI 上的 xHCI（class 0x0C sub 0x03）。命中填 g_xhci.pci。 */
static int xhci_probe_pci(void) {
    const pci_device_t *dev = pci_find_by_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB);
    if (!dev) {
        XLOG("no xHCI controller found on PCI");
        return -1;
    }
    /* prog_if 0x30 = xHCI（区分 UHCI/OHCI/EHCI）*/
    if (dev->prog_if != 0x30) {
        XLOG_NN("USB controller prog_if="); XHEX(dev->prog_if);
        serial_write(" not xHCI(0x30), skip\n");
        return -1;
    }
    g_xhci.pci = dev;
    XLOG_NN("found xHCI @ bus="); XDEC(dev->bus);
    serial_write(" dev="); XDEC(dev->dev);
    serial_write(" fn=");   XDEC(dev->func);
    serial_write(" vendor="); XHEX(dev->vendor_id);
    serial_write(" devid="); XHEX(dev->device_id);
    serial_write("\n");
    return 0;
}

/* 映射 BAR0 MMIO + 解析 cap/op/rt/db 基址。 */
static int xhci_map_regs(void) {
    xhci_ctrl_t *x = &g_xhci;
    const pci_bar_t *bar0 = &x->pci->bars[0];
    if (bar0->base == 0 || !bar0->is_mmio) { XLOG("bad BAR0"); return -1; }
    uint64_t phys = bar0->base;

    /* xHCI 寄存器窗口至少覆盖 cap+op+rt+db，保守映射 64KiB（identity）*/
    if (arch_x86_64_vmm_map_range(phys, phys, 0x10000ULL,
                                  OPENOS_X86_64_VMM_MMIO_FLAGS) != 0) {
        XLOG("vmm_map_range BAR0 fail"); return -1;
    }
    x->mmio = phys;

    uint32_t caplen = mmio_r32(x->mmio, XCAP_CAPLENGTH) & 0xFF;
    uint32_t rtsoff = mmio_r32(x->mmio, XCAP_RTSOFF) & ~0x1Fu;
    uint32_t dboff  = mmio_r32(x->mmio, XCAP_DBOFF)  & ~0x3u;
    x->op = x->mmio + caplen;
    x->rt = x->mmio + rtsoff;
    x->db = x->mmio + dboff;

    uint32_t hcs1 = mmio_r32(x->mmio, XCAP_HCSPARAMS1);
    x->max_slots = hcs1 & 0xFF;
    x->max_ports = (hcs1 >> 24) & 0xFF;
    uint32_t hcc1 = mmio_r32(x->mmio, XCAP_HCCPARAMS1);
    x->ac64 = hcc1 & 0x1;

    XLOG_NN("caplen="); XHEX(caplen);
    serial_write(" rtsoff="); XHEX(rtsoff);
    serial_write(" dboff="); XHEX(dboff);
    serial_write(" slots="); XDEC(x->max_slots);
    serial_write(" ports="); XDEC(x->max_ports);
    serial_write(" ac64="); XDEC(x->ac64);
    serial_write("\n");
    return 0;
}

/* 复位控制器：先 halt（RS=0），再 HCRST，等 CNR 清零。 */
static int xhci_reset(void) {
    xhci_ctrl_t *x = &g_xhci;
    /* 先停运行 */
    uint32_t cmd = mmio_r32(x->op, XOP_USBCMD);
    if (cmd & USBCMD_RS) {
        mmio_w32(x->op, XOP_USBCMD, cmd & ~USBCMD_RS);
        if (xhci_wait_bit(XOP_USBSTS, USBSTS_HCH, 1, 1000) != 0) {
            XLOG("halt timeout"); return -1;
        }
    }
    /* 发起复位 */
    mmio_w32(x->op, XOP_USBCMD, USBCMD_HCRST);
    if (xhci_wait_bit(XOP_USBCMD, USBCMD_HCRST, 0, 2000) != 0) {
        XLOG("HCRST timeout"); return -1;
    }
    /* 等控制器就绪（CNR=0） */
    if (xhci_wait_bit(XOP_USBSTS, USBSTS_CNR, 0, 2000) != 0) {
        XLOG("CNR timeout"); return -1;
    }
    XLOG("controller reset OK");
    return 0;
}

/* 分配并配置 DCBAA / 命令环 / 事件环 + ERST。 */
static int xhci_setup_rings(void) {
    xhci_ctrl_t *x = &g_xhci;

    /* ---- DCBAA（(max_slots+1) * 8 字节，对齐 64，单页足够）---- */
    uint64_t dcbaa_phys = xhci_zalloc_pages(1);
    if (!dcbaa_phys) { XLOG("DCBAA alloc fail"); return -1; }
    x->dcbaa = (volatile uint64_t *)dcbaa_phys;

    /* ---- Command Ring（单页 = 256 TRB）---- */
    uint64_t cmd_phys = xhci_zalloc_pages(1);
    if (!cmd_phys) { XLOG("cmd ring alloc fail"); return -1; }
    x->cmd_ring = (volatile xhci_trb_t *)cmd_phys;
    x->cmd_ring_phys = cmd_phys;
    x->cmd_enqueue = 0;
    x->cmd_cycle   = 1;
    /* 预置末尾 Link TRB（绕回）---- */
    {
        volatile xhci_trb_t *link = &x->cmd_ring[CMD_RING_TRBS - 1];
        link->parameter = cmd_phys;
        link->status    = 0;
        link->control   = (TRB_LINK << TRB_TYPE_SHIFT) | TRB_LINK_TC | TRB_CYCLE;
    }

    /* ---- Event Ring（单页 = 256 TRB）---- */
    uint64_t evt_phys = xhci_zalloc_pages(1);
    if (!evt_phys) { XLOG("evt ring alloc fail"); return -1; }
    x->evt_ring = (volatile xhci_trb_t *)evt_phys;
    x->evt_ring_phys = evt_phys;
    x->evt_dequeue = 0;
    x->evt_cycle   = 1;

    /* ---- ERST（单段，单页）---- */
    uint64_t erst_phys = xhci_zalloc_pages(1);
    if (!erst_phys) { XLOG("ERST alloc fail"); return -1; }
    x->erst = (volatile xhci_erst_entry_t *)erst_phys;
    x->erst_phys = erst_phys;
    x->erst[0].ring_base = evt_phys;
    x->erst[0].ring_size = EVT_RING_TRBS;
    x->erst[0].reserved  = 0;

    /* ---- 写入控制器寄存器 ---- */
    /* DCBAAP */
    mmio_w64(x->op, XOP_DCBAAP, dcbaa_phys);
    /* CONFIG.MaxSlotsEn = max_slots */
    mmio_w32(x->op, XOP_CONFIG, x->max_slots);
    /* CRCR = cmd_ring_phys | RCS(=cmd_cycle) */
    mmio_w64(x->op, XOP_CRCR, cmd_phys | (x->cmd_cycle ? 1 : 0));

    /* Interrupter 0: ERSTSZ=1, ERDP=evt_phys, ERSTBA=erst_phys */
    mmio_w32(x->rt, XRT_ERSTSZ(0), 1);
    mmio_w64(x->rt, XRT_ERDP(0),   evt_phys | ERDP_EHB);
    mmio_w64(x->rt, XRT_ERSTBA(0), erst_phys);
    /* IMOD 适度节流，使能 IMAN.IE（中断使能）*/
    mmio_w32(x->rt, XRT_IMOD(0), 4000);
    mmio_w32(x->rt, XRT_IMAN(0), IMAN_IE);

    XLOG("rings configured (DCBAA/CMD/EVT/ERST)");
    return 0;
}

/* 启动控制器：RS=1 + INTE，等 HCH 清零。 */
static int xhci_start(void) {
    xhci_ctrl_t *x = &g_xhci;
    uint32_t cmd = mmio_r32(x->op, XOP_USBCMD);
    cmd |= USBCMD_RS | USBCMD_INTE;
    mmio_w32(x->op, XOP_USBCMD, cmd);
    if (xhci_wait_bit(XOP_USBSTS, USBSTS_HCH, 0, 2000) != 0) {
        XLOG("controller start timeout (HCH still set)"); return -1;
    }
    XLOG("controller running (RS=1)");
    return 0;
}

/* ============================================================
 * 端口枚举
 * ============================================================ */

/* 速率码→可读名（xHCI PORTSC Port Speed） */
static const char *xhci_speed_name(uint32_t sp) {
    switch (sp) {
        case 1: return "Full-Speed(12Mb)";
        case 2: return "Low-Speed(1.5Mb)";
        case 3: return "High-Speed(480Mb)";
        case 4: return "SuperSpeed(5Gb)";
        case 5: return "SuperSpeedPlus(10Gb)";
        default: return "unknown";
    }
}

/* 复位单个端口（PR=1，等 PRC）。端口号从 1 开始。 */
static int xhci_reset_port(uint32_t port) {
    xhci_ctrl_t *x = &g_xhci;
    uint32_t off = XOP_PORTS + (port - 1) * XPORT_STRIDE + XPORT_PORTSC;
    uint32_t sc = mmio_r32(x->op, off);
    /* 写回时保留非 RW1C 位，清除已有变更位，置 PR */
    uint32_t w = (sc & ~PORTSC_CHANGE_BITS) | PORTSC_PR;
    mmio_w32(x->op, off, w);
    /* 等 PRC（复位完成）*/
    for (uint32_t i = 0; i < 2000; i++) {
        sc = mmio_r32(x->op, off);
        if (sc & PORTSC_PRC) {
            /* 清 PRC（RW1C）*/
            mmio_w32(x->op, off, (sc & ~PORTSC_CHANGE_BITS) | PORTSC_PRC);
            return (sc & PORTSC_PED) ? 0 : -1;
        }
        xhci_delay(2000);
    }
    return -1;
}

/* 遍历 root hub 端口，统计已连接设备并复位。 */
static void xhci_enum_ports(void) {
    xhci_ctrl_t *x = &g_xhci;
    x->connected = 0;
    for (uint32_t p = 1; p <= x->max_ports; p++) {
        uint32_t off = XOP_PORTS + (p - 1) * XPORT_STRIDE + XPORT_PORTSC;
        uint32_t sc = mmio_r32(x->op, off);
        if (!(sc & PORTSC_CCS)) continue;  /* 无设备 */
        x->connected++;
        uint32_t speed = (sc >> PORTSC_SPEED_SHIFT) & PORTSC_SPEED_MASK;
        XLOG_NN("port "); XDEC(p);
        serial_write(": device connected, speed=");
        serial_write(xhci_speed_name(speed));
        serial_write(" portsc="); XHEX(sc);
        serial_write("\n");
        /* 尝试复位端口（USB3 端口连接后通常已自动 enabled）*/
        if (!(sc & PORTSC_PED)) {
            if (xhci_reset_port(p) == 0)
                XLOG("  port reset + enabled OK");
            else
                XLOG("  port reset done (not enabled)");
        } else {
            XLOG("  port already enabled");
        }
    }
    XLOG_NN("enumerated ports: connected="); XDEC(x->connected);
    serial_write("/"); XDEC(x->max_ports); serial_write("\n");
}

/* 发一个 No-Op 命令验证命令环/事件环通路。返回 0 PASS。 */
static int xhci_test_noop(void) {
    uint64_t trb = xhci_cmd_enqueue(0, 0, TRB_NOOP_CMD);
    xhci_ring_cmd_doorbell();
    uint32_t cc = 0, slot = 0;
    if (xhci_wait_cmd_complete(trb, &cc, &slot) != 0) {
        XLOG("NOOP command timeout"); return -1;
    }
    if (cc != TRB_CC_SUCCESS) {
        XLOG_NN("NOOP completion code="); XDEC(cc); serial_write("\n");
        return -1;
    }
    XLOG("NOOP command completed (cmd ring <-> event ring OK)");
    return 0;
}

/* ============================================================
 * 对外 API
 * ============================================================ */

int xhci_init(void) {
    xhci_ctrl_t *x = &g_xhci;
    if (x->present) return 0;  /* 幂等 */

    XLOG("init: probing xHCI controller...");
    if (xhci_probe_pci() != 0) return -1;

    /* PCI: 使能 Bus Master + Memory Space */
    pci_enable_bus_master((pci_device_t *)x->pci);
    pci_enable_mmio((pci_device_t *)x->pci);

    if (xhci_map_regs()  != 0) return -1;
    if (xhci_reset()     != 0) return -1;
    if (xhci_setup_rings() != 0) return -1;
    if (xhci_start()     != 0) return -1;

    x->present = 1;

    /* 命令环自测（NOOP）*/
    xhci_test_noop();
    /* 枚举端口 */
    xhci_enum_ports();

    XLOG("init done");
    return 0;
}

int xhci_present(void) {
    return g_xhci.present ? 1 : 0;
}

uint32_t xhci_port_count(void) {
    return g_xhci.present ? g_xhci.max_ports : 0;
}

uint32_t xhci_connected_ports(void) {
    return g_xhci.present ? g_xhci.connected : 0;
}

uint32_t xhci_irq_count(void) {
    return g_xhci.irq_count;
}

/* 延迟挂载 MSI（LAPIC 就绪后调用，对齐 nvme/ahci 的晚期挂载套路）*/
void xhci_irq_install_late(void) {
    xhci_ctrl_t *x = &g_xhci;
    if (!x->present || x->irq_installed) return;
    if (!arch_x86_64_lapic_is_ready()) return;  /* LAPIC 未就绪，稍后重试 */
    uint8_t apic = arch_x86_64_lapic_id();
    /* 注册 IDT MSI 向量 → 汇编 stub */
    arch_x86_64_idt_register_irq(OPENOS_X86_64_XHCI_VECTOR, x86_64_irq_xhci);
    /* 优先 MSI-X，回退 MSI */
    if (pci_msix_enable((pci_device_t *)x->pci, OPENOS_X86_64_XHCI_VECTOR, apic) == 0) {
        XLOG("MSI-X enabled (vector 0x32)");
    } else if (pci_msi_enable((pci_device_t *)x->pci, OPENOS_X86_64_XHCI_VECTOR, apic) == 0) {
        XLOG("MSI enabled (vector 0x32)");
    } else {
        XLOG("MSI/MSI-X enable failed; staying on polling");
    }
    x->irq_installed = 1;
}

int xhci_selftest(void) {
    XLOG("=== xHCI selftest begin ===");
    if (xhci_init() != 0) {
        XLOG("selftest FAIL: init error");
        return -1;
    }
    XLOG_NN("selftest: max_ports="); XDEC(g_xhci.max_ports);
    serial_write(" connected="); XDEC(g_xhci.connected);
    serial_write(" irq="); XDEC(g_xhci.irq_count);
    serial_write("\n");
    XLOG("=== xHCI selftest PASS ===");
    return 0;
}
