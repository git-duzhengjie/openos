/*
 * nvme64.c — NVMe 存储驱动（M2.2，MVP）
 *
 * 复用：M1.1 PCI 枚举 + pmm64 物理连续分配 + identity-map(phys==virt)
 *        + M2.1 AHCI 的 MMIO 显式映射套路（ABAR→NVMe BAR0）。
 *
 * 范围（MVP）：
 *   - 单控制器、单 namespace(NSID=1)、polling 模式
 *   - 1 个 admin 队列对 + 1 个 IO 队列对，队列深度 QDEPTH
 *   - 数据传输用 PRP（PRP1 + 可选 PRP2/PRP list），单次限 2 页内简单实现
 *   - IDENTIFY controller/namespace、READ(0x02)/WRITE(0x01)、FLUSH(0x00)
 *
 * 参考：NVMe 1.4 spec。
 */
#include "../include/nvme64.h"
#include "../include/pmm64.h"
#include "../include/vmm64.h"
#include "../../../kernel/include/pci.h"

extern uint64_t arch_x86_64_pmm_alloc_pages(uint64_t count);
extern void     early_serial64_write(const char *s);
extern void     arch_x86_64_proc_yield(void);
extern int      arch_x86_64_vmm_map_range(uint64_t virt, uint64_t phys,
                                          uint64_t length, uint64_t flags);

/* ---- 中断依赖（M2.x MSI）---- */
extern int  arch_x86_64_idt_register_irq(unsigned char cpu_vector, void (*handler)(void));
extern int  arch_x86_64_lapic_is_ready(void);
extern void arch_x86_64_lapic_send_eoi(void);
extern unsigned char arch_x86_64_lapic_id(void);
extern void x86_64_irq_nvme(void);   /* isr64.S 汇编 stub */

/* ---- 日志 ---- */
static void klog(const char *s) { early_serial64_write(s); }
static void klog_hex(uint64_t v) {
    char buf[19]; buf[0]='0'; buf[1]='x'; buf[18]='\0';
    for (int i = 0; i < 16; i++) {
        int nib = (int)((v >> ((15 - i) * 4)) & 0xF);
        buf[2 + i] = (char)(nib < 10 ? ('0' + nib) : ('a' + nib - 10));
    }
    klog(buf);
}
static void klog_dec(uint64_t v) {
    char buf[21]; int i = 20; buf[20] = '\0';
    if (v == 0) { klog("0"); return; }
    while (v && i > 0) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
    klog(&buf[i]);
}

/* ==================== NVMe 寄存器偏移（Controller Registers） ==================== */
#define NVME_REG_CAP     0x00  /* Controller Capabilities (64) */
#define NVME_REG_VS      0x08  /* Version (32) */
#define NVME_REG_CC      0x14  /* Controller Configuration (32) */
#define NVME_REG_CSTS    0x1C  /* Controller Status (32) */
#define NVME_REG_AQA     0x24  /* Admin Queue Attributes (32) */
#define NVME_REG_ASQ     0x28  /* Admin SQ Base Addr (64) */
#define NVME_REG_ACQ     0x30  /* Admin CQ Base Addr (64) */
/* Doorbells 从 0x1000 起，步长 = 4 << CAP.DSTRD */

/* CC 位 */
#define NVME_CC_EN       (1u << 0)
#define NVME_CC_CSS_NVM  (0u << 4)
#define NVME_CC_MPS_SH   7           /* 内存页大小 (2^(12+MPS)) */
#define NVME_CC_IOSQES_SH 16         /* IO SQ entry size = 2^n，Submission=64B → 6 */
#define NVME_CC_IOCQES_SH 20         /* IO CQ entry size = 2^n，Completion=16B → 4 */

/* CSTS 位 */
#define NVME_CSTS_RDY    (1u << 0)
#define NVME_CSTS_CFS    (1u << 1)

/* Admin 命令操作码 */
#define NVME_ADM_DELETE_SQ  0x00
#define NVME_ADM_CREATE_SQ  0x01
#define NVME_ADM_DELETE_CQ  0x04
#define NVME_ADM_CREATE_CQ  0x05
#define NVME_ADM_IDENTIFY   0x06
#define NVME_ADM_SET_FEAT   0x09

/* NVM(IO) 命令操作码 */
#define NVME_IO_FLUSH    0x00
#define NVME_IO_WRITE    0x01
#define NVME_IO_READ     0x02

#define QDEPTH   8            /* 队列深度（条目数），足够 MVP */
#define PAGE_SZ  4096u

/* ==================== 提交队列条目（64 字节） ==================== */
typedef struct {
    uint32_t cdw0;      /* opcode + flags + CID */
    uint32_t nsid;
    uint64_t rsvd2;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) nvme_sqe_t;

/* ==================== 完成队列条目（16 字节） ==================== */
typedef struct {
    uint32_t cdw0;
    uint32_t cdw1;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;    /* bit0 = phase tag，bit15..1 = status field */
} __attribute__((packed)) nvme_cqe_t;

/* ==================== 队列对 ==================== */
typedef struct {
    volatile nvme_sqe_t *sq;   /* 提交队列 */
    volatile nvme_cqe_t *cq;   /* 完成队列 */
    volatile uint32_t   *sq_db; /* 提交门铃 */
    volatile uint32_t   *cq_db; /* 完成门铃 */
    uint16_t sq_tail;
    uint16_t cq_head;
    uint8_t  phase;            /* 期望的 phase tag */
    uint16_t qid;
} nvme_queue_t;

/* ==================== 驱动状态 ==================== */
static volatile uint8_t *g_regs   = 0;   /* BAR0 MMIO 基址 */
static uint32_t          g_dstrd  = 0;   /* doorbell stride */
static nvme_queue_t      g_admin;
static nvme_queue_t      g_io;
static uint16_t          g_cid    = 1;   /* 命令 ID 计数 */

static int      g_present = 0;

/* ---- 中断状态（M2.x MSI）---- */
#define OPENOS_X86_64_NVME_VECTOR 0x31u
static const pci_device_t *g_pci_dev = 0;   /* 保存控制器 PCI 设备，供 MSI 配置 */
static volatile int g_nvme_irq_ready = 0;   /* MSI 已成功挂载 */
static volatile int g_nvme_done = 0;        /* 完成标志：中断 handler 置 1 */
static volatile uint32_t g_nvme_irq_count = 0;/* 中断触发次数 */
static uint32_t g_nsid    = 1;
static uint64_t g_nsze    = 0;   /* namespace 逻辑块数 */
static uint32_t g_blksz   = 512; /* 每逻辑块字节数 */

/* 数据缓冲（物理连续，PRP 用）—— 单次传输临时区，2 页 */
static uint8_t *g_databuf = 0;

/* ==================== MMIO 访问 ==================== */
static inline uint32_t reg_rd32(uint32_t off) {
    return *(volatile uint32_t *)(g_regs + off);
}
static inline void reg_wr32(uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(g_regs + off) = v;
}
static inline uint64_t reg_rd64(uint32_t off) {
    return *(volatile uint64_t *)(g_regs + off);
}
static inline void reg_wr64(uint32_t off, uint64_t v) {
    *(volatile uint64_t *)(g_regs + off) = v;
}

/* ---- 中断 C 处理程序（M2.x MSI）---- */
/* isr64.S 的 x86_64_irq_nvme stub 调用。职责：置完成标志 + 发 LAPIC EOI。
 * CQ head 推进与 phase 判断由发射侧统一处理，避免与轮询竞争。 */
void arch_x86_64_nvme_irq_trampoline(void)
{
    g_nvme_irq_count++;
    g_nvme_done = 1;
    arch_x86_64_lapic_send_eoi();
}

/* 挂载 MSI：IDT gate + PCI MSI 配置。需 LAPIC 就绪（故为延迟挂载）。成功置 g_nvme_irq_ready。
 * IO CQ 创建时已置 IEN + vector 0，此处仅补装 PCI MSI enable。 */
void nvme_irq_install_late(void)
{
    if (g_nvme_irq_ready) return;   /* 幂等 */
    if (!g_pci_dev) return;
    if (!arch_x86_64_lapic_is_ready()) {
        klog("[nvme] LAPIC not ready, MSI skipped (polling only)\n");
        return;
    }
    if (arch_x86_64_idt_register_irq(OPENOS_X86_64_NVME_VECTOR, x86_64_irq_nvme) != 0) {
        klog("[nvme] IDT register FAILED (polling only)\n");
        return;
    }
    /* QEMU nvme 设备使用 MSI-X（cap 0x11）而非传统 MSI；先试 MSI，失败则走 MSI-X */
    int ok = pci_msi_enable((pci_device_t *)g_pci_dev,
                            OPENOS_X86_64_NVME_VECTOR, arch_x86_64_lapic_id());
    if (!ok) {
        ok = pci_msix_enable((pci_device_t *)g_pci_dev,
                             OPENOS_X86_64_NVME_VECTOR, arch_x86_64_lapic_id());
    }
    if (!ok) {
        klog("[nvme] MSI/MSI-X enable FAILED (polling only)\n");
        return;
    }
    g_nvme_irq_ready = 1;
    klog("[nvme] MSI installed (vector 0x31)\n");
}

/* 返回中断触发次数（调试：>0 证明 MSI/MSI-X 中断路径真实生效） */
uint32_t nvme_irq_count(void) { return g_nvme_irq_count; }


/* ==================== 命令发射（polling） ====================
 * 把一条已填好的 SQE 放入队列 q，敲门铃，轮询 CQ 等完成。
 * 返回 status field（0 = 成功），负数为超时/错误。 */
static int nvme_submit(nvme_queue_t *q, nvme_sqe_t *cmd) {
    /* 分配 CID 并写入 cdw0 高 16 位 */
    uint16_t cid = g_cid++;
    cmd->cdw0 = (cmd->cdw0 & 0x0000FFFFu) | ((uint32_t)cid << 16);

    /* 拷入提交队列尾部 */
    volatile nvme_sqe_t *slot = &q->sq[q->sq_tail];
    const uint32_t *src = (const uint32_t *)cmd;
    volatile uint32_t *dst = (volatile uint32_t *)slot;
    for (int i = 0; i < 16; i++) dst[i] = src[i];

    /* 屏障：确保 SQE 写入对设备可见后再敲门铃 */
    __asm__ volatile("sfence" ::: "memory");

    /* 推进 SQ tail，敲提交门铃 */
    g_nvme_done = 0;
    q->sq_tail = (uint16_t)((q->sq_tail + 1) % QDEPTH);
    *q->sq_db = q->sq_tail;

    /* 等待完成：中断优先，phase tag 轮询回退。
     * 即使 MSI 已挂载，仍以 phase tag 作为权威判据（中断只是唤醒），
     * 若中断未触发（QEMU 配置差异），phase 轮询仍能完成，不会挂死。 */
    volatile nvme_cqe_t *cqe = &q->cq[q->cq_head];
    uint64_t spin = 0;
    /* 说明：NVMe 完成极快，phase-tag 轮询总能赢过 MSI-X 消息经 PCI 写事务的
     * 投递延迟，故运行时优选轮询（与 Linux nvme 驱动“polling queue”思路一致）。
     * MSI-X 已正确使能（见 [pci] MSI-X enabled 日志），中断 handler 作为唤醒兼底。
     * 不使用 hlt：早期 LAPIC timer 未周期触发，hlt 可能无中断源唤醒而挂起。 */
    for (;;) {
        uint16_t st = cqe->status;
        if ((st & 0x1) == q->phase) {
            /* 这条完成项已就绪：屏障后再读取 DMA 数据/completion 字段 */
            __asm__ volatile("mfence" ::: "memory");
            uint16_t sf = (uint16_t)(st >> 1);
            uint16_t rcid = cqe->cid;

            /* 推进 CQ head，phase 在回绕时翻转 */
            q->cq_head = (uint16_t)((q->cq_head + 1) % QDEPTH);
            if (q->cq_head == 0) q->phase ^= 1;
            *q->cq_db = q->cq_head;

            if (rcid != cid) {
                klog("[nvme] CID mismatch\n");
                return -2;
            }
            return (int)(sf & 0x7FFF);
        }
        if ((++spin & 0xFFFF) == 0) {
            arch_x86_64_proc_yield();
            if (spin > 0x8000000ULL) { klog("[nvme] cmd timeout\n"); return -1; }
        }
    }
}

/* 清零一段内存 */
static void zero_mem(void *p, uint64_t n) {
    volatile uint8_t *b = (volatile uint8_t *)p;
    for (uint64_t i = 0; i < n; i++) b[i] = 0;
}
static void zero_sqe(nvme_sqe_t *c) { zero_mem(c, sizeof(*c)); }

/* ==================== 初始化 admin 队列 ====================
 * 分配 admin SQ/CQ 物理页，写入 AQA/ASQ/ACQ。 */
static int nvme_init_admin_queues(void) {
    /* 各分配 1 页（QDEPTH*64=512B SQ, QDEPTH*16=128B CQ 均 ≤ 1 页） */
    uint64_t sq_pa = arch_x86_64_pmm_alloc_pages(1);
    uint64_t cq_pa = arch_x86_64_pmm_alloc_pages(1);
    if (!sq_pa || !cq_pa) { klog("[nvme] admin queue alloc fail\n"); return -1; }

    g_admin.sq = (volatile nvme_sqe_t *)sq_pa;
    g_admin.cq = (volatile nvme_cqe_t *)cq_pa;
    zero_mem((void *)g_admin.sq, PAGE_SZ);
    zero_mem((void *)g_admin.cq, PAGE_SZ);
    g_admin.sq_tail = 0;
    g_admin.cq_head = 0;
    g_admin.phase   = 1;   /* CQ 初始 phase tag 为 0，首轮完成翻为 1 */
    g_admin.qid     = 0;

    /* AQA: 低16=SQ size-1, 高16=CQ size-1 */
    reg_wr32(NVME_REG_AQA, ((QDEPTH - 1) << 16) | (QDEPTH - 1));
    reg_wr64(NVME_REG_ASQ, sq_pa);
    reg_wr64(NVME_REG_ACQ, cq_pa);

    /* doorbell: SQ0TDBL = regs + 0x1000 + (2*qid)*stride，CQ0HDBL = +stride */
    uint32_t stride = 4u << g_dstrd;
    g_admin.sq_db = (volatile uint32_t *)(g_regs + 0x1000 + (2 * 0) * stride);
    g_admin.cq_db = (volatile uint32_t *)(g_regs + 0x1000 + (2 * 0 + 1) * stride);
    return 0;
}

/* 创建 1 个 IO 队列对（qid=1）。先建 CQ 再建 SQ。 */
static int nvme_create_io_queues(void) {
    uint64_t sq_pa = arch_x86_64_pmm_alloc_pages(1);
    uint64_t cq_pa = arch_x86_64_pmm_alloc_pages(1);
    if (!sq_pa || !cq_pa) { klog("[nvme] io queue alloc fail\n"); return -1; }

    g_io.sq = (volatile nvme_sqe_t *)sq_pa;
    g_io.cq = (volatile nvme_cqe_t *)cq_pa;
    zero_mem((void *)g_io.sq, PAGE_SZ);
    zero_mem((void *)g_io.cq, PAGE_SZ);
    g_io.sq_tail = 0;
    g_io.cq_head = 0;
    g_io.phase   = 1;
    g_io.qid     = 1;

    uint32_t stride = 4u << g_dstrd;
    g_io.sq_db = (volatile uint32_t *)(g_regs + 0x1000 + (2 * 1) * stride);
    g_io.cq_db = (volatile uint32_t *)(g_regs + 0x1000 + (2 * 1 + 1) * stride);

    nvme_sqe_t cmd;

    /* --- CREATE IO CQ (qid=1) --- */
    zero_sqe(&cmd);
    cmd.cdw0 = NVME_ADM_CREATE_CQ;
    cmd.prp1 = cq_pa;
    /* cdw10: 高16=queue size-1, 低16=qid */
    cmd.cdw10 = ((QDEPTH - 1) << 16) | 1;
    /* cdw11: bit0=PC(物理连续), bit1=IEN(中断使能), 高16=Interrupt Vector
     * 无条件置 IEN + vector 0：此时 PCI MSI 尚未 enable（LAPIC 未就绪），
     * QEMU 不会真发中断，仍走 polling；待 nvme_irq_install_late() 补装 MSI 后中断生效，
     * 无需重建 IO 队列。 */
    cmd.cdw11 = 0x1u | 0x2u | (0u << 16);
    int st = nvme_submit(&g_admin, &cmd);
    if (st != 0) { klog("[nvme] create CQ fail st="); klog_hex((uint64_t)st); klog("\n"); return -2; }

    /* --- CREATE IO SQ (qid=1, 关联 cqid=1) --- */
    zero_sqe(&cmd);
    cmd.cdw0 = NVME_ADM_CREATE_SQ;
    cmd.prp1 = sq_pa;
    cmd.cdw10 = ((QDEPTH - 1) << 16) | 1;
    /* cdw11: 高16=cqid, bit0=PC */
    cmd.cdw11 = (1u << 16) | 0x1;
    st = nvme_submit(&g_admin, &cmd);
    if (st != 0) { klog("[nvme] create SQ fail st="); klog_hex((uint64_t)st); klog("\n"); return -3; }

    klog("[nvme] IO queue pair created\n");
    return 0;
}

/* ==================== IDENTIFY ====================
 * CNS=1: identify controller; CNS=0: identify namespace(需 nsid)。
 * 返回数据写入 g_databuf（刚好 4096 字节）。 */
static int nvme_identify(uint32_t cns, uint32_t nsid) {
    zero_mem(g_databuf, PAGE_SZ);
    nvme_sqe_t cmd;
    zero_sqe(&cmd);
    cmd.cdw0  = NVME_ADM_IDENTIFY;
    cmd.nsid  = nsid;
    cmd.prp1  = (uint64_t)g_databuf;   /* phys==virt */
    cmd.cdw10 = cns;
    return nvme_submit(&g_admin, &cmd);
}

/* 探测 namespace 1：取总块数与块大小 */
static int nvme_probe_namespace(void) {
    int st = nvme_identify(0 /*CNS=namespace*/, g_nsid);
    if (st != 0) { klog("[nvme] identify ns fail st="); klog_hex((uint64_t)st); klog("\n"); return -1; }

    /* Identify Namespace 结构：
     *   NSZE @ 字节 0 (8B) = namespace 总块数
     *   NLBAF @ 字节 25 = 支持的 LBA 格式数-1
     *   FLBAS @ 字节 26 (低4位) = 当前使用的 LBA 格式索引
     *   LBAF[i] @ 字节 128 + i*4：低16=metadata, 高位 LBADS(bit16..23)=2^n 块字节 */
    uint64_t nsze = *(volatile uint64_t *)(g_databuf + 0);
    uint8_t  flbas = g_databuf[26] & 0x0F;
    uint32_t lbaf  = *(volatile uint32_t *)(g_databuf + 128 + flbas * 4);
    uint8_t  lbads = (uint8_t)((lbaf >> 16) & 0xFF);
    uint32_t blksz = (lbads >= 9 && lbads <= 20) ? (1u << lbads) : 512u;

    g_nsze  = nsze;
    g_blksz = blksz;
    klog("[nvme] ns1: blocks="); klog_dec(nsze);
    klog(" blksz="); klog_dec(blksz); klog("\n");
    return 0;
}

/* ==================== 控制器初始化主流程 ==================== */
int nvme_init(void) {
    g_present = 0;
    klog("[nvme] init...\n");

    /* --- 1. PCI 探测 NVMe 控制器 (class 0x01 subclass 0x08) --- */
    const pci_device_t *dev = pci_find_by_class(PCI_CLASS_STORAGE, PCI_SUBCLASS_NVME);
    if (!dev) { klog("[nvme] no NVMe controller\n"); return -1; }
    klog("[nvme] found @ bus/dev/fn, vendor="); klog_hex(dev->vendor_id);
    klog(" device="); klog_hex(dev->device_id); klog("\n");

    /* --- 2. BAR0 = MMIO 控制器寄存器 --- */
    const pci_bar_t *bar0 = &dev->bars[0];
    if (bar0->base == 0 || !bar0->is_mmio) { klog("[nvme] bad BAR0\n"); return -1; }
    g_regs = (volatile uint8_t *)bar0->base;
    klog("[nvme] BAR0="); klog_hex(bar0->base); klog("\n");

    /* --- 2b. 映射 BAR0 MMIO（超出 low-512MB identity，需显式映射 + 禁缓存）
     *        NVMe 寄存器空间含 doorbell，映射 16KB 充裕 --- */
    {
        uint64_t base = bar0->base & ~(0xFFFULL);
        if (arch_x86_64_vmm_map_range(base, base, 0x4000ULL,
                                      OPENOS_X86_64_VMM_MMIO_FLAGS) != 0) {
            klog("[nvme] BAR0 mmio map FAILED\n"); return -2;
        }
        klog("[nvme] BAR0 mapped (mmio)\n");
    }

    /* --- 3. 使能 bus master + MMIO --- */
    pci_enable_bus_master((pci_device_t *)dev);
    pci_enable_mmio((pci_device_t *)dev);
    g_pci_dev = dev;   /* 保存供 MSI 配置 */

    /* --- 4. 读 CAP，取 doorbell stride 与最大队列深度 --- */
    uint64_t cap = reg_rd64(NVME_REG_CAP);
    g_dstrd = (uint32_t)((cap >> 32) & 0xF);
    uint32_t mqes = (uint32_t)(cap & 0xFFFF); /* 最大队列条目-1 */
    klog("[nvme] CAP dstrd="); klog_dec(g_dstrd);
    klog(" mqes="); klog_dec(mqes); klog("\n");

    /* --- 5. 禁用控制器（CC.EN=0）并等 CSTS.RDY=0 --- */
    uint32_t cc = reg_rd32(NVME_REG_CC);
    cc &= ~NVME_CC_EN;
    reg_wr32(NVME_REG_CC, cc);
    {
        uint64_t spin = 0;
        while (reg_rd32(NVME_REG_CSTS) & NVME_CSTS_RDY) {
            if ((++spin & 0xFFFF) == 0) arch_x86_64_proc_yield();
            if (spin > 0x4000000ULL) { klog("[nvme] disable timeout\n"); return -3; }
        }
    }

    /* --- 6. 分配数据缓冲 + 初始化 admin 队列 --- */
    {
        uint64_t db = arch_x86_64_pmm_alloc_pages(2);
        if (!db) { klog("[nvme] databuf alloc fail\n"); return -4; }
        g_databuf = (uint8_t *)db;
    }
    if (nvme_init_admin_queues() != 0) return -5;

    /* --- 7. 配置 CC 并使能控制器 --- */
    cc = 0;
    cc |= NVME_CC_CSS_NVM;
    cc |= (0u << NVME_CC_MPS_SH);            /* MPS=0 → 4KB 页 */
    cc |= (6u << NVME_CC_IOSQES_SH);         /* SQ entry 64B = 2^6 */
    cc |= (4u << NVME_CC_IOCQES_SH);         /* CQ entry 16B = 2^4 */
    cc |= NVME_CC_EN;
    reg_wr32(NVME_REG_CC, cc);
    {
        uint64_t spin = 0;
        while (!(reg_rd32(NVME_REG_CSTS) & NVME_CSTS_RDY)) {
            if (reg_rd32(NVME_REG_CSTS) & NVME_CSTS_CFS) {
                klog("[nvme] controller fatal (CFS)\n"); return -6;
            }
            if ((++spin & 0xFFFF) == 0) arch_x86_64_proc_yield();
            if (spin > 0x4000000ULL) { klog("[nvme] enable timeout\n"); return -6; }
        }
    }
    klog("[nvme] controller enabled (RDY)\n");

    /* --- 8. IDENTIFY controller（取型号信息） --- */
    if (nvme_identify(1 /*CNS=controller*/, 0) == 0) {
        /* 型号 MN @ 字节 24..63 */
        klog("[nvme] ctrl MN=");
        for (int i = 24; i < 40; i++) {
            char c = (char)g_databuf[i];
            char s[2]; s[0] = (c >= 32 && c < 127) ? c : '.'; s[1] = 0;
            klog(s);
        }
        klog("\n");
    }

    /* --- 9. 创建 IO 队列对 --- */
    /* --- 9b. 创建 IO 队列（CQ 已带 IEN + vector 0）；MSI enable 由 nvme_irq_install_late() 延迟补装 --- */
    if (nvme_create_io_queues() != 0) return -7;

    /* --- 10. 探测 namespace 1 --- */
    if (nvme_probe_namespace() != 0) return -8;

    g_present = 1;
    klog("[nvme] init DONE\n");
    return 0;
}

/* ==================== 读写（PRP） ====================
 * MVP：单次传输限 g_databuf 2 页 = 8KB。为简化，按“≤页内单次”处理：
 *   PRP1 = 首页基址；PRP2 = 第二页基址（若跨页）。
 * 上层 read/write_sectors 循环分块，每块 ≤ 2 页。 */
static int nvme_rw_chunk(int is_write, uint64_t lba, uint32_t nblk, void *buf) {
    uint32_t bytes = nblk * g_blksz;
    if (bytes > 2 * PAGE_SZ) return -10;

    /* 写：先把用户数据拷入 g_databuf */
    if (is_write) {
        const uint8_t *s = (const uint8_t *)buf;
        for (uint32_t i = 0; i < bytes; i++) g_databuf[i] = s[i];
    }

    uint64_t p1 = (uint64_t)g_databuf;
    uint64_t p2 = 0;
    if (bytes > PAGE_SZ) p2 = (uint64_t)g_databuf + PAGE_SZ;

    nvme_sqe_t cmd;
    zero_sqe(&cmd);
    cmd.cdw0  = is_write ? NVME_IO_WRITE : NVME_IO_READ;
    cmd.nsid  = g_nsid;
    cmd.prp1  = p1;
    cmd.prp2  = p2;
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFFu);        /* SLBA 低32 */
    cmd.cdw11 = (uint32_t)((lba >> 32) & 0xFFFFFFFFu);/* SLBA 高32 */
    cmd.cdw12 = (nblk - 1) & 0xFFFF;                  /* NLB（零基） */

    int st = nvme_submit(&g_io, &cmd);
    if (st != 0) return -11;

    /* 读：把 g_databuf 拷回用户 */
    if (!is_write) {
        uint8_t *d = (uint8_t *)buf;
        for (uint32_t i = 0; i < bytes; i++) d[i] = g_databuf[i];
    }
    return 0;
}

/* 每块可容纳的最大逻辑块数（2 页 / blksz） */
static uint32_t nvme_max_blk_per_chunk(void) {
    uint32_t n = (2 * PAGE_SZ) / g_blksz;
    if (n == 0) n = 1;
    return n;
}

int nvme_read_sectors(uint64_t lba, uint32_t count, void *buf) {
    if (!g_present || count == 0) return -1;
    uint8_t *out = (uint8_t *)buf;
    uint32_t per = nvme_max_blk_per_chunk();
    while (count > 0) {
        uint32_t n = (count < per) ? count : per;
        int st = nvme_rw_chunk(0, lba, n, out);
        if (st != 0) return st;
        lba   += n;
        count -= n;
        out   += n * g_blksz;
    }
    return 0;
}

int nvme_write_sectors(uint64_t lba, uint32_t count, const void *buf) {
    if (!g_present || count == 0) return -1;
    const uint8_t *in = (const uint8_t *)buf;
    uint32_t per = nvme_max_blk_per_chunk();
    while (count > 0) {
        uint32_t n = (count < per) ? count : per;
        int st = nvme_rw_chunk(1, lba, n, (void *)in);
        if (st != 0) return st;
        lba   += n;
        count -= n;
        in    += n * g_blksz;
    }
    return 0;
}

int nvme_flush(void) {
    if (!g_present) return -1;
    nvme_sqe_t cmd;
    zero_sqe(&cmd);
    cmd.cdw0 = NVME_IO_FLUSH;
    cmd.nsid = g_nsid;
    int st = nvme_submit(&g_io, &cmd);
    return (st == 0) ? 0 : -1;
}

/* ==================== FAT32 blockdev 适配器 ====================
 * fat32_read_fn/write_fn 的 lba 是 uint32_t（512B 扇区语义），
 * 而 nvme_read/write_sectors 用 uint64_t lba + 原生块大小。
 * 这里做签名收窄 + 512B→原生块的换算，供 fat32_mount 依赖注入。
 * 约定：FAT32 以 512B 逻辑扇区寻址；若 NVMe 原生块=512 则直通，
 *       否则暂不支持（返回 -1，交由上层回退 ATA）。 */
int nvme_fat_read(uint32_t lba, uint32_t count, void *buf) {
    if (!g_present || g_blksz != 512) return -1;
    return nvme_read_sectors((uint64_t)lba, count, buf);
}

int nvme_fat_write(uint32_t lba, uint32_t count, const void *buf) {
    if (!g_present || g_blksz != 512) return -1;
    return nvme_write_sectors((uint64_t)lba, count, buf);
}

/* ==================== 对外查询接口 ==================== */
int      nvme_present(void)      { return g_present; }
uint64_t nvme_sector_count(void) { return g_present ? g_nsze : 0; }
uint32_t nvme_block_size(void)   { return g_present ? g_blksz : 0; }

/* ==================== headless 自测 ====================
 * IDENTIFY 出容量 + 写一块读回逐字节校验。选靠后的安全块。 */
int nvme_selftest(void) {
    if (!g_present) { klog("[nvme] selftest SKIP (no device)\n"); return -1; }

    uint64_t total = g_nsze;
    klog("[nvme] selftest: capacity="); klog_dec(total);
    klog(" blocks x "); klog_dec(g_blksz); klog(" bytes\n");
    if (total < 16) { klog("[nvme] selftest: too small\n"); return -2; }

    uint64_t lba = total - 4;   /* 靠后安全块 */
    static uint8_t wbuf[512];
    static uint8_t rbuf[512];
    uint32_t bs = (g_blksz < 512) ? g_blksz : 512;

    for (uint32_t i = 0; i < bs; i++) wbuf[i] = (uint8_t)(0xA5 ^ (i & 0xFF));
    for (uint32_t i = 0; i < bs; i++) rbuf[i] = 0;

    if (nvme_write_sectors(lba, 1, wbuf) != 0) { klog("[nvme] selftest write FAIL\n"); return -3; }
    if (nvme_flush() != 0)                      { klog("[nvme] selftest flush FAIL\n"); return -4; }
    if (nvme_read_sectors(lba, 1, rbuf) != 0)   { klog("[nvme] selftest read FAIL\n"); return -5; }

    for (uint32_t i = 0; i < bs; i++) {
        if (wbuf[i] != rbuf[i]) {
            klog("[nvme] selftest VERIFY FAIL @ off="); klog_dec(i); klog("\n");
            return -6;
        }
    }
    klog("[nvme] selftest: write/read/verify PASS @ lba="); klog_dec(lba); klog("\n");
    klog("[nvme] === selftest PASS ===\n");
    return 0;
}
