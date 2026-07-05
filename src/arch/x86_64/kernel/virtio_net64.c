/* =====================================================================
 * virtio_net64.c — virtio-net (legacy PCI) 网卡驱动
 *
 * 阶段 M1.2：基于 M1.1 的 PCI 枚举，实现 QEMU virtio-net 收发以太网帧。
 * 采用 legacy virtio 0.9.5 PCI 接口（BAR0 IO 空间），split virtqueue。
 *
 * 依赖：
 *   - pci.h        PCI 设备查找 / BAR / bus master 使能
 *   - virtio.h     virtqueue 结构 + legacy 寄存器偏移
 *   - pmm64        物理连续多页分配（identity map 低 512MB，phys==virt）
 *   - io.h         端口 IO（inb/outb/inw/outw/inl/outl）
 * ===================================================================== */

#include "../../../kernel/include/types.h"
#include "../../../kernel/include/pci.h"
#include "../../../kernel/include/virtio.h"
#include "../../../kernel/include/virtio_net.h"
#include "../../../kernel/include/io.h"
#include "../include/pmm64.h"

extern void early_serial64_write(const char *s);

/* ---- virtio legacy PCI 寄存器偏移均定义于 virtio.h ----
 * 本文件仅补充 virtio.h 未定义的别名（映射到已有宏）。 */
#define VIRTIO_PCI_QUEUE_SIZE       VIRTIO_PCI_QUEUE_NUM
#define VIRTIO_PCI_QUEUE_SELECT     VIRTIO_PCI_QUEUE_SEL
#define VIRTIO_PCI_CONFIG_OFF       VIRTIO_PCI_CONFIG
#define VIRTIO_STATUS_ACK           VIRTIO_STATUS_ACKNOWLEDGE

#define VIRTIO_PCI_QUEUE_PFN_SHIFT  12

#define VIRTIO_VENDOR               0x1AF4u
/* legacy device id: 0x1000 = network */
#define VIRTIO_NET_DEVICE_LEGACY    0x1000u

/* ==================== 全局状态 ==================== */

typedef struct virtio_net_dev {
    uint16_t     io_base;      /* BAR0 IO 端口基址 */
    uint8_t      mac[6];
    uint8_t      present;
    virtqueue_t  rxq;
    virtqueue_t  txq;
    /* RX：为每个描述符预留一块缓冲区 */
    uint8_t     *rx_bufs;      /* rx_count * RX_BUF_SIZE 连续内存 */
    uint64_t     rx_bufs_phys;
    /* TX：单块发送缓冲（含 virtio_net_hdr + 帧） */
    uint8_t     *tx_buf;
    uint64_t     tx_buf_phys;
} virtio_net_dev_t;

#define RX_BUF_SIZE   2048u
#define TX_BUF_SIZE   2048u

static virtio_net_dev_t g_vnet;

/* ==================== 小工具 ==================== */

static void vn_log(const char *s) { early_serial64_write(s); }

static void vn_hex(uint64_t v, int nbytes) {
    char buf[19];
    const char *hx = "0123456789abcdef";
    int i, pos = 0;
    buf[pos++] = '0'; buf[pos++] = 'x';
    for (i = nbytes * 2 - 1; i >= 0; i--) {
        buf[pos++] = hx[(v >> (i * 4)) & 0xF];
    }
    buf[pos] = 0;
    early_serial64_write(buf);
}

static void vn_dec(uint32_t v) {
    char buf[12]; int p = 0;
    if (v == 0) { early_serial64_write("0"); return; }
    char tmp[12]; int t = 0;
    while (v) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
    while (t) buf[p++] = tmp[--t];
    buf[p] = 0;
    early_serial64_write(buf);
}

static void *vn_memset(void *d, int c, uint64_t n) {
    uint8_t *p = (uint8_t *)d;
    while (n--) *p++ = (uint8_t)c;
    return d;
}

static void *vn_memcpy(void *d, const void *s, uint64_t n) {
    uint8_t *dp = (uint8_t *)d; const uint8_t *sp = (const uint8_t *)s;
    while (n--) *dp++ = *sp++;
    return d;
}

/* ---- legacy IO 寄存器访问（相对 io_base） ---- */
static inline uint32_t vn_r32(uint16_t off) { return inl(g_vnet.io_base + off); }
static inline uint16_t vn_r16(uint16_t off) { return inw(g_vnet.io_base + off); }
static inline uint8_t  vn_r8 (uint16_t off) { return inb(g_vnet.io_base + off); }
static inline void vn_w32(uint16_t off, uint32_t v) { outl(g_vnet.io_base + off, v); }
static inline void vn_w16(uint16_t off, uint16_t v) { outw(g_vnet.io_base + off, v); }
static inline void vn_w8 (uint16_t off, uint8_t v)  { outb(g_vnet.io_base + off, v); }

static void vn_set_status(uint8_t bits) {
    uint8_t cur = vn_r8(VIRTIO_PCI_STATUS);
    vn_w8(VIRTIO_PCI_STATUS, (uint8_t)(cur | bits));
}

/* ==================== virtqueue 分配与初始化 ====================
 * legacy split queue 布局（物理连续、4K 页对齐整体）：
 *   desc[queue_size]           每项 16 字节
 *   avail (flags+idx+ring[qs]+used_event)
 *   --- 填充到 4096 对齐 ---
 *   used  (flags+idx+ring[qs]+avail_event)
 * PFN = ring_phys >> 12 写入 QUEUE_PFN。
 */
static uint32_t vq_ring_size(uint16_t qs) {
    uint32_t desc = 16u * qs;
    uint32_t avail = 2u + 2u + 2u * qs + 2u;       /* flags,idx,ring,used_event */
    uint32_t used_off = (desc + avail + (VIRTIO_PCI_VRING_ALIGN - 1))
                        & ~(VIRTIO_PCI_VRING_ALIGN - 1);
    uint32_t used = 2u + 2u + 8u * qs + 2u;        /* flags,idx,ring,avail_event */
    return used_off + used;
}

static int vq_setup(virtqueue_t *vq, uint16_t index) {
    /* 选择队列，读队列大小 */
    vn_w16(VIRTIO_PCI_QUEUE_SELECT, index);
    uint16_t qs = vn_r16(VIRTIO_PCI_QUEUE_SIZE);
    if (qs == 0) {
        vn_log("[virtio-net] queue "); vn_dec(index); vn_log(" size=0 (不存在)\n");
        return -1;
    }

    uint32_t bytes = vq_ring_size(qs);
    uint32_t pages = (bytes + 4095u) / 4096u;
    x86_64_phys_addr_t phys = arch_x86_64_pmm_alloc_pages(pages);
    if (phys == 0) {
        vn_log("[virtio-net] vq 内存分配失败\n");
        return -1;
    }
    /* identity map 低 512MB，物理地址即可直接作虚拟地址访问 */
    uint8_t *base = (uint8_t *)(uintptr_t)phys;
    vn_memset(base, 0, pages * 4096u);

    uint32_t desc_bytes = 16u * qs;
    uint32_t avail_bytes = 2u + 2u + 2u * qs + 2u;
    uint32_t used_off = (desc_bytes + avail_bytes + (VIRTIO_PCI_VRING_ALIGN - 1))
                        & ~(VIRTIO_PCI_VRING_ALIGN - 1);

    vq->queue_size = qs;
    vq->queue_index = index;
    vq->desc  = (volatile virtq_desc_t  *)(base);
    vq->avail = (volatile virtq_avail_t *)(base + desc_bytes);
    vq->used  = (volatile virtq_used_t  *)(base + used_off);
    vq->ring_phys = phys;
    vq->ring_bytes = pages * 4096u;
    vq->last_used = 0;

    /* 初始化空闲描述符链 0->1->...->qs-1 */
    for (uint16_t i = 0; i < qs; i++) {
        vq->desc[i].next = (uint16_t)(i + 1);
    }
    vq->free_head = 0;
    vq->num_free = qs;

    /* 写 PFN，激活队列 */
    vn_w16(VIRTIO_PCI_QUEUE_SELECT, index);
    vn_w32(VIRTIO_PCI_QUEUE_PFN, (uint32_t)(phys >> VIRTIO_PCI_QUEUE_PFN_SHIFT));

    vn_log("[virtio-net] vq"); vn_dec(index);
    vn_log(" size="); vn_dec(qs);
    vn_log(" pfn="); vn_hex(phys >> 12, 4); vn_log("\n");
    return 0;
}

/* 分配一个空闲描述符，返回索引；无空闲返回 0xFFFF */
static uint16_t vq_alloc_desc(virtqueue_t *vq) {
    if (vq->num_free == 0) return 0xFFFF;
    uint16_t head = vq->free_head;
    vq->free_head = vq->desc[head].next;
    vq->num_free--;
    return head;
}

/* 归还一个描述符到空闲链 */
static void vq_free_desc(virtqueue_t *vq, uint16_t idx) {
    vq->desc[idx].next = vq->free_head;
    vq->free_head = idx;
    vq->num_free++;
}

/* 把 head 描述符放入 avail 环并通知设备 */
static void vq_submit(virtqueue_t *vq, uint16_t head) {
    uint16_t ai = vq->avail->idx % vq->queue_size;
    vq->avail->ring[ai] = head;
    __asm__ volatile("" ::: "memory");
    vq->avail->idx++;
    __asm__ volatile("" ::: "memory");
    vn_w16(VIRTIO_PCI_QUEUE_NOTIFY, vq->queue_index);
}

/* ==================== 接收缓冲填充 ==================== */
/* 为 RX 队列投递一个可写描述符，指向第 idx 块接收缓冲 */
static void rx_post(uint16_t buf_idx) {
    virtqueue_t *vq = &g_vnet.rxq;
    uint16_t d = vq_alloc_desc(vq);
    if (d == 0xFFFF) return;
    uint64_t bufp = g_vnet.rx_bufs_phys + (uint64_t)buf_idx * RX_BUF_SIZE;
    vq->desc[d].addr = bufp;
    vq->desc[d].len = RX_BUF_SIZE;
    vq->desc[d].flags = VIRTQ_DESC_F_WRITE; /* 设备写入 */
    vq->desc[d].next = 0;
    vq_submit(vq, d);
}

/* ==================== 设备初始化 ==================== */
static int vnet_setup(const pci_device_t *pdev) {
    /* 1. 获取 BAR0 IO 基址 */
    const pci_bar_t *bar0 = &pdev->bars[0];
    if (bar0->is_mmio != 0 || bar0->base == 0) {
        vn_log("[virtio-net] BAR0 非 IO 空间，不支持\n");
        return -1;
    }
    g_vnet.io_base = (uint16_t)bar0->base;

    /* 2. 使能 bus master + IO */
    pci_enable_bus_master((pci_device_t *)pdev);

    /* 3. 重置设备：status=0 */
    vn_w8(VIRTIO_PCI_STATUS, 0);
    vn_set_status(VIRTIO_STATUS_ACK);
    vn_set_status(VIRTIO_STATUS_DRIVER);

    /* 4. 特性协商：仅接受 MAC + STATUS（不要 MRG_RXBUF，简化头部） */
    uint32_t host_features = vn_r32(VIRTIO_PCI_HOST_FEATURES);
    uint32_t want = 0;
    if (host_features & VIRTIO_NET_F_MAC)    want |= VIRTIO_NET_F_MAC;
    if (host_features & VIRTIO_NET_F_STATUS) want |= VIRTIO_NET_F_STATUS;
    vn_w32(VIRTIO_PCI_GUEST_FEATURES, want);

    vn_log("[virtio-net] host_features="); vn_hex(host_features, 4);
    vn_log(" guest="); vn_hex(want, 4); vn_log("\n");

    /* 5. 读 MAC（设备配置区前 6 字节，无 MSI-X 时从 0x14） */
    if (want & VIRTIO_NET_F_MAC) {
        for (int i = 0; i < 6; i++)
            g_vnet.mac[i] = vn_r8((uint16_t)(VIRTIO_PCI_CONFIG_OFF + i));
    } else {
        for (int i = 0; i < 6; i++) g_vnet.mac[i] = 0;
    }
    vn_log("[virtio-net] MAC=");
    for (int i = 0; i < 6; i++) { if (i) vn_log(":"); vn_hex(g_vnet.mac[i], 1); }
    vn_log("\n");

    /* 6. 建立 RX / TX 队列 */
    if (vq_setup(&g_vnet.rxq, VIRTIO_NET_QUEUE_RX) != 0) return -1;
    if (vq_setup(&g_vnet.txq, VIRTIO_NET_QUEUE_TX) != 0) return -1;

    /* 7. 分配 RX 缓冲区（每个描述符一块）并全部投递 */
    uint16_t rxn = g_vnet.rxq.queue_size;
    uint32_t rx_total = (uint32_t)rxn * RX_BUF_SIZE;
    uint32_t rx_pages = (rx_total + 4095u) / 4096u;
    x86_64_phys_addr_t rxp = arch_x86_64_pmm_alloc_pages(rx_pages);
    if (rxp == 0) { vn_log("[virtio-net] RX 缓冲分配失败\n"); return -1; }
    g_vnet.rx_bufs = (uint8_t *)(uintptr_t)rxp;
    g_vnet.rx_bufs_phys = rxp;
    vn_memset(g_vnet.rx_bufs, 0, rx_pages * 4096u);
    for (uint16_t i = 0; i < rxn; i++) rx_post(i);

    /* 8. 分配 TX 单块缓冲 */
    x86_64_phys_addr_t txp = arch_x86_64_pmm_alloc_pages(1);
    if (txp == 0) { vn_log("[virtio-net] TX 缓冲分配失败\n"); return -1; }
    g_vnet.tx_buf = (uint8_t *)(uintptr_t)txp;
    g_vnet.tx_buf_phys = txp;
    vn_memset(g_vnet.tx_buf, 0, 4096);

    /* 9. DRIVER_OK */
    vn_set_status(VIRTIO_STATUS_DRIVER_OK);
    g_vnet.present = 1;
    vn_log("[virtio-net] 初始化完成，DRIVER_OK\n");
    return 0;
}

/* ==================== 发送 ==================== */
int virtio_net_send(const void *frame, uint32_t len) {
    if (!g_vnet.present) return -1;
    if (len == 0 || len > (TX_BUF_SIZE - sizeof(virtio_net_hdr_t))) return -1;

    virtqueue_t *vq = &g_vnet.txq;

    /* 先回收已完成的 TX 描述符 */
    while (vq->last_used != vq->used->idx) {
        uint16_t ui = vq->last_used % vq->queue_size;
        uint16_t did = (uint16_t)vq->used->ring[ui].id;
        vq_free_desc(vq, did);
        vq->last_used++;
    }

    /* 组包：virtio_net_hdr(全0) + 以太网帧 */
    virtio_net_hdr_t *hdr = (virtio_net_hdr_t *)g_vnet.tx_buf;
    vn_memset(hdr, 0, sizeof(*hdr));
    vn_memcpy(g_vnet.tx_buf + sizeof(*hdr), frame, len);
    uint32_t total = (uint32_t)sizeof(*hdr) + len;

    uint16_t d = vq_alloc_desc(vq);
    if (d == 0xFFFF) { vn_log("[virtio-net] TX 无空闲描述符\n"); return -1; }
    vq->desc[d].addr = g_vnet.tx_buf_phys;
    vq->desc[d].len = total;
    vq->desc[d].flags = 0; /* 设备只读 */
    vq->desc[d].next = 0;
    vq_submit(vq, d);
    return 0;
}

/* ==================== 接收（轮询） ==================== */
int virtio_net_poll_recv(void *buf, uint32_t buf_len) {
    if (!g_vnet.present) return -1;
    virtqueue_t *vq = &g_vnet.rxq;

    if (vq->last_used == vq->used->idx) return 0; /* 无新包 */

    uint16_t ui = vq->last_used % vq->queue_size;
    uint32_t did = vq->used->ring[ui].id;
    uint32_t wlen = vq->used->ring[ui].len; /* 含 virtio_net_hdr */
    vq->last_used++;

    /* 计算该描述符对应的缓冲块索引 */
    uint64_t bufp = vq->desc[did].addr;
    uint16_t blk = (uint16_t)((bufp - g_vnet.rx_bufs_phys) / RX_BUF_SIZE);

    int ret = 0;
    if (wlen > sizeof(virtio_net_hdr_t)) {
        uint32_t frame_len = wlen - (uint32_t)sizeof(virtio_net_hdr_t);
        if (frame_len > buf_len) frame_len = buf_len;
        uint8_t *src = g_vnet.rx_bufs + (uint64_t)blk * RX_BUF_SIZE
                       + sizeof(virtio_net_hdr_t);
        vn_memcpy(buf, src, frame_len);
        ret = (int)frame_len;
    }

    /* 归还描述符并重新投递该缓冲块 */
    vq_free_desc(vq, (uint16_t)did);
    rx_post(blk);
    return ret;
}

/* ==================== 公开 API ==================== */
void virtio_net_init(void) {
    vn_memset(&g_vnet, 0, sizeof(g_vnet));
    const pci_device_t *pdev = pci_find_by_id(VIRTIO_VENDOR, VIRTIO_NET_DEVICE_LEGACY);
    if (!pdev) {
        vn_log("[virtio-net] 未发现 virtio-net 设备（1af4:1000）\n");
        return;
    }
    vn_log("[virtio-net] 发现设备 "); vn_hex(pdev->vendor_id, 2);
    vn_log(":"); vn_hex(pdev->device_id, 2); vn_log("\n");
    if (vnet_setup(pdev) != 0) {
        vn_log("[virtio-net] 初始化失败\n");
        vn_set_status(VIRTIO_STATUS_FAILED);
        g_vnet.present = 0;
    }
}

uint32_t virtio_net_device_count(void) { return g_vnet.present ? 1u : 0u; }

int virtio_net_get_mac(uint8_t out_mac[6]) {
    if (!g_vnet.present) return -1;
    for (int i = 0; i < 6; i++) out_mac[i] = g_vnet.mac[i];
    return 0;
}

void virtio_net_dump(void) {
    if (!g_vnet.present) { vn_log("[virtio-net] 无设备\n"); return; }
    vn_log("[virtio-net] io_base="); vn_hex(g_vnet.io_base, 2);
    vn_log(" MAC=");
    for (int i = 0; i < 6; i++) { if (i) vn_log(":"); vn_hex(g_vnet.mac[i], 1); }
    vn_log(" rxq="); vn_dec(g_vnet.rxq.queue_size);
    vn_log(" txq="); vn_dec(g_vnet.txq.queue_size);
    vn_log("\n");
}
