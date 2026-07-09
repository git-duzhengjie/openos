/* ============================================================
 * openos - USB Mass Storage Class (BOT + SCSI) 驱动实现
 * ------------------------------------------------------------
 * 依赖 xhci64.c 的 bulk 传输原语 + blockdev 抽象层
 * ============================================================ */

#include "usb_msc.h"
#include "xhci64.h"
#include "serial.h"
#include "pmm64.h"

/* ---- 全局 MSC 设备表 ---- */
static usb_msc_dev_t g_msc_devs[USB_MSC_MAX_DEVS];

/* ---- 传输用 DMA 缓冲（恒等映射物理地址）---- */
/* 必须用 pmm 分配恒等映射物理页(virt==phys)，不能用内核高半区静态数组，
 * 否则把内核 vaddr(0xFFFFFFFF8...) 当 DMA 物理地址会让 xHCI 读到错误物理页。 */
static uint8_t *g_cbw_buf  = 0;   /* 64 B */
static uint8_t *g_csw_buf  = 0;   /* 64 B */
static uint8_t *g_data_buf = 0;   /* 512 B */

/* 分配 DMA 缓冲（一页足够放下 CBW+CSW+DATA），恒等映射物理地址 */
static int msc_dma_init(void) {
    if (g_cbw_buf) return 0;                 /* 已初始化 */
    uint64_t p = arch_x86_64_pmm_alloc_pages(1);
    if (!p) return -1;
    volatile uint8_t *b = (volatile uint8_t *)p;
    for (int i = 0; i < 4096; i++) b[i] = 0;
    g_cbw_buf  = (uint8_t *)(uintptr_t)(p + 0);      /* [0..63]   */
    g_csw_buf  = (uint8_t *)(uintptr_t)(p + 64);     /* [64..127] */
    g_data_buf = (uint8_t *)(uintptr_t)(p + 128);    /* [128..639]*/
    return 0;
}

/* ---- 日志宏 ---- */
#define MLOG(s)      do { serial_write("[usb-msc] "); serial_write(s); serial_write("\n"); } while (0)
#define MLOG_NN(s)   do { serial_write("[usb-msc] "); serial_write(s); } while (0)

static void mdec(uint32_t v) {
    char b[12]; int i = 0;
    if (v == 0) { serial_write("0"); return; }
    while (v && i < 11) { b[i++] = '0' + (v % 10); v /= 10; }
    char o[12]; int j = 0;
    while (i > 0) o[j++] = b[--i];
    o[j] = 0; serial_write(o);
}

/* 小端读 32 位 */
static inline void put_le32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
/* 大端读 32 位 */
static inline uint32_t get_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* ============================================================
 * BOT 传输核心：一次完整命令 = CBW → (数据) → CSW
 * ============================================================ */

/* 找到 dev 对应的 xHCI MSC idx（按 slot_id 匹配）。返回 <0 失败。 */
static int msc_xhci_idx(usb_msc_dev_t *dev) {
    uint32_t n = xhci_msc_device_count();
    for (uint32_t i = 0; i < n; i++) {
        uint32_t slot = 0; uint8_t iface = 0, ei = 0, eo = 0;
        if (xhci_msc_device_info(i, &slot, &iface, &ei, &eo) == 0) {
            if (slot == dev->slot_id && iface == dev->iface_num)
                return (int)i;
        }
    }
    return -1;
}

/* 执行一次 BOT 命令。
 *   cb/cb_len：SCSI 命令块
 *   data：数据缓冲（可 NULL），data_len：期望字节数
 *   dir_in：1=数据方向 IN，0=OUT
 * 返回 0 成功（CSW status=0），<0 失败。 */
static int bot_command(usb_msc_dev_t *dev, const uint8_t *cb, uint8_t cb_len,
                       uint8_t *data, uint32_t data_len, int dir_in) {
    int idx = msc_xhci_idx(dev);
    if (idx < 0) return -1;

    /* ---- 1) 组装并发送 CBW（Bulk OUT，31 字节）---- */
    usb_msc_cbw_t *cbw = (usb_msc_cbw_t *)g_cbw_buf;
    for (int i = 0; i < 31; i++) ((uint8_t *)cbw)[i] = 0;
    cbw->dCBWSignature = CBW_SIGNATURE;
    cbw->dCBWTag = ++dev->tag;
    cbw->dCBWDataTransferLength = data_len;
    cbw->bmCBWFlags = dir_in ? CBW_FLAG_DATA_IN : CBW_FLAG_DATA_OUT;
    cbw->bCBWLUN = 0;
    cbw->bCBWCBLength = cb_len;
    for (int i = 0; i < cb_len && i < 16; i++) cbw->CBWCB[i] = cb[i];

    int r = xhci_msc_bulk_transfer((uint32_t)idx, 0 /*OUT*/,
                                   (uint64_t)(uintptr_t)g_cbw_buf, 31);
    if (r < 0) { MLOG("CBW send FAIL"); return -2; }

    /* ---- 2) 数据阶段（可选）---- */
    if (data_len > 0 && data) {
        if (dir_in) {
            r = xhci_msc_bulk_transfer((uint32_t)idx, 1 /*IN*/,
                                       (uint64_t)(uintptr_t)data, data_len);
            if (r < 0) { MLOG("DATA-IN FAIL"); return -3; }
        } else {
            r = xhci_msc_bulk_transfer((uint32_t)idx, 0 /*OUT*/,
                                       (uint64_t)(uintptr_t)data, data_len);
            if (r < 0) { MLOG("DATA-OUT FAIL"); return -3; }
        }
    }

    /* ---- 3) 接收 CSW（Bulk IN，13 字节）---- */
    for (int i = 0; i < 13; i++) g_csw_buf[i] = 0;
    r = xhci_msc_bulk_transfer((uint32_t)idx, 1 /*IN*/,
                               (uint64_t)(uintptr_t)g_csw_buf, 13);
    if (r < 0) { MLOG("CSW recv FAIL"); return -4; }

    usb_msc_csw_t *csw = (usb_msc_csw_t *)g_csw_buf;
    if (csw->dCSWSignature != CSW_SIGNATURE) { MLOG("CSW bad sig"); return -5; }
    if (csw->dCSWTag != cbw->dCBWTag) { MLOG("CSW tag mismatch"); return -6; }
    if (csw->bCSWStatus != CSW_STATUS_PASSED) return -7;
    return 0;
}

/* ============================================================
 * SCSI 命令封装
 * ============================================================ */

/* TEST UNIT READY（无数据） */
static int scsi_test_unit_ready(usb_msc_dev_t *dev) {
    uint8_t cb[6] = { SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0 };
    return bot_command(dev, cb, 6, 0, 0, 1);
}

/* INQUIRY（读 36 字节标准数据） */
static int scsi_inquiry(usb_msc_dev_t *dev) {
    uint8_t cb[6] = { SCSI_INQUIRY, 0, 0, 0, 36, 0 };
    int r = bot_command(dev, cb, 6, g_data_buf, 36, 1);
    if (r < 0) return r;
    scsi_inquiry_data_t *inq = (scsi_inquiry_data_t *)g_data_buf;
    for (int i = 0; i < 8; i++) dev->vendor[i] = inq->vendor_id[i];
    dev->vendor[8] = 0;
    for (int i = 0; i < 16; i++) dev->product[i] = inq->product_id[i];
    dev->product[16] = 0;
    return 0;
}

/* REQUEST SENSE（读 18 字节，用于清 CHECK CONDITION） */
static int scsi_request_sense(usb_msc_dev_t *dev) {
    uint8_t cb[6] = { SCSI_REQUEST_SENSE, 0, 0, 0, 18, 0 };
    return bot_command(dev, cb, 6, g_data_buf, 18, 1);
}

/* READ CAPACITY(10)（读 8 字节，大端） */
static int scsi_read_capacity10(usb_msc_dev_t *dev) {
    uint8_t cb[10] = { SCSI_READ_CAPACITY10, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    int r = bot_command(dev, cb, 10, g_data_buf, 8, 1);
    if (r < 0) return r;
    uint32_t last_lba = get_be32(&g_data_buf[0]);
    uint32_t blk_len  = get_be32(&g_data_buf[4]);
    dev->block_count = last_lba + 1;
    dev->block_size  = blk_len ? blk_len : 512;
    return 0;
}

/* ============================================================
 * 块读写（分块，每次限 g_data_buf 容量 512B）
 * ============================================================ */

int usb_msc_read_blocks(usb_msc_dev_t *dev, uint32_t lba, uint32_t count, void *buf) {
    if (!dev || !dev->used) return -1;
    uint8_t *out = (uint8_t *)buf;
    uint32_t bs = dev->block_size ? dev->block_size : 512;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t cur = lba + i;
        /* READ(10)：opcode, flags, LBA(be32), grp, len(be16), ctrl */
        uint8_t cb[10];
        cb[0] = SCSI_READ_10; cb[1] = 0;
        cb[2] = (cur >> 24) & 0xFF; cb[3] = (cur >> 16) & 0xFF;
        cb[4] = (cur >> 8) & 0xFF;  cb[5] = cur & 0xFF;
        cb[6] = 0; cb[7] = 0; cb[8] = 1; cb[9] = 0;   /* 1 block */
        int r = bot_command(dev, cb, 10, g_data_buf, bs, 1);
        if (r < 0) { MLOG("READ10 FAIL"); return -2; }
        for (uint32_t j = 0; j < bs; j++) out[i * bs + j] = g_data_buf[j];
    }
    return 0;
}

int usb_msc_write_blocks(usb_msc_dev_t *dev, uint32_t lba, uint32_t count, const void *buf) {
    if (!dev || !dev->used) return -1;
    const uint8_t *in = (const uint8_t *)buf;
    uint32_t bs = dev->block_size ? dev->block_size : 512;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t cur = lba + i;
        for (uint32_t j = 0; j < bs; j++) g_data_buf[j] = in[i * bs + j];
        uint8_t cb[10];
        cb[0] = SCSI_WRITE_10; cb[1] = 0;
        cb[2] = (cur >> 24) & 0xFF; cb[3] = (cur >> 16) & 0xFF;
        cb[4] = (cur >> 8) & 0xFF;  cb[5] = cur & 0xFF;
        cb[6] = 0; cb[7] = 0; cb[8] = 1; cb[9] = 0;
        int r = bot_command(dev, cb, 10, g_data_buf, bs, 0);
        if (r < 0) { MLOG("WRITE10 FAIL"); return -2; }
    }
    return 0;
}

/* ============================================================
 * attach：枚举回调，初始化一个 MSC 逻辑设备
 * ============================================================ */

int usb_msc_attach(uint32_t slot_id, uint8_t iface_num,
                   uint8_t ep_in, uint8_t ep_out) {
    /* 找空槽 */
    usb_msc_dev_t *dev = 0;
    for (int i = 0; i < USB_MSC_MAX_DEVS; i++) {
        if (!g_msc_devs[i].used) { dev = &g_msc_devs[i]; break; }
    }
    if (!dev) { MLOG("no free slot"); return -1; }

    for (uint32_t i = 0; i < sizeof(*dev); i++) ((uint8_t *)dev)[i] = 0;
    dev->used = 1;
    dev->slot_id = slot_id;
    dev->iface_num = iface_num;
    dev->ep_in = ep_in;
    dev->ep_out = ep_out;

    MLOG_NN("attach slot="); mdec(slot_id);
    serial_write(" iface="); mdec(iface_num); serial_write("\n");

    /* 在 xHCI 层找到对应 idx 并配置 bulk 端点 */
    int idx = msc_xhci_idx(dev);
    if (idx < 0) { MLOG("xhci idx not found"); dev->used = 0; return -2; }
    if (xhci_msc_configure((uint32_t)idx) != 0) {
        MLOG("configure bulk EP FAIL"); dev->used = 0; return -3;
    }
    MLOG("bulk EP configured");

    /* Get Max LUN（class 请求，wIndex=iface，读 1 字节） */
    int r = xhci_msc_control((uint32_t)idx,
                             0xA1 /* D2H|class|interface */,
                             USB_MSC_REQ_GET_MAX_LUN, 0, iface_num, 1,
                             (uint64_t)(uintptr_t)g_data_buf);
    if (r >= 0) dev->max_lun = g_data_buf[0];
    else dev->max_lun = 0;   /* STALL 也视为单 LUN */
    MLOG_NN("max_lun="); mdec(dev->max_lun); serial_write("\n");

    /* INQUIRY */
    if (scsi_inquiry(dev) != 0) { MLOG("INQUIRY FAIL"); dev->used = 0; return -4; }
    MLOG_NN("vendor="); serial_write(dev->vendor);
    serial_write(" product="); serial_write(dev->product); serial_write("\n");

    /* TEST UNIT READY（重试几次，首次常返 CHECK CONDITION） */
    int ready = 0;
    for (int t = 0; t < 5; t++) {
        if (scsi_test_unit_ready(dev) == 0) { ready = 1; break; }
        scsi_request_sense(dev);   /* 清 sense */
    }
    if (!ready) MLOG("unit not ready (continue anyway)");

    /* READ CAPACITY(10) */
    if (scsi_read_capacity10(dev) != 0) {
        MLOG("READ CAPACITY FAIL"); dev->used = 0; return -5;
    }
    MLOG_NN("capacity blocks="); mdec(dev->block_count);
    serial_write(" block_size="); mdec(dev->block_size);
    serial_write(" total_MB=");
    mdec((uint32_t)(((uint64_t)dev->block_count * dev->block_size) >> 20));
    serial_write("\n");

    MLOG("attach OK, U-disk ready");
    return 0;
}

/* ============================================================
 * 查询接口
 * ============================================================ */

int usb_msc_present(void) {
    for (int i = 0; i < USB_MSC_MAX_DEVS; i++)
        if (g_msc_devs[i].used) return 1;
    return 0;
}

uint32_t usb_msc_count(void) {
    uint32_t n = 0;
    for (int i = 0; i < USB_MSC_MAX_DEVS; i++)
        if (g_msc_devs[i].used) n++;
    return n;
}

usb_msc_dev_t *usb_msc_get(uint32_t index) {
    uint32_t n = 0;
    for (int i = 0; i < USB_MSC_MAX_DEVS; i++) {
        if (g_msc_devs[i].used) {
            if (n == index) return &g_msc_devs[i];
            n++;
        }
    }
    return 0;
}

/* ============================================================
 * 初始化：遍历 xHCI 已枚举的 MSC 接口，逐个 attach
 * ============================================================ */
void usb_msc_init(void) {
    if (msc_dma_init() != 0) { MLOG("DMA buffer alloc failed"); return; }
    uint32_t n = xhci_msc_device_count();
    if (n == 0) { MLOG("no USB mass-storage device found"); return; }
    MLOG_NN("found "); mdec(n); serial_write(" mass-storage interface(s)\n");
    for (uint32_t i = 0; i < n; i++) {
        uint32_t slot = 0; uint8_t iface = 0, ep_in = 0, ep_out = 0;
        if (xhci_msc_device_info(i, &slot, &iface, &ep_in, &ep_out) != 0)
            continue;
        usb_msc_attach(slot, iface, ep_in, ep_out);
    }

    /* ---- 读写自检：写图案 -> 读回比对 -> 原样恢复，不破坏用户数据 ---- */
    usb_msc_dev_t *d = usb_msc_get(0);
    if (d && d->used && d->block_count > 0) {
        uint32_t bs = d->block_size ? d->block_size : 512;
        uint32_t test_lba = (d->block_count > 16) ? (d->block_count - 8) : 0;
        static uint8_t bak[512], pat[512], chk[512];
        if (bs <= 512) {
            /* 1) 备份原扇区 */
            if (usb_msc_read_blocks(d, test_lba, 1, bak) != 0) {
                MLOG("selftest: backup read FAIL"); return;
            }
            /* 2) 写入测试图案 */
            for (uint32_t i = 0; i < bs; i++) pat[i] = (uint8_t)(i ^ 0xA5);
            if (usb_msc_write_blocks(d, test_lba, 1, pat) != 0) {
                MLOG("selftest: pattern write FAIL"); return;
            }
            /* 3) 读回比对 */
            for (uint32_t i = 0; i < bs; i++) chk[i] = 0;
            if (usb_msc_read_blocks(d, test_lba, 1, chk) != 0) {
                MLOG("selftest: verify read FAIL"); return;
            }
            int ok = 1;
            for (uint32_t i = 0; i < bs; i++)
                if (chk[i] != pat[i]) { ok = 0; break; }
            /* 4) 恢复原始数据 */
            usb_msc_write_blocks(d, test_lba, 1, bak);
            if (ok) { MLOG_NN("selftest: WRITE/READ/VERIFY PASS @ lba="); mdec(test_lba); serial_write("\n"); }
            else    { MLOG("selftest: VERIFY MISMATCH"); }
        }
    }
}
