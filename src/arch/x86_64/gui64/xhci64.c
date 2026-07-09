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

static void xhci_dispatch_hid_event(volatile xhci_trb_t *evt);
static int xhci_dev_by_slot(uint32_t slot_id);
static int xhci_wait_event(uint32_t want_type, uint64_t trb_phys,
                           uint32_t *out_cc, uint32_t *out_slot, uint32_t *out_resid) {
    xhci_ctrl_t *x = &g_xhci;
    for (uint32_t spin = 0; spin < 20000000; spin++) {
        volatile xhci_trb_t *evt = &x->evt_ring[x->evt_dequeue];
        uint32_t c = evt->control;
        int cyc = (c & TRB_CYCLE) ? 1 : 0;
        if (cyc == (int)x->evt_cycle) {
            uint32_t type = (c >> TRB_TYPE_SHIFT) & 0x3F;
            int hit = (type == want_type && evt->parameter == trb_phys);
            if (type == TRB_TRANSFER_EVT) {
                extern void early_console64_write(const char*);
                extern void early_console64_write_hex64(unsigned long long);
                early_console64_write("[wev] par="); early_console64_write_hex64(evt->parameter);
                early_console64_write(" slot="); early_console64_write_hex64(TRB_GET_SLOT(evt));
                early_console64_write(" ep="); early_console64_write_hex64((evt->control>>16)&0x1F);
                early_console64_write(" cc="); early_console64_write_hex64(TRB_GET_CC(evt));
                early_console64_write("\n");
            }
            if (hit) {
                if (out_cc)    *out_cc    = TRB_GET_CC(evt);
                if (out_slot)  *out_slot  = TRB_GET_SLOT(evt);
                if (out_resid) *out_resid = evt->status & 0xFFFFFF;
            } else if (type == TRB_TRANSFER_EVT) {
                /* 非目标的 Transfer Event（通常是 HID Interrupt-IN 完成）：
                 * 不能放弃返回(否则与本次等待的端点事件竞争会互相卡死)，
                 * 也不能丢弃(否则 HID report 永久丢失)。
                 * 就地分发给对应 HID 设备(拷 report + re-arm)，然后消费该 TRB
                 * 继续 spin 等待本次真正的目标 event。 */
                xhci_dispatch_hid_event(evt);
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
            __asm__ volatile("pause");
        }
    }
    { extern void early_console64_write(const char*);
      extern void early_console64_write_hex64(unsigned long long);
      volatile xhci_trb_t *evt = &x->evt_ring[x->evt_dequeue];
      early_console64_write("[wait-TO] deq="); early_console64_write_hex64(x->evt_dequeue);
      early_console64_write(" ecyc="); early_console64_write_hex64(x->evt_cycle);
      early_console64_write(" ctl="); early_console64_write_hex64(evt->control);
      early_console64_write(" par="); early_console64_write_hex64(evt->parameter);
      early_console64_write(" want="); early_console64_write_hex64(trb_phys);
      early_console64_write("\n");
      /* 扫描整个 event ring，打印所有 Transfer Event 的 parameter */
      for (uint32_t s = 0; s < EVT_RING_TRBS; s++) {
          volatile xhci_trb_t *e = &x->evt_ring[s];
          uint32_t t = (e->control >> TRB_TYPE_SHIFT) & 0x3F;
          if (t == TRB_TRANSFER_EVT) {
              early_console64_write("[scan] idx="); early_console64_write_hex64(s);
              early_console64_write(" par="); early_console64_write_hex64(e->parameter);
              early_console64_write(" slot="); early_console64_write_hex64(TRB_GET_SLOT(e));
              early_console64_write(" ep="); early_console64_write_hex64((e->control>>16)&0x1F);
              early_console64_write("\n");
          }
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
    /* 全局事件泵分发的待取 report（每设备一个待取槽）*/
    uint8_t     rpt_data[64];
    uint32_t    rpt_len;      /* 待取 report 字节数 */
    volatile int rpt_ready;   /* 1=有新 report 待 poll 取走 */

    /* ---- M2.3 Mass Storage(BOT) 扩展：Bulk IN/OUT 端点 ---- */
    uint8_t  is_msc;          /* 1=此接口为 USB Mass Storage(BOT) */
    uint8_t  ep_bulk_in;      /* Bulk IN 端点地址（含方向位 0x80） */
    uint8_t  ep_bulk_out;     /* Bulk OUT 端点地址 */
    uint16_t ep_bulk_in_mps;  /* Bulk IN MaxPacketSize */
    uint16_t ep_bulk_out_mps; /* Bulk OUT MaxPacketSize */
    /* Bulk IN Transfer Ring */
    xhci_trb_t *ep_bin_ring;
    uint64_t    ep_bin_ring_phys;
    uint32_t    ep_bin_enqueue;
    uint32_t    ep_bin_cycle;
    /* Bulk OUT Transfer Ring */
    xhci_trb_t *ep_bout_ring;
    uint64_t    ep_bout_ring_phys;
    uint32_t    ep_bout_enqueue;
    uint32_t    ep_bout_cycle;
    /* ---- MSC 事件信箱：全局事件泵按 slot/dci 投递 bulk 完成事件 ----
     * 修复双消费者竞争：HID poll 的 xhci_pump_events 会读走并丢弃
     * bulk(epid4) transfer event，导致 MSC 的 wait_event 永远等不到。
     * 现在 pump 遇到本设备的 bulk 事件时投递到此信箱，不再丢弃。*/
    volatile int msc_evt_ready;   /* 1=有一个待取的 bulk 完成事件 */
    uint32_t     msc_evt_dci;     /* 事件对应的端点 DCI（3=bulk in, 4=bulk out）*/
    uint32_t     msc_evt_cc;      /* completion code */
    uint32_t     msc_evt_resid;   /* 剩余字节数（EDTLA）*/
    uint64_t     msc_evt_par;     /* 事件 parameter（完成的 TRB 物理地址）*/
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

/* 解析 Configuration Descriptor：
 *  - HID interface：填 d->proto/ep_in_*（Interrupt IN）
 *  - Mass Storage(BOT) interface：填 d->is_msc/ep_bulk_in/ep_bulk_out
 * 成功返回 0。 */
static int xhci_parse_config(xhci_dev_t *d, uint8_t *buf, uint32_t total) {
    uint32_t i = 0;
    int found_hid = 0;
    int in_msc = 0;   /* 当前处于 MSC 接口的端点扫描中 */
    int msc_done = 0; /* MSC bulk in/out 均已集齐 */
    while (i + 2 <= total) {
        uint8_t len  = buf[i];
        uint8_t type = buf[i + 1];
        if (len == 0) break;
        if (type == USB_DESC_INTERFACE && i + 9 <= total) {
            uint8_t if_class = buf[i + 5];
            uint8_t if_sub   = buf[i + 6];
            uint8_t if_proto = buf[i + 7];
            if (if_class == USB_CLASS_HID) {
                d->dev_class  = if_class;
                d->proto      = if_proto;
                d->iface_num  = buf[i + 2];
                found_hid = 1;
                in_msc = 0;
            } else if (if_class == 0x08 /* Mass Storage */ &&
                       if_proto == 0x50 /* Bulk-Only Transport */) {
                /* SubClass 0x06=SCSI transparent；部分 U 盘报 0x05/0x01，均按 SCSI 处理 */
                d->dev_class = if_class;
                d->proto     = if_proto;
                d->iface_num = buf[i + 2];
                d->is_msc    = 1;
                (void)if_sub;
                found_hid = 0;
                in_msc = 1;
            } else {
                found_hid = 0;
                in_msc = 0;  /* 其他接口，后续端点忽略 */
            }
        } else if (type == USB_DESC_ENDPOINT && i + 7 <= total) {
            uint8_t ep_addr = buf[i + 2];
            uint8_t ep_attr = buf[i + 3];
            uint16_t mps    = buf[i + 4] | (buf[i + 5] << 8);
            uint8_t interval = buf[i + 6];
            if (found_hid) {
                /* Interrupt(0x03) + IN(0x80) */
                if ((ep_attr & 0x03) == 0x03 && (ep_addr & 0x80)) {
                    d->ep_in_addr     = ep_addr;
                    d->ep_in_mps      = mps & 0x7FF;
                    d->ep_in_interval = interval;
                    return 0;  /* 找到 HID Interrupt IN 端点 */
                }
            } else if (in_msc) {
                /* Bulk(0x02) 端点：按方向拆 IN/OUT */
                if ((ep_attr & 0x03) == 0x02) {
                    if (ep_addr & 0x80) {
                        d->ep_bulk_in     = ep_addr;
                        d->ep_bulk_in_mps = mps & 0x7FF;
                    } else {
                        d->ep_bulk_out     = ep_addr;
                        d->ep_bulk_out_mps = mps & 0x7FF;
                    }
                    if (d->ep_bulk_in && d->ep_bulk_out) msc_done = 1;
                }
            }
        }
        i += len;
    }
    if (msc_done) return 0;
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

    /* [dbg] dump raw config descriptor bytes */
    XLOG_NN("  cfg raw total="); XDEC(total); early_serial64_write(" :");
    {
        uint32_t _n = total; if (_n > 60) _n = 60;
        for (uint32_t _k = 0; _k < _n; _k++) { early_serial64_write(" "); XHEX(dma[_k]); }
        early_serial64_write("\n");
    }

    if (xhci_parse_config(d, dma, total) != 0) {
        XLOG("  no HID/MSC endpoint (skip)"); d->used = 0; return 0;
    }
    if (d->is_msc) {
        XLOG_NN("  MSC iface="); XDEC(d->iface_num);
        serial_write(" ep_bulk_in="); XHEX(d->ep_bulk_in);
        serial_write(" ep_bulk_out="); XHEX(d->ep_bulk_out);
        serial_write(" in_mps="); XDEC(d->ep_bulk_in_mps);
        serial_write(" out_mps="); XDEC(d->ep_bulk_out_mps); serial_write("\n");
    } else {
        XLOG_NN("  HID iface="); XDEC(d->iface_num);
        serial_write(" proto="); XDEC(d->proto);
        serial_write(" ep_in="); XHEX(d->ep_in_addr);
        serial_write(" mps="); XDEC(d->ep_in_mps); serial_write("\n");
    }

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
    { extern void early_console64_write_hex64(unsigned long long);
      extern void early_console64_write(const char*);
      early_console64_write("[arm] slot="); early_console64_write_hex64(d->slot_id);
      early_console64_write(" enq="); early_console64_write_hex64(d->ep_in_enqueue);
      early_console64_write(" cyc="); early_console64_write_hex64(d->ep_in_cycle);
      early_console64_write("\n"); }
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

/* 将一个已确认为 Transfer Event 的 TRB 分发给对应 HID 设备：
 * 命中某设备的 Interrupt-IN 端点 → 拷贝 report 到 rpt 缓冲 + 置 ready + re-arm。
 * 供 xhci_wait_event 在等待其它端点(如 MSC bulk)时顺带消费 HID 事件，避免丢报文。 */
static void xhci_dispatch_hid_event(volatile xhci_trb_t *evt) {
    uint32_t c     = evt->control;
    uint32_t eslot = TRB_GET_SLOT(evt);
    uint32_t eep   = (c >> 16) & 0x1F;
    int      resid = (int)(evt->status & 0xFFFFFF);

    int di = xhci_dev_by_slot(eslot);
    if (di < 0) return;
    xhci_dev_t *d = &g_devs[di];
    if (!d->ep_in_ring) return;
    uint32_t want_dci = (d->ep_in_addr & 0x0F) * 2 + 1;
    if (eep != want_dci) return;               /* 非该设备 Interrupt-IN */

    uint32_t rlen = d->ep_in_mps ? d->ep_in_mps : 8;
    if (rlen > sizeof(d->rpt_data)) rlen = sizeof(d->rpt_data);
    uint32_t n = (rlen > (uint32_t)resid) ? (rlen - (uint32_t)resid) : 0;
    if (n > sizeof(d->rpt_data)) n = sizeof(d->rpt_data);
    for (uint32_t i = 0; i < n; i++) d->rpt_data[i] = d->hid_buf[i];
    d->rpt_len   = n;
    d->rpt_ready = 1;
    xhci_hid_arm(d);                           /* 重新武装下一个中断传输 */
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
    ep[2] = (uint32_t)((d->ep_in_ring_phys & 0xFFFFFFF0u) | 1u);   /* TR Dequeue Ptr Lo (bit0=DCS) */
    ep[3] = (uint32_t)(d->ep_in_ring_phys >> 32);                 /* TR Dequeue Ptr Hi */
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

/* 为第 idx 个 MSC 设备配置 Bulk IN + Bulk OUT 端点：
 *   建两个 Transfer Ring → 一次 Configure Endpoint 命令加入两个 EP。
 * 返回 0 成功，负数失败。幂等。 */
static int xhci_msc_index(uint32_t idx);   /* 前向声明（定义在文件后半） */
int xhci_msc_configure(uint32_t idx) {
    int di = xhci_msc_index(idx);
    if (di < 0) return -1;
    xhci_dev_t *d = &g_devs[di];
    if (d->ep_bin_ring) return 0;              /* 已配置，幂等 */
    if (!d->ep_bulk_in || !d->ep_bulk_out) return -2;

    xhci_ctrl_t *x = &g_xhci;
    uint32_t cs = x->ctx_size;

    /* 1) 分配两个 Bulk Transfer Ring（恒等映射） */
    uint64_t rin  = xhci_zalloc_pages(1);
    uint64_t rout = xhci_zalloc_pages(1);
    if (!rin || !rout) return -3;
    d->ep_bin_ring       = (xhci_trb_t *)rin;
    d->ep_bin_ring_phys  = rin;
    d->ep_bin_enqueue    = 0;
    d->ep_bin_cycle      = 1;
    d->ep_bout_ring      = (xhci_trb_t *)rout;
    d->ep_bout_ring_phys = rout;
    d->ep_bout_enqueue   = 0;
    d->ep_bout_cycle     = 1;

    /* Link TRB 回绕（TC=1） */
    xhci_trb_t *lin = &d->ep_bin_ring[EPIN_RING_TRBS - 1];
    lin->parameter = d->ep_bin_ring_phys;
    lin->status = 0;
    lin->control = (TRB_LINK << TRB_TYPE_SHIFT) | TRB_LINK_TC | 1u;
    xhci_trb_t *lout = &d->ep_bout_ring[EPIN_RING_TRBS - 1];
    lout->parameter = d->ep_bout_ring_phys;
    lout->status = 0;
    lout->control = (TRB_LINK << TRB_TYPE_SHIFT) | TRB_LINK_TC | 1u;

    /* 2) DCI：IN=2*ep+1, OUT=2*ep */
    uint32_t in_ep   = d->ep_bulk_in & 0x0F;
    uint32_t out_ep  = d->ep_bulk_out & 0x0F;
    uint32_t dci_in  = in_ep * 2 + 1;
    uint32_t dci_out = out_ep * 2;
    uint32_t dci_max = dci_in > dci_out ? dci_in : dci_out;

    /* 3) Input Context：清零 → A0(slot)+A[dci_in]+A[dci_out] */
    uint8_t *ic = d->in_ctx;
    for (uint32_t i = 0; i < (dci_max + 1) * cs; i++) ic[i] = 0;
    uint32_t *icc = xhci_ctx_at(ic, 0, cs);
    icc[1] = (1u << 0) | (1u << dci_in) | (1u << dci_out);

    /* Slot Context：Context Entries = dci_max */
    uint32_t *slot  = xhci_ctx_at(ic, 1, cs);
    uint32_t *dslot = (uint32_t *)d->dev_ctx;
    slot[0] = (dslot[0] & ~(0x1Fu << 27)) | (dci_max << 27);
    slot[1] = dslot[1];

    /* Bulk IN Context：EPType=6(Bulk IN)，CErr=3 */
    uint32_t mps_in = d->ep_bulk_in_mps ? d->ep_bulk_in_mps : 512;
    uint32_t *epi = xhci_ctx_at(ic, dci_in + 1, cs);  /* Input Ctx: EP 槽位 = dci+1 (ICC占idx0) */
    epi[0] = 0;
    epi[1] = (3u << 1) | (6u << 3) | (mps_in << 16);
    epi[2] = (uint32_t)((d->ep_bin_ring_phys & 0xFFFFFFF0u) | 1u);
    epi[3] = (uint32_t)(d->ep_bin_ring_phys >> 32);
    epi[4] = mps_in;

    /* Bulk OUT Context：EPType=2(Bulk OUT)，CErr=3 */
    uint32_t mps_out = d->ep_bulk_out_mps ? d->ep_bulk_out_mps : 512;
    uint32_t *epo = xhci_ctx_at(ic, dci_out + 1, cs);  /* Input Ctx: EP 槽位 = dci+1 (ICC占idx0) */
    epo[0] = 0;
    epo[1] = (3u << 1) | (2u << 3) | (mps_out << 16);
    epo[2] = (uint32_t)((d->ep_bout_ring_phys & 0xFFFFFFF0u) | 1u);
    epo[3] = (uint32_t)(d->ep_bout_ring_phys >> 32);
    epo[4] = mps_out;

    /* 4) Configure Endpoint 命令 */
    uint32_t cc = 0, cslot = 0;
    uint64_t cmd = xhci_cmd_enqueue_ex(d->in_ctx_phys, 0,
                                       TRB_CONFIG_EP, d->slot_id << 24);
    xhci_ring_cmd_doorbell();
    int wr = xhci_wait_cmd_complete(cmd, &cc, &cslot);
    { extern void early_console64_write(const char*);
      extern void early_console64_write_hex64(unsigned long long);
      early_console64_write("[msc-cfg] wr="); early_console64_write_hex64((unsigned long long)(long long)wr);
      early_console64_write(" cc="); early_console64_write_hex64(cc);
      early_console64_write(" dci_in="); early_console64_write_hex64(dci_in);
      early_console64_write(" dci_out="); early_console64_write_hex64(dci_out);
      early_console64_write(" bin_phys="); early_console64_write_hex64(d->ep_bin_ring_phys);
      early_console64_write(" bout_phys="); early_console64_write_hex64(d->ep_bout_ring_phys);
      early_console64_write("\n"); }
    if (wr != 0) return -4;
    if (cc != 1) return -5;   /* 1=Success */

    /* 回读 dev_ctx 里 QEMU 拷贝后的 EP OUT context：验证 dequeue pointer 是否 = 我们的 ring */
    { extern void early_console64_write(const char*);
      extern void early_console64_write_hex64(unsigned long long);
      uint32_t *depo = xhci_ctx_at((uint8_t*)d->dev_ctx, dci_out, cs);
      uint32_t *depi = xhci_ctx_at((uint8_t*)d->dev_ctx, dci_in, cs);
      early_console64_write("[msc-devctx] OUT[0]="); early_console64_write_hex64(depo[0]);
      early_console64_write(" OUT[1]="); early_console64_write_hex64(depo[1]);
      early_console64_write(" OUT[2]="); early_console64_write_hex64(depo[2]);
      early_console64_write(" OUT[3]="); early_console64_write_hex64(depo[3]);
      early_console64_write(" IN[2]="); early_console64_write_hex64(depi[2]);
      early_console64_write("\n"); }
    return 0;
}

/* 同步 Bulk 传输：向第 idx 个 MSC 设备的 bulk 端点发/收数据。
 *   dir_in=1 表示 Bulk IN（设备→主机），=0 表示 Bulk OUT。
 * 返回实际传输字节数(>=0) 或负数错误。 */
/* 前向声明：MSC bulk 传输需要主动泵事件（定义在文件后部的 HID 事件泵） */
static void xhci_pump_events(void);

int xhci_msc_bulk_transfer(uint32_t idx, int dir_in, uint64_t buf_phys, uint32_t len) {
    int di = xhci_msc_index(idx);
    if (di < 0) return -1;
    xhci_dev_t *d = &g_devs[di];
    if (!d->ep_bin_ring || !d->ep_bout_ring) return -2;

    /* 选端点环与 DCI */
    xhci_trb_t *ring;
    uint32_t *enq_p, *cyc_p, ring_phys_dci, ep_addr;
    uint64_t ring_phys;
    if (dir_in) {
        ring = d->ep_bin_ring; enq_p = &d->ep_bin_enqueue;
        cyc_p = &d->ep_bin_cycle; ring_phys = d->ep_bin_ring_phys;
        ep_addr = d->ep_bulk_in & 0x0F;
        ring_phys_dci = ep_addr * 2 + 1;
    } else {
        ring = d->ep_bout_ring; enq_p = &d->ep_bout_enqueue;
        cyc_p = &d->ep_bout_cycle; ring_phys = d->ep_bout_ring_phys;
        ep_addr = d->ep_bulk_out & 0x0F;
        ring_phys_dci = ep_addr * 2;
    }

    uint32_t enq = *enq_p;
    uint32_t cyc = *cyc_p;
    /* 避开末尾 Link TRB（索引 EPIN_RING_TRBS-1）；如到达则回绕到 0 并翻转 cycle */
    if (enq >= EPIN_RING_TRBS - 1) { enq = 0; cyc ^= 1; }

    /* 构建 Normal TRB */
    xhci_trb_t *trb = &ring[enq];
    trb->parameter = buf_phys;
    trb->status    = len & 0x1FFFFu;           /* TRB Transfer Length */
    trb->control   = (TRB_NORMAL << TRB_TYPE_SHIFT) | TRB_IOC | TRB_ISP |
                     (cyc ? 1u : 0u);
    uint64_t trb_phys = ring_phys + (uint64_t)enq * sizeof(xhci_trb_t);

    /* 推进队列指针 */
    enq++;
    *enq_p = enq;
    *cyc_p = cyc;

    /* 敲端点门铃 */
    { extern void early_console64_write(const char*);
      extern void early_console64_write_hex64(unsigned long long);
      early_console64_write("[msc-bulk] dir_in="); early_console64_write_hex64(dir_in);
      early_console64_write(" dci="); early_console64_write_hex64(ring_phys_dci);
      early_console64_write(" enq="); early_console64_write_hex64(enq - 1);
      early_console64_write(" cyc="); early_console64_write_hex64(cyc);
      early_console64_write(" trb_phys="); early_console64_write_hex64(trb_phys);
      early_console64_write(" len="); early_console64_write_hex64(len);
      early_console64_write("\n");
      volatile uint32_t *tp = (volatile uint32_t *)trb;
      early_console64_write("[trb-mem] w0="); early_console64_write_hex64(tp[0]);
      early_console64_write(" w1="); early_console64_write_hex64(tp[1]);
      early_console64_write(" w2="); early_console64_write_hex64(tp[2]);
      early_console64_write(" w3="); early_console64_write_hex64(tp[3]);
      early_console64_write(" ring_phys="); early_console64_write_hex64(d->ep_bout_ring_phys);
      early_console64_write(" bin_phys="); early_console64_write_hex64(d->ep_bin_ring_phys);
      early_console64_write("\n"); }
    xhci_ring_dev_doorbell(d->slot_id, ring_phys_dci);

    /* 等待 Transfer Event —— 【修复双消费者竞争】
     * 不再用 xhci_wait_event 按 parameter 匹配（QEMU 报告的完成 TRB 地址
     * 与内核 TRB 地址空间可能不一致，且 HID poll 的 xhci_pump_events 会抢先
     * 读走并丢弃 bulk 事件）。改为主动泵事件，让 pump 把本设备 bulk 事件
     * 投递到 d->msc_evt_* 信箱，再从信箱取结果。BOT 单设备严格串行，
     * 同一时刻只有一个 bulk 传输在途，按 slot+dci 匹配即可。 */
    d->msc_evt_ready = 0;                       /* 清旧事件 */
    uint32_t cc = 0, resid = 0;
    int got = 0;
    for (uint32_t spin = 0; spin < 2000000u; spin++) {
        xhci_pump_events();
        if (d->msc_evt_ready && d->msc_evt_dci == ring_phys_dci) {
            cc    = d->msc_evt_cc;
            resid = d->msc_evt_resid;
            d->msc_evt_ready = 0;
            got = 1;
            break;
        }
    }
    { extern void early_console64_write(const char*);
      extern void early_console64_write_hex64(unsigned long long);
      early_console64_write("[msc-bulk] wait got="); early_console64_write_hex64((unsigned long long)got);
      early_console64_write(" cc="); early_console64_write_hex64(cc);
      early_console64_write(" resid="); early_console64_write_hex64(resid);
      early_console64_write("\n"); }
    if (!got) return -3;
    if (cc != 1 && cc != 13) return -4;         /* 1=Success, 13=Short Packet */
    /* resid = 未传字节，实际传输 = len - resid */
    uint32_t transferred = (resid <= len) ? (len - resid) : len;
    return (int)transferred;
}

/* 通过 slot_id 查设备索引；未找到返回 -1 */
static int xhci_dev_by_slot(uint32_t slot_id) {
    for (int i = 0; i < XHCI_MAX_DEVS; i++)
        if (g_devs[i].used && g_devs[i].slot_id == slot_id) return i;
    return -1;
}

/* 全局事件泵：一次性排空事件环，将每个 HID Transfer Event 按
 * (slot_id, ep) 分发到对应设备的 report 缓冲并立即重新武装。
 * 绝不丢弃/偷走其它设备的事件。非 Transfer 事件消费跳过。
 * 由 xhci_hid_poll 触发（幂等，环空则立即返回）。 */
static void xhci_pump_events(void) {
    xhci_ctrl_t *x = &g_xhci;
    for (uint32_t guard = 0; guard < EVT_RING_TRBS * 2; guard++) {
        volatile xhci_trb_t *evt = &x->evt_ring[x->evt_dequeue];
        uint32_t c = evt->control;
        int cyc = (c & TRB_CYCLE) ? 1 : 0;
        if (cyc != (int)x->evt_cycle) return;      /* 无新事件 */

        uint32_t type  = (c >> TRB_TYPE_SHIFT) & 0x3F;
        uint32_t eslot = TRB_GET_SLOT(evt);
        uint32_t eep   = (c >> 16) & 0x1F;         /* Endpoint ID = DCI */
        int      resid = (int)(evt->status & 0xFFFFFF);

        { extern void early_console64_write_hex64(unsigned long long);
          extern void early_console64_write(const char*);
          early_console64_write("[evt-raw] type="); early_console64_write_hex64(type);
          early_console64_write(" slot="); early_console64_write_hex64(eslot);
          early_console64_write(" ep="); early_console64_write_hex64(eep);
          early_console64_write("\n"); }

        /* 消费事件 + 更新 ERDP */
        x->evt_dequeue++;
        if (x->evt_dequeue >= EVT_RING_TRBS) {
            x->evt_dequeue = 0;
            x->evt_cycle ^= 1;
        }
        uint64_t erdp = x->evt_ring_phys + x->evt_dequeue * sizeof(xhci_trb_t);
        mmio_w64(x->rt, XRT_ERDP(0), erdp | ERDP_EHB);

        if (type != TRB_TRANSFER_EVT) continue;    /* 非传输事件跳过 */

        { extern void early_console64_write_hex64(unsigned long long);
          extern void early_console64_write(const char*);
          early_console64_write("[xevt] xfer slot="); early_console64_write_hex64(eslot);
          early_console64_write(" ep="); early_console64_write_hex64(eep);
          early_console64_write("\n"); }

        int di = xhci_dev_by_slot(eslot);
        if (di < 0) continue;
        xhci_dev_t *d = &g_devs[di];

        /* ---- MSC bulk 事件投递（修复双消费者竞争）----
         * bulk IN = dci3, bulk OUT = dci4。若本设备是 MSC 且事件命中
         * 其 bulk 端点，投递到设备信箱供 xhci_msc_wait_event 取走，
         * 绝不能被下面的 HID 逻辑丢弃。 */
        if (d->is_msc && (eep == 3 || eep == 4)) {
            d->msc_evt_dci   = eep;
            d->msc_evt_cc    = (evt->status >> 24) & 0xFF;
            d->msc_evt_resid = (uint32_t)resid;
            d->msc_evt_par   = evt->parameter;
            d->msc_evt_ready = 1;
            continue;
        }

        if (!d->ep_in_ring) continue;
        uint32_t want_dci = (d->ep_in_addr & 0x0F) * 2 + 1;
        if (eep != want_dci) continue;             /* 非该设备 Interrupt-IN */

        /* 实际长度 = 期望长度 - residual，拷入设备 report 缓冲 */
        uint32_t rlen = d->ep_in_mps ? d->ep_in_mps : 8;
        if (rlen > sizeof(d->rpt_data)) rlen = sizeof(d->rpt_data);
        uint32_t n = (rlen > (uint32_t)resid) ? (rlen - (uint32_t)resid) : 0;
        if (n > sizeof(d->rpt_data)) n = sizeof(d->rpt_data);
        for (uint32_t i = 0; i < n; i++) d->rpt_data[i] = d->hid_buf[i];
        d->rpt_len   = n;
        d->rpt_ready = 1;

        xhci_hid_arm(d);                           /* 立即重新武装下一个传输 */
    }
}


/* 非阻塞探测第 idx 个 HID 设备的 Interrupt-IN 传输：
 *   命中一个 report → 拷贝到 out_buf 并重新投递 → 返回字节数(>0)
 *   无事件 → 0；出错 → 负数。 */
int xhci_hid_poll(uint32_t idx, uint8_t *out_buf, uint32_t out_cap) {
    int di = xhci_hid_index(idx);
    if (di < 0) return -1;
    xhci_dev_t *d = &g_devs[di];
    if (!d->ep_in_ring) return -2;         /* 未配置 */

    /* 【修复】不再每轮空敲门铃：此前每轮敲门铃（412 万次）会不断 reset
     * QEMU 端点的 retry 定时器，导致端点永远停在 retry、真实输入进不来。
     *
     * 但完全不敲又矫枉过正：开机首次 arm 投 1 个 TRB → 无按键 QEMU 返回 NAK →
     * 端点停在 retry；此后若 QEMU 的 usb_wakeup 时机没对上（trace 实测 epid3
     * 开机后零 ep_kick），端点就永久停摆、真实输入再也进不来。
     *
     * 折中方案：低频兜底门铃——每隔 XHCI_HID_KICK_INTERVAL 轮敲一次已配置端点的
     * 门铃，把可能停在 retry 的端点周期性踢活。频率远低于忙等阈值，不会 reset
     * retry 定时器造成 412 万次风暴，又能保证停摆端点在毫秒级内被重新推进。 */
    {
        static uint32_t s_kick_tick = 0;
        #define XHCI_HID_KICK_INTERVAL 512u
        if (++s_kick_tick >= XHCI_HID_KICK_INTERVAL) {
            s_kick_tick = 0;
            uint32_t ep_num = d->ep_in_addr & 0x0F;
            xhci_ring_dev_doorbell(d->slot_id, ep_num * 2 + 1);
        }
    }

    /* 排空事件环，按 slot 分发到各设备缓冲（不偷其它设备事件）；
     * pump 内部会对消费掉的 TRB 位置补投新 TRB 并敲门铃。 */
    xhci_pump_events();

    if (!d->rpt_ready) return 0;           /* 本设备无新 report */
    uint32_t n = d->rpt_len;
    if (n > out_cap) n = out_cap;
    for (uint32_t i = 0; i < n; i++) out_buf[i] = d->rpt_data[i];
    d->rpt_ready = 0;
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

/* ============================================================
 * M2.3 Mass Storage(BOT) —— Bulk 端点底层实现
 * ============================================================ */

/* 取第 idx 个 MSC 设备在 g_devs 中的索引；未找到返回 -1 */
static int xhci_msc_index(uint32_t idx) {
    uint32_t n = 0;
    for (int i = 0; i < XHCI_MAX_DEVS; i++) {
        if (g_devs[i].used && g_devs[i].is_msc) {
            if (n == idx) return i;
            n++;
        }
    }
    return -1;
}

uint32_t xhci_msc_device_count(void) {
    uint32_t n = 0;
    for (int i = 0; i < XHCI_MAX_DEVS; i++)
        if (g_devs[i].used && g_devs[i].is_msc) n++;
    return n;
}

int xhci_msc_device_info(uint32_t idx, uint32_t *slot_id, uint8_t *iface,
                         uint8_t *ep_in, uint8_t *ep_out) {
    int di = xhci_msc_index(idx);
    if (di < 0) return -1;
    xhci_dev_t *d = &g_devs[di];
    if (slot_id) *slot_id = d->slot_id;
    if (iface)   *iface   = d->iface_num;
    if (ep_in)   *ep_in   = d->ep_bulk_in;
    if (ep_out)  *ep_out  = d->ep_bulk_out;
    return 0;
}

/* EP0 控制传输透传（BOT Reset / Get Max LUN）*/
int xhci_msc_control(uint32_t idx, uint8_t bmReqType, uint8_t bReq,
                     uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                     uint64_t buf_phys) {
    int di = xhci_msc_index(idx);
    if (di < 0) return -1;
    return xhci_ep0_transfer(&g_devs[di], bmReqType, bReq, wValue, wIndex,
                             wLength, buf_phys);
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
