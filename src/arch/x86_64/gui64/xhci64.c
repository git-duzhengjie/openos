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
#include "../../../kernel/include/usb.h"

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
#define XNL()        early_serial64_write("\n")
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
#define TRB_SETUP_STAGE    2
#define TRB_DATA_STAGE     3
#define TRB_STATUS_STAGE   4
#define TRB_LINK           6
#define TRB_ENABLE_SLOT    9
#define TRB_ADDRESS_DEV    11
#define TRB_CONFIG_EP      12
#define TRB_EVAL_CONTEXT   13
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
#define TRB_IDT            (1u << 6)  /* Immediate Data（Setup Stage 用） */
#define TRB_ISP            (1u << 2)  /* Interrupt on Short Packet */
#define TRB_CHAIN          (1u << 4)  /* Chain bit */
/* Setup Stage TRB：TRT（Transfer Type，control[16:17]） */
#define TRB_TRT_NO_DATA    0
#define TRB_TRT_OUT        2
#define TRB_TRT_IN         3
/* Data/Status Stage：DIR bit（control[16]，1=IN） */
#define TRB_DIR_IN         (1u << 16)
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
    uint32_t     ctx_size;    /* Context 结构大小：CSZ=1 时 64，否则 32 字节 */

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
static uint64_t xhci_cmd_enqueue_ex(uint64_t param, uint32_t status, uint32_t ctrl_type, uint32_t extra_ctrl) {
    xhci_ctrl_t *x = &g_xhci;
    volatile xhci_trb_t *trb = &x->cmd_ring[x->cmd_enqueue];
    uint64_t trb_phys = x->cmd_ring_phys + x->cmd_enqueue * sizeof(xhci_trb_t);

    trb->parameter = param;
    trb->status    = status;
    /* 组装 control：type + 当前 cycle（+ IOC 以便产生完成事件）+ 额外字段（如 slot_id）*/
    uint32_t ctrl = (ctrl_type << TRB_TYPE_SHIFT) | TRB_IOC | extra_ctrl;
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

static uint64_t xhci_cmd_enqueue(uint64_t param, uint32_t status, uint32_t ctrl_type) {
    return xhci_cmd_enqueue_ex(param, status, ctrl_type, 0);
}

/* 通用事件等待器：轮询事件环，等待匹配 want_type 且 parameter==trb_phys 的事件。
 * 命中回传 cc/slot/残余长度（transfer event 的 status[23:0]）。返回 0 命中，-1 超时。
 * 途中遇到的其它事件（端口状态等）一律消费跳过。 */
static int xhci_wait_event(uint32_t want_type, uint64_t trb_phys,
                           uint32_t *out_cc, uint32_t *out_slot, uint32_t *out_resid) {
    xhci_ctrl_t *x = &g_xhci;
    for (uint32_t spin = 0; spin < 200000; spin++) {
        volatile xhci_trb_t *evt = &x->evt_ring[x->evt_dequeue];
        uint32_t c = evt->control;
        int cyc = (c & TRB_CYCLE) ? 1 : 0;
        if (cyc == (int)x->evt_cycle) {
            uint32_t type = (c >> TRB_TYPE_SHIFT) & 0x3F;
            int hit = (type == want_type && evt->parameter == trb_phys);
            if (hit) {
                if (out_cc)    *out_cc    = TRB_GET_CC(evt);
                if (out_slot)  *out_slot  = TRB_GET_SLOT(evt);
                if (out_resid) *out_resid = evt->status & 0xFFFFFF;
            }
            /* 推进 dequeue + 更新 ERDP（命中与否都要消费）*/
            x->evt_dequeue++;
            if (x->evt_dequeue >= EVT_RING_TRBS) {
                x->evt_dequeue = 0;
                x->evt_cycle ^= 1;
            }
            uint64_t erdp = x->evt_ring_phys + x->evt_dequeue * sizeof(xhci_trb_t);
            mmio_w64(x->rt, XRT_ERDP(0), erdp | ERDP_EHB);
            if (hit) return 0;
        } else {
            xhci_delay(2000);
        }
    }
    return -1;
}

/* 轮询事件环，等待与 cmd_trb_phys 匹配的命令完成事件（薄封装）。
 * 成功时通过 out_cc/out_slot 回传。返回 0 命中，-1 超时。 */
static int xhci_wait_cmd_complete(uint64_t cmd_trb_phys, uint32_t *out_cc, uint32_t *out_slot) {
    return xhci_wait_event(TRB_CMD_COMPL_EVT, cmd_trb_phys, out_cc, out_slot, 0);
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
    x->ctx_size = (hcc1 & (1u << 2)) ? 64 : 32;  /* CSZ: HCCPARAMS1 bit2 */

    XLOG_NN("caplen="); XHEX(caplen);
    serial_write(" rtsoff="); XHEX(rtsoff);
    serial_write(" dboff="); XHEX(dboff);
    serial_write(" slots="); XDEC(x->max_slots);
    serial_write(" ports="); XDEC(x->max_ports);
    serial_write(" ac64="); XDEC(x->ac64);
    serial_write(" ctxsz="); XDEC(x->ctx_size);
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

/* ============================================================
 * USB 设备栈（M2.3 Step1-2）——Slot 分配 / Address Device /
 * EP0 控制传输 / GET_DESCRIPTOR / SET_CONFIGURATION
 * ============================================================ */

/* ---- USB 标准请求（bmRequestType / bRequest）---- */
#define USB_REQTYPE_D2H    0x80  /* Device-to-Host */
#define USB_REQTYPE_H2D    0x00  /* Host-to-Device */
#define USB_REQTYPE_STD    0x00
#define USB_REQTYPE_CLASS  0x20
#define USB_REQTYPE_IFACE  0x01
#define USB_REQ_GET_DESCRIPTOR   6
#define USB_REQ_SET_ADDRESS      5
#define USB_REQ_SET_CONFIG       9
#define USB_REQ_SET_IFACE        11
/* HID 类请求 */
#define HID_REQ_SET_PROTOCOL     0x0B
#define HID_REQ_SET_IDLE         0x0A
#define HID_PROTO_BOOT           0
#define HID_SUB_BOOT             1
#define HID_PROTO_KEYBOARD       1
#define HID_PROTO_MOUSE          2

#ifndef XHCI_MAX_DEVS
#define XHCI_MAX_DEVS   4
#endif

/* 单个 USB 设备的运行时状态 */
typedef struct xhci_dev {
    int      used;
    uint32_t slot_id;
    uint32_t port;        /* root hub 端口号（1-based）*/
    uint32_t speed;       /* PORTSC speed 字段 */
    uint8_t  dev_class;   /* bInterfaceClass */
    uint8_t  proto;       /* bInterfaceProtocol（1=键盘 2=鼠标）*/
    uint8_t  config_val;  /* bConfigurationValue */
    uint8_t  iface_num;   /* bInterfaceNumber */
    uint8_t  ep_in_addr;  /* Interrupt IN 端点地址（含方向位）*/
    uint16_t ep_in_mps;   /* Interrupt IN 端点 MaxPacketSize */
    uint8_t  ep_in_interval; /* bInterval */
    uint16_t vid, pid;

    /* 每设备 xHCI 上下文（物理连续，页对齐）*/
    uint64_t in_ctx_phys;    /* Input Context（含 Input Control + Slot + EP）*/
    uint8_t *in_ctx;
    uint64_t dev_ctx_phys;   /* Device Context（写入 DCBAA[slot]）*/
    uint8_t *dev_ctx;
    /* EP0 Transfer Ring */
    xhci_trb_t *ep0_ring;
    uint64_t    ep0_ring_phys;
    uint32_t    ep0_enqueue;
    uint32_t    ep0_cycle;
    /* Interrupt IN Transfer Ring（HID 用，Step3）*/
    xhci_trb_t *ep_in_ring;
    uint64_t    ep_in_ring_phys;
    uint32_t    ep_in_enqueue;
    uint32_t    ep_in_cycle;
    uint8_t    *hid_buf;      /* HID report DMA 缓冲 */
    uint64_t    hid_buf_phys;
} xhci_dev_t;

static xhci_dev_t g_devs[XHCI_MAX_DEVS];

#define EP0_RING_TRBS   16
#define EPIN_RING_TRBS  16
#define CTX_BYTES(x)    ((x)->ctx_size)  /* 32 或 64 */

/* 敲设备 Doorbell：slot 号 → doorbell[slot]，target=DCI */
static void xhci_ring_dev_doorbell(uint32_t slot, uint32_t dci) {
    xhci_ctrl_t *x = &g_xhci;
    mmio_w32(x->db, slot * 4, dci);
    (void)mmio_r32(x->db, slot * 4);
}

/* Slot Context / Endpoint Context 字段偏移基于 ctx_size 动态计算。
 * Input Context 布局：[Input Control Ctx][Slot Ctx][EP0 Ctx][EP1 OUT][EP1 IN]...
 * DCI（Device Context Index）：EP0=1，EPn 方向 IN=2n+1 / OUT=2n。 */
static inline uint32_t *xhci_ctx_at(uint8_t *base, uint32_t idx, uint32_t ctx_size) {
    return (uint32_t *)(base + idx * ctx_size);
}

/* 分配一个设备槽位；返回 xhci_dev_t* 或 NULL */
static xhci_dev_t *xhci_dev_alloc(void) {
    for (int i = 0; i < XHCI_MAX_DEVS; i++)
        if (!g_devs[i].used) { 
            for (uint32_t b = 0; b < sizeof(xhci_dev_t); b++) ((uint8_t*)&g_devs[i])[b] = 0;
            g_devs[i].used = 1; 
            return &g_devs[i]; 
        }
    return 0;
}

/* Enable Slot 命令 → 返回 slot_id（0 表示失败）*/
static uint32_t xhci_enable_slot(void) {
    uint64_t trb_phys = xhci_cmd_enqueue(0, 0, TRB_ENABLE_SLOT);
    xhci_ring_cmd_doorbell();
    uint32_t cc = 0, slot = 0;
    if (xhci_wait_cmd_complete(trb_phys, &cc, &slot) != 0) {
        XLOG("enable_slot: TIMEOUT");
        return 0;
    }
    if (cc != 1) { XLOG_NN("enable_slot: cc="); XDEC(cc); XNL(); return 0; }
    return slot;
}

/* 根据 PORTSC speed 字段推断 EP0 默认 MaxPacketSize（xHCI 4.3）*/
static uint32_t xhci_speed_to_mps0(uint32_t speed) {
    switch (speed) {
        case 1: return 64;   /* Full-Speed（暂以 64，少数设备为 8）*/
        case 2: return 8;    /* Low-Speed */
        case 3: return 64;   /* High-Speed */
        case 4: return 512;  /* SuperSpeed */
        default: return 64;
    }
}

/* 为设备分配 Input Context / Device Context / EP0 Ring，并初始化
 * Input Control Ctx（A0|A1）+ Slot Ctx + EP0 Ctx。返回 0 成功。 */
static int xhci_dev_init_context(xhci_dev_t *d) {
    xhci_ctrl_t *x = &g_xhci;
    uint32_t cs = x->ctx_size;

    /* Input Context = (2 + max_ep) 个 ctx；MVP 需 Input Control + Slot + EP0 + EP_IN，预留 8 个 */
    d->in_ctx_phys  = xhci_zalloc_pages(1);
    d->dev_ctx_phys = xhci_zalloc_pages(1);
    d->ep0_ring_phys = xhci_zalloc_pages(1);
    if (!d->in_ctx_phys || !d->dev_ctx_phys || !d->ep0_ring_phys) {
        XLOG("dev_ctx alloc FAIL");
        return -1;
    }
    /* 物理=虚拟恒等映射，直接当指针用 */
    d->in_ctx    = (uint8_t *)d->in_ctx_phys;
    d->dev_ctx   = (uint8_t *)d->dev_ctx_phys;
    d->ep0_ring  = (xhci_trb_t *)d->ep0_ring_phys;
    d->ep0_enqueue = 0;
    d->ep0_cycle   = 1;

    /* ---- Input Control Context（idx 0）：添加 A0(Slot)+A1(EP0) ---- */
    uint32_t *icc = xhci_ctx_at(d->in_ctx, 0, cs);
    icc[1] = (1u << 0) | (1u << 1);   /* Add Context flags: Slot + EP0 */

    /* ---- Slot Context（idx 1）---- */
    uint32_t *slot = xhci_ctx_at(d->in_ctx, 1, cs);
    /* Route String=0; Speed[23:20]; Context Entries[31:27]=1（仅 EP0）*/
    slot[0] = (d->speed << 20) | (1u << 27);
    /* Root Hub Port Number[23:16] */
    slot[1] = (d->port << 16);

    /* ---- EP0 Context（idx 2）---- */
    uint32_t *ep0 = xhci_ctx_at(d->in_ctx, 2, cs);
    uint32_t mps0 = xhci_speed_to_mps0(d->speed);
    /* EP Type[5:3]=4(Control); CErr[2:1]=3 */
    ep0[1] = (4u << 3) | (3u << 1);
    /* Max Packet Size[31:16] */
    ep0[1] |= (mps0 << 16);
    /* TR Dequeue Pointer + DCS(bit0)=1 */
    ep0[2] = (uint32_t)(d->ep0_ring_phys | 1);
    ep0[3] = (uint32_t)(d->ep0_ring_phys >> 32);
    /* Average TRB Length */
    ep0[4] = 8;

    /* 写入 DCBAA[slot] = Device Context 物理地址 */
    x->dcbaa[d->slot_id] = d->dev_ctx_phys;
    return 0;
}

/* Address Device 命令：把 Input Context 交给控制器，分配 USB 地址。返回 0 成功。 */
static int xhci_address_device(xhci_dev_t *d) {
    uint64_t trb_phys = xhci_cmd_enqueue_ex(d->in_ctx_phys, 0, TRB_ADDRESS_DEV, d->slot_id << 24);
    xhci_ring_cmd_doorbell();
    uint32_t cc = 0, slot = 0;
    if (xhci_wait_cmd_complete(trb_phys, &cc, &slot) != 0) {
        XLOG("address_device: TIMEOUT"); return -1;
    }
    if (cc != 1) { XLOG_NN("address_device: cc="); XDEC(cc); XNL(); return -1; }
    return 0;
}

/* ---- EP0 控制传输：Setup [+ Data] + Status 三阶段 ----
 * bmRequestType/bRequest/wValue/wIndex/wLength 为标准 setup 字段；
 * buf/buf_phys 为数据阶段缓冲（可为 NULL）。返回实际传输字节数（>=0）或 -1。 */
static int xhci_ep0_transfer(xhci_dev_t *d, uint8_t bmReqType, uint8_t bReq,
                             uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                             uint64_t buf_phys) {
    xhci_ctrl_t *x = &g_xhci;
    int is_in = (bmReqType & USB_REQTYPE_D2H) ? 1 : 0;
    int has_data = (wLength > 0);

    /* --- Setup Stage TRB --- */
    xhci_trb_t *t = &d->ep0_ring[d->ep0_enqueue];
    uint64_t setup_data = ((uint64_t)bmReqType)
                        | ((uint64_t)bReq    << 8)
                        | ((uint64_t)wValue  << 16)
                        | ((uint64_t)wIndex  << 32)
                        | ((uint64_t)wLength << 48);
    t->parameter = setup_data;
    t->status    = 8;   /* TRB Transfer Length = 8 */
    uint32_t trt = has_data ? (is_in ? TRB_TRT_IN : TRB_TRT_OUT) : TRB_TRT_NO_DATA;
    t->control   = (TRB_SETUP_STAGE << TRB_TYPE_SHIFT) | TRB_IDT
                 | (trt << 16) | d->ep0_cycle;
    d->ep0_enqueue++;

    /* --- Data Stage TRB（可选）--- */
    if (has_data) {
        xhci_trb_t *dt = &d->ep0_ring[d->ep0_enqueue];
        dt->parameter = buf_phys;
        dt->status    = wLength;
        dt->control   = (TRB_DATA_STAGE << TRB_TYPE_SHIFT)
                      | (is_in ? TRB_DIR_IN : 0) | TRB_ISP
                      | d->ep0_cycle;
        d->ep0_enqueue++;
    }

    /* --- Status Stage TRB（方向与 Data 相反，IOC）--- */
    xhci_trb_t *st = &d->ep0_ring[d->ep0_enqueue];
    uint64_t status_trb_phys = d->ep0_ring_phys + d->ep0_enqueue * sizeof(xhci_trb_t);
    st->parameter = 0;
    st->status    = 0;
    /* Status 方向：无数据阶段 → IN；有数据阶段 → 与 Data 反 */
    int status_in = has_data ? (is_in ? 0 : 1) : 1;
    st->control   = (TRB_STATUS_STAGE << TRB_TYPE_SHIFT)
                  | (status_in ? TRB_DIR_IN : 0) | TRB_IOC
                  | d->ep0_cycle;
    d->ep0_enqueue++;

    /* 敲 EP0 doorbell（DCI=1）*/
    xhci_ring_dev_doorbell(d->slot_id, 1);

    /* 等待 Status Stage 的 Transfer Event */
    uint32_t cc = 0, resid = 0;
    if (xhci_wait_event(TRB_TRANSFER_EVT, status_trb_phys, &cc, 0, &resid) != 0) {
        XLOG("ep0_transfer: TIMEOUT"); return -1;
    }
    if (cc != 1 && cc != 13 /* Short Packet */) {
        XLOG_NN("ep0_transfer: cc="); XDEC(cc); XNL();
        return -1;
    }
    (void)x;
    return (int)wLength;
}

/* 解析 Configuration Descriptor，寻找第一个 HID interface 及其 Interrupt IN 端点。
 * 成功填充 d->dev_class/proto/iface_num/ep_in_* 并返回 0。 */
static int xhci_parse_config(xhci_dev_t *d, uint8_t *buf, uint32_t total) {
    uint32_t i = 0;
    int found_hid = 0;
    while (i + 2 <= total) {
        uint8_t len  = buf[i];
        uint8_t type = buf[i + 1];
        if (len == 0) break;
        if (type == USB_DESC_INTERFACE && i + 9 <= total) {
            uint8_t if_class = buf[i + 5];
            uint8_t if_proto = buf[i + 7];
            if (if_class == USB_CLASS_HID) {
                d->dev_class  = if_class;
                d->proto      = if_proto;
                d->iface_num  = buf[i + 2];
                found_hid = 1;
            } else {
                found_hid = 0;  /* 非 HID 接口，后续端点忽略 */
            }
        } else if (type == USB_DESC_ENDPOINT && found_hid && i + 7 <= total) {
            uint8_t ep_addr = buf[i + 2];
            uint8_t ep_attr = buf[i + 3];
            uint16_t mps    = buf[i + 4] | (buf[i + 5] << 8);
            uint8_t interval = buf[i + 6];
            /* Interrupt(0x03) + IN(0x80) */
            if ((ep_attr & 0x03) == 0x03 && (ep_addr & 0x80)) {
                d->ep_in_addr     = ep_addr;
                d->ep_in_mps      = mps & 0x7FF;
                d->ep_in_interval = interval;
                return 0;  /* 找到 HID Interrupt IN 端点 */
            }
        }
        i += len;
    }
    return -1;
}

/* 完整枚举一个已复位、已 enabled 的端口上的设备。返回 xhci_dev_t*或 NULL。 */
static xhci_dev_t *xhci_enumerate_device(uint32_t port, uint32_t speed) {
    XLOG_NN("  enumerate: port="); XDEC(port); serial_write(" speed="); XDEC(speed); serial_write("\n");
    /* 1. Enable Slot */
    uint32_t slot = xhci_enable_slot();
    if (!slot) { XLOG("  enable_slot FAIL"); return 0; }
    XLOG_NN("  enable_slot OK slot="); XDEC(slot); serial_write("\n");

    xhci_dev_t *d = xhci_dev_alloc();
    if (!d) { XLOG("  dev table full"); return 0; }
    d->slot_id = slot;
    d->port    = port;
    d->speed   = speed;

    /* 2. 分配上下文 + Address Device */
    if (xhci_dev_init_context(d) != 0) { d->used = 0; return 0; }
    if (xhci_address_device(d)   != 0) { XLOG("  address_device FAIL"); d->used = 0; return 0; }
    XLOG_NN("  slot="); XDEC(slot); serial_write(" addressed OK\n");

    /* 3. GET_DESCRIPTOR(Device) — 前 18 字节 */
    uint64_t dma_phys = xhci_zalloc_pages(1);
    if (!dma_phys) { d->used = 0; return 0; }
    uint8_t *dma = (uint8_t *)dma_phys;

    int r = xhci_ep0_transfer(d, USB_REQTYPE_D2H | USB_REQTYPE_STD, USB_REQ_GET_DESCRIPTOR,
                              (USB_DESC_DEVICE << 8) | 0, 0, 18, dma_phys);
    if (r < 0) { XLOG("  GET device desc FAIL"); d->used = 0; return 0; }
    usb_device_descriptor_t *dd = (usb_device_descriptor_t *)dma;
    d->vid = dd->idVendor;
    d->pid = dd->idProduct;
    XLOG_NN("  device desc: VID="); XHEX(d->vid);
    serial_write(" PID="); XHEX(d->pid); serial_write("\n");

    /* 4. GET_DESCRIPTOR(Config) — 先取 9 字节 header 得 wTotalLength */
    r = xhci_ep0_transfer(d, USB_REQTYPE_D2H | USB_REQTYPE_STD, USB_REQ_GET_DESCRIPTOR,
                          (USB_DESC_CONFIG << 8) | 0, 0, 9, dma_phys);
    if (r < 0) { XLOG("  GET config hdr FAIL"); d->used = 0; return 0; }
    uint16_t total = dma[2] | (dma[3] << 8);
    d->config_val  = dma[5];
    if (total > 4096) total = 4096;

    /* 再取完整 config 描述符集合 */
    r = xhci_ep0_transfer(d, USB_REQTYPE_D2H | USB_REQTYPE_STD, USB_REQ_GET_DESCRIPTOR,
                          (USB_DESC_CONFIG << 8) | 0, 0, total, dma_phys);
    if (r < 0) { XLOG("  GET config full FAIL"); d->used = 0; return 0; }

    if (xhci_parse_config(d, dma, total) != 0) {
        XLOG("  no HID interrupt-IN endpoint (skip)"); d->used = 0; return 0;
    }
    XLOG_NN("  HID iface="); XDEC(d->iface_num);
    serial_write(" proto="); XDEC(d->proto);
    serial_write(" ep_in="); XHEX(d->ep_in_addr);
    serial_write(" mps="); XDEC(d->ep_in_mps); serial_write("\n");

    /* 5. SET_CONFIGURATION */
    r = xhci_ep0_transfer(d, USB_REQTYPE_H2D | USB_REQTYPE_STD, USB_REQ_SET_CONFIG,
                          d->config_val, 0, 0, 0);
    if (r < 0) { XLOG("  SET_CONFIG FAIL"); d->used = 0; return 0; }
    XLOG("  SET_CONFIG OK");

    return d;
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
        int enabled = 0;
        if (!(sc & PORTSC_PED)) {
            if (xhci_reset_port(p) == 0) {
                XLOG("  port reset + enabled OK");
                enabled = 1;
            } else {
                XLOG("  port reset done (not enabled)");
            }
        } else {
            XLOG("  port already enabled");
            enabled = 1;
        }

        /* 端口已 enabled → 枚举设备（重读 speed，复位后可能变化）*/
        if (enabled) {
            uint32_t sc2 = mmio_r32(x->op, off);
            uint32_t sp  = (sc2 >> PORTSC_SPEED_SHIFT) & PORTSC_SPEED_MASK;
            xhci_dev_t *dev = xhci_enumerate_device(p, sp);
            if (dev) {
                XLOG_NN("  ENUM OK: slot="); XDEC(dev->slot_id);
                serial_write(" class="); XDEC(dev->dev_class);
                serial_write(" proto="); XDEC(dev->proto); serial_write("\n");
            } else {
                XLOG("  ENUM skipped/failed");
            }
        }
    }
    XLOG_NN("enumerated ports: connected="); XDEC(x->connected);
    serial_write("/"); XDEC(x->max_ports); serial_write("\n");
}

/* ============================================================
 * M2.3 Step3-4: HID 层 xHCI 传输原语（供 usb_hid64.c 调用）
 * ============================================================ */

#define XHCI_HID_IS(d)  ((d)->used && (d)->dev_class == 3 && \
                         ((d)->proto == 1 || (d)->proto == 2))

/* 把第 idx 个 HID 设备映射到 g_devs 下标；返回 -1 表示越界。 */
static int xhci_hid_index(uint32_t idx) {
    uint32_t n = 0;
    for (int i = 0; i < XHCI_MAX_DEVS; i++) {
        xhci_dev_t *d = &g_devs[i];
        if (XHCI_HID_IS(d)) {
            if (n == idx) return i;
            n++;
        }
    }
    return -1;
}

uint32_t xhci_hid_device_count(void) {
    uint32_t n = 0;
    for (int i = 0; i < XHCI_MAX_DEVS; i++)
        if (XHCI_HID_IS(&g_devs[i])) n++;
    return n;
}

uint32_t xhci_hid_device_proto(uint32_t idx) {
    int i = xhci_hid_index(idx);
    return (i < 0) ? 0 : g_devs[i].proto;
}

uint32_t xhci_hid_device_report_len(uint32_t idx) {
    int i = xhci_hid_index(idx);
    if (i < 0) return 0;
    uint32_t mps = g_devs[i].ep_in_mps;
    return (mps && mps <= 64) ? mps : 8;
}

/* 前向声明：configure 需要先于 arm 定义位置被引用 */
static void xhci_hid_arm(xhci_dev_t *d);

/* 投递一个 Normal TRB 到 Interrupt-IN 环并敲门铃，等待下一个 report。 */
static void xhci_hid_arm(xhci_dev_t *d) {
    uint32_t rlen = d->ep_in_mps ? d->ep_in_mps : 8;
    if (rlen > 64) rlen = 64;

    xhci_trb_t *trb = &d->ep_in_ring[d->ep_in_enqueue];
    trb->parameter = d->hid_buf_phys;
    trb->status    = rlen;                 /* TRB Transfer Length */
    /* Normal TRB：IOC=1（完成上报事件），ISP=1（短包也上报） */
    trb->control = (TRB_NORMAL << TRB_TYPE_SHIFT) | TRB_IOC | TRB_ISP | d->ep_in_cycle;

    /* 推进 enqueue（末槽为 Link，绕过） */
    d->ep_in_enqueue++;
    if (d->ep_in_enqueue >= EPIN_RING_TRBS - 1) {
        /* 翻转 Link TRB 的 cycle，使其对消费者可见 */
        xhci_trb_t *link = &d->ep_in_ring[EPIN_RING_TRBS - 1];
        link->control = (link->control & ~1u) | d->ep_in_cycle;
        d->ep_in_enqueue = 0;
        d->ep_in_cycle  ^= 1;
    }

    /* 敲设备 doorbell：target = DCI = 2*ep_num + 1 */
    uint32_t ep_num = d->ep_in_addr & 0x0F;
    xhci_ring_dev_doorbell(d->slot_id, ep_num * 2 + 1);
}

/* 为第 idx 个 HID 设备配置 Interrupt-IN 端点：
 *   建 Transfer Ring → 构建 Input Context（EP_IN Context）
 *   → Configure Endpoint 命令 → SET_PROTOCOL(boot) → 投递首个 Normal TRB。
 * 返回 0 成功，负数失败。幂等（已配置直接返回 0）。 */
int xhci_hid_configure(uint32_t idx) {
    int di = xhci_hid_index(idx);
    if (di < 0) return -1;
    xhci_dev_t *d = &g_devs[di];
    if (d->ep_in_ring) return 0;           /* 已配置，幂等 */
    if (!d->ep_in_addr) return -2;         /* 无 Interrupt IN 端点 */

    xhci_ctrl_t *x = &g_xhci;
    uint32_t cs = x->ctx_size;

    /* 1) 分配 Interrupt-IN Transfer Ring + HID DMA 缓冲（恒等映射） */
    uint64_t ring_phys = xhci_zalloc_pages(1);
    uint64_t buf_phys  = xhci_zalloc_pages(1);
    if (!ring_phys || !buf_phys) return -3;
    d->ep_in_ring      = (xhci_trb_t *)ring_phys;
    d->ep_in_ring_phys = ring_phys;
    d->ep_in_enqueue   = 0;
    d->ep_in_cycle     = 1;
    d->hid_buf         = (uint8_t *)buf_phys;
    d->hid_buf_phys    = buf_phys;

    /* Transfer Ring 末尾放 Link TRB 回绕到环首（TC=1） */
    xhci_trb_t *link = &d->ep_in_ring[EPIN_RING_TRBS - 1];
    link->parameter = d->ep_in_ring_phys;
    link->status    = 0;
    link->control   = (TRB_LINK << TRB_TYPE_SHIFT) | TRB_LINK_TC | 1u;

    /* 2) DCI = 2*ep_num + 1（IN 方向） */
    uint32_t ep_num = d->ep_in_addr & 0x0F;
    uint32_t dci    = ep_num * 2 + 1;

    /* 3) 构建 Input Context：清零 → A0(slot)+A[dci] */
    uint8_t *ic = d->in_ctx;
    for (uint32_t i = 0; i < (dci + 1) * cs; i++) ic[i] = 0;
    uint32_t *icc = xhci_ctx_at(ic, 0, cs);
    icc[1] = (1u << 0) | (1u << dci);      /* Add: Slot Context + EP_IN */

    /* Slot Context：更新 Context Entries = dci */
    uint32_t *slot  = xhci_ctx_at(ic, 1, cs);
    uint32_t *dslot = (uint32_t *)d->dev_ctx;
    slot[0] = (dslot[0] & ~(0x1Fu << 27)) | (dci << 27);
    slot[1] = dslot[1];

    /* EP_IN Context：Interrupt IN（EPType=7），CErr=3 */
    uint32_t *ep  = xhci_ctx_at(ic, dci, cs);
    uint32_t mps  = d->ep_in_mps ? d->ep_in_mps : 8;
    uint32_t itv  = d->ep_in_interval ? d->ep_in_interval : 7;
    ep[0] = (itv << 16);
    ep[1] = (3u << 1) | (7u << 3) | (mps << 16);
    ep[2] = (uint32_t)(d->ep_in_ring_phys | 1u);   /* TR Dequeue + DCS=1 */
    ep[3] = (uint32_t)(d->ep_in_ring_phys >> 32);
    ep[4] = mps;                                   /* Average TRB Length */

    /* 4) Configure Endpoint 命令 */
    uint32_t cc = 0, cslot = 0;
    uint64_t cmd = xhci_cmd_enqueue_ex(d->in_ctx_phys, 0,
                                       TRB_CONFIG_EP, d->slot_id << 24);
    xhci_ring_cmd_doorbell();
    if (xhci_wait_cmd_complete(cmd, &cc, &cslot) != 0) return -4;

    /* 5) SET_PROTOCOL(boot=0)：bmReqType=0x21 Class|Interface，bReq=0x0B */
    (void)xhci_ep0_transfer(d, 0x21, 0x0B, 0, d->iface_num, 0, 0);

    /* 6) 投递首个 Normal TRB 等待 report */
    xhci_hid_arm(d);
    return 0;
}

/* 非阻塞消费一个事件：仅命中属于 (slot_id, ep_dci) 的 Transfer Event。
 * 命中返回 residual(>=0)；无匹配事件（或事件环空）返回 -1。
 * 途中遇到的其它类型事件一律消费跳过（避免事件环阻塞）。 */
static int xhci_poll_transfer_event(uint32_t slot_id, uint32_t ep_dci) {
    xhci_ctrl_t *x = &g_xhci;
    /* 单次扫描：最多消费本轮已产生的连续事件 */
    for (uint32_t guard = 0; guard < EVT_RING_TRBS; guard++) {
        volatile xhci_trb_t *evt = &x->evt_ring[x->evt_dequeue];
        uint32_t c = evt->control;
        int cyc = (c & TRB_CYCLE) ? 1 : 0;
        if (cyc != (int)x->evt_cycle) return -1;   /* 无新事件 */

        uint32_t type = (c >> TRB_TYPE_SHIFT) & 0x3F;
        uint32_t eslot = TRB_GET_SLOT(evt);
        uint32_t eep   = (c >> 16) & 0x1F;         /* Endpoint ID = DCI */
        int resid = (int)(evt->status & 0xFFFFFF);
        int hit = (type == TRB_TRANSFER_EVT && eslot == slot_id && eep == ep_dci);

        /* 消费事件 + 更新 ERDP */
        x->evt_dequeue++;
        if (x->evt_dequeue >= EVT_RING_TRBS) {
            x->evt_dequeue = 0;
            x->evt_cycle ^= 1;
        }
        uint64_t erdp = x->evt_ring_phys + x->evt_dequeue * sizeof(xhci_trb_t);
        mmio_w64(x->rt, XRT_ERDP(0), erdp | ERDP_EHB);

        if (hit) return resid;
    }
    return -1;
}

/* 非阻塞探测第 idx 个 HID 设备的 Interrupt-IN 传输：
 *   命中一个 report → 拷贝到 out_buf 并重新投递 → 返回字节数(>0)
 *   无事件 → 0；出错 → 负数。 */
int xhci_hid_poll(uint32_t idx, uint8_t *out_buf, uint32_t out_cap) {
    int di = xhci_hid_index(idx);
    if (di < 0) return -1;
    xhci_dev_t *d = &g_devs[di];
    if (!d->ep_in_ring) return -2;         /* 未配置 */

    uint32_t ep_num = d->ep_in_addr & 0x0F;
    uint32_t want_dci = ep_num * 2 + 1;

    int resid = xhci_poll_transfer_event(d->slot_id, want_dci);
    if (resid < 0) return 0;               /* 无匹配事件 */

    /* 实际长度 = 期望长度 - residual */
    uint32_t rlen = d->ep_in_mps ? d->ep_in_mps : 8;
    if (rlen > 64) rlen = 64;
    uint32_t n = (rlen > (uint32_t)resid) ? (rlen - (uint32_t)resid) : 0;
    if (n > out_cap) n = out_cap;
    for (uint32_t i = 0; i < n; i++) out_buf[i] = d->hid_buf[i];

    xhci_hid_arm(d);                       /* 重新武装下一个传输 */
    return (int)n;
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
