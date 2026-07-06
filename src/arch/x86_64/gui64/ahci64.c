/*
 * ahci64.c — AHCI/SATA 硬盘驱动（M2.1）
 *
 * MVP：单控制器、单端口、polling 模式、READ/WRITE DMA EXT (LBA48)。
 * 复用 M1.1 PCI 枚举 + pmm64 物理连续分配 + identity-map(phys==virt)。
 *
 * 参考规范：Serial ATA AHCI 1.3.1 Specification。
 */
#include <stdint.h>
#include <stddef.h>
#include "../include/ahci64.h"
#include "../include/pmm64.h"
#include "../include/vmm64.h"
#include "../../../kernel/include/pci.h"

/* ---- 外部依赖 ---- */
extern uint64_t arch_x86_64_pmm_alloc_pages(uint64_t count);
extern void     early_serial64_write(const char *s);
extern void     arch_x86_64_proc_yield(void);
extern int      arch_x86_64_vmm_map_range(uint64_t virt, uint64_t phys,
                                          uint64_t length, uint64_t flags);

/* ---- 串口日志辅助 ---- */
static void klog(const char *s) { early_serial64_write(s); }
static void klog_hex(uint64_t v)
{
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        int shift = (15 - i) * 4;
        uint8_t nib = (uint8_t)((v >> shift) & 0xF);
        buf[2 + i] = (char)(nib < 10 ? ('0' + nib) : ('a' + nib - 10));
    }
    buf[18] = 0;
    klog(buf);
}
static void klog_dec(uint64_t v)
{
    char buf[21];
    int i = 20;
    buf[i--] = 0;
    if (v == 0) { buf[i--] = '0'; }
    while (v > 0 && i >= 0) { buf[i--] = (char)('0' + (v % 10)); v /= 10; }
    klog(&buf[i + 1]);
}

/* ================= AHCI HBA 寄存器 (Generic Host Control) ================= */
/* HBA memory registers (ABAR + offset)，全部 32-bit */
#define HBA_CAP        0x00  /* Host Capabilities */
#define HBA_GHC        0x04  /* Global Host Control */
#define HBA_IS         0x08  /* Interrupt Status */
#define HBA_PI         0x0C  /* Ports Implemented */
#define HBA_VS         0x10  /* Version */

#define GHC_HR         (1u << 0)   /* HBA Reset */
#define GHC_IE         (1u << 1)   /* Interrupt Enable */
#define GHC_AE         (1u << 31)  /* AHCI Enable */

/* ---- 每个端口寄存器 (ABAR + 0x100 + port*0x80) ---- */
#define PORT_BASE(p)   (0x100u + (uint32_t)(p) * 0x80u)
#define PxCLB          0x00  /* Command List Base (low 32) */
#define PxCLBU         0x04  /* Command List Base (high 32) */
#define PxFB           0x08  /* FIS Base (low 32) */
#define PxFBU          0x0C  /* FIS Base (high 32) */
#define PxIS           0x10  /* Interrupt Status */
#define PxIE           0x14  /* Interrupt Enable */
#define PxCMD          0x18  /* Command and Status */
#define PxTFD          0x20  /* Task File Data */
#define PxSIG          0x24  /* Signature */
#define PxSSTS         0x28  /* SATA Status (SCR0) */
#define PxSCTL         0x2C  /* SATA Control (SCR2) */
#define PxSERR         0x30  /* SATA Error (SCR1) */
#define PxSACT         0x34  /* SATA Active */
#define PxCI           0x38  /* Command Issue */

#define PxCMD_ST       (1u << 0)   /* Start */
#define PxCMD_FRE      (1u << 4)   /* FIS Receive Enable */
#define PxCMD_FR       (1u << 14)  /* FIS Receive Running */
#define PxCMD_CR       (1u << 15)  /* Command List Running */

#define PxTFD_BSY      (1u << 7)   /* Busy */
#define PxTFD_DRQ      (1u << 3)   /* Data transfer requested */
#define PxTFD_ERR      (1u << 0)   /* Error */

#define SIG_SATA       0x00000101u /* SATA drive signature */

/* SSTS.DET */
#define SSTS_DET_MASK  0x0Fu
#define SSTS_DET_PRESENT 0x03u     /* device present + PHY comm established */

/* ================= FIS 类型 ================= */
#define FIS_TYPE_REG_H2D  0x27u
#define FIS_TYPE_REG_D2H  0x34u

/* ================= ATA 命令 ================= */
#define ATA_CMD_IDENTIFY      0xECu
#define ATA_CMD_READ_DMA_EXT  0x25u
#define ATA_CMD_WRITE_DMA_EXT 0x35u
#define ATA_CMD_FLUSH_EXT     0xEAu

/* ================= AHCI 数据结构（严格按硬件布局，packed） ================= */

/* Command Header（命令列表中的一项，32 字节）。每端口 32 项 = 1KB。 */
typedef struct __attribute__((packed)) hba_cmd_header {
    uint8_t  cfl : 5;    /* Command FIS Length (in DWORDs) */
    uint8_t  a   : 1;    /* ATAPI */
    uint8_t  w   : 1;    /* Write (1=H2D 写盘) */
    uint8_t  p   : 1;    /* Prefetchable */
    uint8_t  r   : 1;    /* Reset */
    uint8_t  b   : 1;    /* BIST */
    uint8_t  c   : 1;    /* Clear busy upon R_OK */
    uint8_t  rsv0: 1;
    uint8_t  pmp : 4;    /* Port Multiplier Port */
    uint16_t prdtl;      /* PRDT 项数 */
    volatile uint32_t prdbc;  /* PRD 已传输字节数 */
    uint32_t ctba;       /* Command Table Base (low 32) */
    uint32_t ctbau;      /* Command Table Base (high 32) */
    uint32_t rsv1[4];
} hba_cmd_header_t;

/* Physical Region Descriptor（PRDT 项，16 字节） */
typedef struct __attribute__((packed)) hba_prdt_entry {
    uint32_t dba;        /* Data Base Address (low 32) */
    uint32_t dbau;       /* Data Base Address (high 32) */
    uint32_t rsv0;
    uint32_t dbc : 22;   /* Byte Count - 1（最大 4MB-1） */
    uint32_t rsv1: 9;
    uint32_t i   : 1;    /* Interrupt on Completion */
} hba_prdt_entry_t;

/* Command Table：CFIS(64) + ATAPI(16) + rsv(48) + PRDT[]。这里 1 个 PRDT。 */
typedef struct __attribute__((packed)) hba_cmd_table {
    uint8_t  cfis[64];   /* Command FIS */
    uint8_t  acmd[16];   /* ATAPI command */
    uint8_t  rsv[48];
    hba_prdt_entry_t prdt[8];  /* 支持最多 8 个 PRDT（8*4MB=32MB/次，够用） */
} hba_cmd_table_t;

/* Register FIS - Host to Device（20 字节，放进 cmd_table.cfis 前部） */
typedef struct __attribute__((packed)) fis_reg_h2d {
    uint8_t  fis_type;   /* FIS_TYPE_REG_H2D */
    uint8_t  pmport : 4;
    uint8_t  rsv0   : 3;
    uint8_t  c      : 1; /* 1=Command, 0=Control */
    uint8_t  command;    /* ATA 命令 */
    uint8_t  featurel;
    uint8_t  lba0, lba1, lba2;
    uint8_t  device;
    uint8_t  lba3, lba4, lba5;
    uint8_t  featureh;
    uint8_t  countl, counth;
    uint8_t  icc;
    uint8_t  control;
    uint8_t  rsv1[4];
} fis_reg_h2d_t;

/* ================= 全局驱动状态 ================= */
static volatile uint8_t *g_abar = 0;   /* HBA MMIO 基址（identity map，phys==virt） */
static int      g_port = -1;           /* 使用的端口号 */
static int      g_present = 0;
static uint64_t g_sectors = 0;         /* 磁盘总扇区数 */

/* 端口的 DMA 结构（物理连续分配，phys==virt 直接用指针） */
static volatile hba_cmd_header_t *g_clb = 0;   /* 命令列表（32 项，1KB） */
static volatile uint8_t          *g_fb  = 0;   /* FIS 接收区（256B） */
static volatile hba_cmd_table_t  *g_ctb = 0;   /* 命令表（用 slot 0） */
static volatile uint8_t          *g_dma = 0;   /* 数据 DMA 缓冲（一页 4KB=8扇区） */

/* ================= MMIO 读写辅助 ================= */
static inline uint32_t mmio_r(uint32_t off)
{
    return *(volatile uint32_t *)(g_abar + off);
}
static inline void mmio_w(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(g_abar + off) = val;
}
static inline uint32_t port_r(int p, uint32_t off)
{
    return mmio_r(PORT_BASE(p) + off);
}
static inline void port_w(int p, uint32_t off, uint32_t val)
{
    mmio_w(PORT_BASE(p) + off, val);
}

/* ================= 端口引擎停/启 ================= */

/* 停止命令列表 + FIS 接收引擎，等待 CR/FR 清零 */
static void port_stop(int p)
{
    uint32_t cmd = port_r(p, PxCMD);
    cmd &= ~PxCMD_ST;
    cmd &= ~PxCMD_FRE;
    port_w(p, PxCMD, cmd);

    /* 等 CR 和 FR 清零（最多自旋若干次，带 yield 防卡） */
    for (int i = 0; i < 1000000; i++) {
        uint32_t c = port_r(p, PxCMD);
        if (!(c & PxCMD_CR) && !(c & PxCMD_FR)) break;
        if ((i & 0x3FFF) == 0) arch_x86_64_proc_yield();
    }
}

/* 启动 FIS 接收 + 命令列表引擎 */
static void port_start(int p)
{
    /* 等 CR 清零才能启动 */
    for (int i = 0; i < 1000000; i++) {
        if (!(port_r(p, PxCMD) & PxCMD_CR)) break;
        if ((i & 0x3FFF) == 0) arch_x86_64_proc_yield();
    }
    uint32_t cmd = port_r(p, PxCMD);
    cmd |= PxCMD_FRE;
    cmd |= PxCMD_ST;
    port_w(p, PxCMD, cmd);
}

/* 等 BSY/DRQ 清零（命令发射前确保端口空闲） */
static int port_wait_ready(int p)
{
    for (int i = 0; i < 2000000; i++) {
        uint32_t tfd = port_r(p, PxTFD);
        if (!(tfd & (PxTFD_BSY | PxTFD_DRQ))) return 0;
        if ((i & 0x3FFF) == 0) arch_x86_64_proc_yield();
    }
    return -1;
}

/*
 * 发射一条命令（slot 0），轮询完成。
 *   cmd     : ATA 命令号
 *   lba     : 起始 LBA48
 *   count   : 扇区数（0=IDENTIFY 特例，按 1 扇处理）
 *   write   : 1=写，0=读
 *   buf_phys: 数据缓冲物理地址
 *   bytes   : 传输字节数
 * 返回 0 成功，负数失败。
 */
static int ahci_issue(uint8_t cmd, uint64_t lba, uint32_t count,
                      int write, uint64_t buf_phys, uint32_t bytes)
{
    int p = g_port;
    if (port_wait_ready(p) != 0) { klog("[ahci] port busy before issue\n"); return -1; }

    /* 清 SERR */
    port_w(p, PxSERR, port_r(p, PxSERR));

    /* --- 填命令头 slot 0 --- */
    volatile hba_cmd_header_t *ch = &g_clb[0];
    ch->cfl   = (uint8_t)(sizeof(fis_reg_h2d_t) / 4);  /* 20/4 = 5 DWORD */
    ch->a     = 0;
    ch->w     = write ? 1 : 0;
    ch->p     = 0;
    ch->prdtl = 1;
    ch->prdbc = 0;

    /* --- 填 PRDT[0] --- */
    volatile hba_cmd_table_t *ct = g_ctb;
    /* 清零 cmd table 头部 */
    for (int i = 0; i < 64; i++) ct->cfis[i] = 0;
    ct->prdt[0].dba  = (uint32_t)(buf_phys & 0xFFFFFFFFu);
    ct->prdt[0].dbau = (uint32_t)(buf_phys >> 32);
    ct->prdt[0].rsv0 = 0;
    ct->prdt[0].dbc  = (bytes > 0 ? bytes - 1 : 0);  /* byte count - 1 */
    ct->prdt[0].rsv1 = 0;
    ct->prdt[0].i    = 0;

    /* --- 填 H2D FIS --- */
    volatile fis_reg_h2d_t *fis = (volatile fis_reg_h2d_t *)ct->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport   = 0;
    fis->c        = 1;  /* command */
    fis->command  = cmd;
    fis->featurel = 0;
    fis->featureh = 0;
    fis->lba0 = (uint8_t)(lba        & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16)& 0xFF);
    fis->device = (cmd == ATA_CMD_IDENTIFY) ? 0 : (1u << 6);  /* LBA mode */
    fis->lba3 = (uint8_t)((lba >> 24)& 0xFF);
    fis->lba4 = (uint8_t)((lba >> 32)& 0xFF);
    fis->lba5 = (uint8_t)((lba >> 40)& 0xFF);
    fis->countl = (uint8_t)(count & 0xFF);
    fis->counth = (uint8_t)((count >> 8) & 0xFF);
    fis->icc = 0;
    fis->control = 0;
    fis->rsv1[0] = fis->rsv1[1] = fis->rsv1[2] = fis->rsv1[3] = 0;

    /* --- 发射：置 CI bit0 --- */
    port_w(p, PxCI, 1u);

    /* --- 轮询完成：CI bit0 清零 --- */
    for (int i = 0; i < 3000000; i++) {
        if ((port_r(p, PxCI) & 1u) == 0) break;
        /* 检查任务文件错误 */
        if (port_r(p, PxTFD) & PxTFD_ERR) {
            klog("[ahci] task file error, TFD="); klog_hex(port_r(p, PxTFD)); klog("\n");
            return -2;
        }
        if ((i & 0x3FFF) == 0) arch_x86_64_proc_yield();
    }
    if (port_r(p, PxCI) & 1u) { klog("[ahci] issue timeout\n"); return -3; }

    /* 最终错误检查 */
    if (port_r(p, PxTFD) & PxTFD_ERR) {
        klog("[ahci] error after complete, TFD="); klog_hex(port_r(p, PxTFD)); klog("\n");
        return -4;
    }
    return 0;
}

/* ================= 初始化 ================= */

/* 分配一页物理连续内存（identity map，phys==virt）。失败返回 0。 */
static uint64_t alloc_page(void)
{
    uint64_t pa = arch_x86_64_pmm_alloc_pages(1);
    if (pa == 0) return 0;
    /* 清零 */
    volatile uint8_t *p = (volatile uint8_t *)pa;
    for (int i = 0; i < 4096; i++) p[i] = 0;
    return pa;
}

int ahci_init(void)
{
    g_present = 0;
    g_port = -1;
    g_sectors = 0;

    /* --- 1. PCI 探测 AHCI 控制器 (class 0x01, subclass 0x06) --- */
    const pci_device_t *dev = pci_find_by_class(PCI_CLASS_STORAGE, PCI_SUBCLASS_SATA);
    if (!dev) { klog("[ahci] no AHCI controller found\n"); return -1; }
    klog("[ahci] controller @ ");
    klog_hex(dev->vendor_id); klog(":"); klog_hex(dev->device_id); klog("\n");

    /* --- 2. 取 BAR5 = ABAR（MMIO） --- */
    const pci_bar_t *bar5 = &dev->bars[5];
    if (!bar5->is_mmio || bar5->base == 0) { klog("[ahci] BAR5 invalid\n"); return -2; }
    g_abar = (volatile uint8_t *)bar5->base;
    klog("[ahci] ABAR="); klog_hex(bar5->base); klog("\n");

    /* --- 2b. 映射 ABAR MMIO 区域（超出 low-512MB identity 范围，需显式映射，
     *        用 MMIO flags 禁缓存 PCD/PWT）。AHCI 寄存器空间 ≤ 8KB，映射 2 页。 --- */
    {
        uint64_t base = bar5->base & ~(0xFFFULL);
        if (arch_x86_64_vmm_map_range(base, base, 0x2000ULL,
                                      OPENOS_X86_64_VMM_MMIO_FLAGS) != 0) {
            klog("[ahci] ABAR mmio map FAILED\n"); return -2;
        }
        klog("[ahci] ABAR mapped (mmio)\n");
    }

    /* --- 3. 使能 bus master + MMIO --- */
    klog("[ahci] enabling bus master + mmio...\n");
    pci_enable_bus_master((pci_device_t *)dev);
    pci_enable_mmio((pci_device_t *)dev);
    klog("[ahci] reading CAP...\n");
    klog("[ahci] CAP="); klog_hex(mmio_r(HBA_CAP)); klog("\n");

    /* --- 4. 使能 AHCI 模式 --- */
    mmio_w(HBA_GHC, mmio_r(HBA_GHC) | GHC_AE);

    /* --- 5. 扫描 PI 找挂盘端口 --- */
    uint32_t pi = mmio_r(HBA_PI);
    klog("[ahci] PI="); klog_hex(pi); klog("\n");
    int found = -1;
    for (int p = 0; p < 32; p++) {
        if (!(pi & (1u << p))) continue;
        uint32_t ssts = port_r(p, PxSSTS);
        if ((ssts & SSTS_DET_MASK) != SSTS_DET_PRESENT) continue;
        uint32_t sig = port_r(p, PxSIG);
        if (sig == SIG_SATA) { found = p; break; }
    }
    if (found < 0) { klog("[ahci] no SATA drive on any port\n"); return -3; }
    g_port = found;
    klog("[ahci] SATA drive @ port "); klog_dec((uint64_t)found); klog("\n");

    /* --- 6. 分配 DMA 结构 --- */
    uint64_t clb = alloc_page();  /* 命令列表 1KB + 命令表 共享一页 */
    uint64_t fb  = alloc_page();  /* FIS 接收区 */
    uint64_t dma = alloc_page();  /* 数据缓冲 4KB */
    if (!clb || !fb || !dma) { klog("[ahci] DMA alloc failed\n"); return -4; }
    g_clb = (volatile hba_cmd_header_t *)clb;
    g_fb  = (volatile uint8_t *)fb;
    g_dma = (volatile uint8_t *)dma;
    /* 命令表放在 clb 页的 0x400 偏移（命令列表 32项*32B=1KB，后面空间放 ctb） */
    uint64_t ctb = clb + 0x400;
    g_ctb = (volatile hba_cmd_table_t *)ctb;

    /* --- 7. 停引擎、配置 CLB/FB、启引擎 --- */
    port_stop(g_port);
    port_w(g_port, PxCLB,  (uint32_t)(clb & 0xFFFFFFFFu));
    port_w(g_port, PxCLBU, (uint32_t)(clb >> 32));
    port_w(g_port, PxFB,   (uint32_t)(fb  & 0xFFFFFFFFu));
    port_w(g_port, PxFBU,  (uint32_t)(fb  >> 32));
    /* 命令头 slot 0 指向 ctb */
    g_clb[0].ctba  = (uint32_t)(ctb & 0xFFFFFFFFu);
    g_clb[0].ctbau = (uint32_t)(ctb >> 32);
    /* 清中断状态 */
    port_w(g_port, PxIS, port_r(g_port, PxIS));
    port_w(g_port, PxSERR, port_r(g_port, PxSERR));
    port_start(g_port);

    /* --- 8. IDENTIFY --- */
    uint64_t idbuf = (uint64_t)g_dma;
    if (ahci_issue(ATA_CMD_IDENTIFY, 0, 0, 0, idbuf, 512) != 0) {
        klog("[ahci] IDENTIFY failed\n");
        return -5;
    }
    /* IDENTIFY 数据：word 100-103 = LBA48 总扇区数（10*2=字节偏移 200） */
    volatile uint16_t *id = (volatile uint16_t *)g_dma;
    uint64_t sectors = (uint64_t)id[100]
                     | ((uint64_t)id[101] << 16)
                     | ((uint64_t)id[102] << 32)
                     | ((uint64_t)id[103] << 48);
    if (sectors == 0) {
        /* 回退 word 60-61（LBA28） */
        sectors = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    }
    g_sectors = sectors;
    g_present = 1;
    klog("[ahci] IDENTIFY ok, sectors="); klog_dec(sectors);
    klog(" ("); klog_dec(sectors / 2048); klog(" MB)\n");
    return 0;
}

int ahci_present(void) { return g_present; }
uint64_t ahci_sector_count(void) { return g_sectors; }

/* ================= 公共读写接口 ================= */

/* DMA 缓冲一页 4KB = 8 扇区，分块传输 */
#define AHCI_DMA_SECTORS 8

static void memcpy8(volatile uint8_t *d, const volatile uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

int ahci_read_sectors(uint64_t lba, uint32_t count, void *buf)
{
    if (!g_present) return -1;
    if (count == 0) return 0;
    uint8_t *out = (uint8_t *)buf;
    while (count > 0) {
        uint32_t chunk = (count > AHCI_DMA_SECTORS) ? AHCI_DMA_SECTORS : count;
        uint32_t bytes = chunk * 512u;
        int r = ahci_issue(ATA_CMD_READ_DMA_EXT, lba, chunk, 0,
                           (uint64_t)g_dma, bytes);
        if (r != 0) return r;
        memcpy8((volatile uint8_t *)out, g_dma, bytes);
        out   += bytes;
        lba   += chunk;
        count -= chunk;
    }
    return 0;
}

int ahci_write_sectors(uint64_t lba, uint32_t count, const void *buf)
{
    if (!g_present) return -1;
    if (count == 0) return 0;
    const uint8_t *in = (const uint8_t *)buf;
    while (count > 0) {
        uint32_t chunk = (count > AHCI_DMA_SECTORS) ? AHCI_DMA_SECTORS : count;
        uint32_t bytes = chunk * 512u;
        memcpy8(g_dma, (const volatile uint8_t *)in, bytes);
        int r = ahci_issue(ATA_CMD_WRITE_DMA_EXT, lba, chunk, 1,
                           (uint64_t)g_dma, bytes);
        if (r != 0) return r;
        in    += bytes;
        lba   += chunk;
        count -= chunk;
    }
    return 0;
}

int ahci_flush(void)
{
    if (!g_present) return -1;
    return ahci_issue(ATA_CMD_FLUSH_EXT, 0, 0, 0, 0, 0);
}

/* ================= headless 自测 ================= */
int ahci_selftest(void)
{
    klog("[ahci] === selftest begin ===\n");
    if (ahci_init() != 0) { klog("[ahci] selftest: init FAIL\n"); return -1; }
    if (!ahci_present()) { klog("[ahci] selftest: no disk FAIL\n"); return -1; }

    /* 写-读回校验：用最后一个安全扇区（避免碰到预期数据） */
    static uint8_t wbuf[512];
    static uint8_t rbuf[512];
    uint64_t test_lba = (g_sectors > 100) ? (g_sectors - 10) : 1;

    for (int i = 0; i < 512; i++) wbuf[i] = (uint8_t)((i * 7 + 0x5A) & 0xFF);
    for (int i = 0; i < 512; i++) rbuf[i] = 0;

    if (ahci_write_sectors(test_lba, 1, wbuf) != 0) {
        klog("[ahci] selftest: write FAIL\n"); return -2;
    }
    ahci_flush();
    if (ahci_read_sectors(test_lba, 1, rbuf) != 0) {
        klog("[ahci] selftest: read FAIL\n"); return -3;
    }
    for (int i = 0; i < 512; i++) {
        if (rbuf[i] != wbuf[i]) {
            klog("[ahci] selftest: verify MISMATCH @ "); klog_dec((uint64_t)i);
            klog("\n"); return -4;
        }
    }
    klog("[ahci] selftest: write/read/verify PASS @ lba=");
    klog_dec(test_lba); klog("\n");
    klog("[ahci] === selftest PASS ===\n");
    return 0;
}
