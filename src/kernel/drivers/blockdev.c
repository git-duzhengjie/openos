/* ============================================================
 * openos - 块设备抽象层实现 (M2.2)
 * ------------------------------------------------------------
 * 提供统一的块设备注册表与分发层，屏蔽底层 NVMe / AHCI / ATA
 * 具体驱动差异。上层（FAT32/VFS）通过 blockdev_t 句柄进行设备
 * 无关的 read/write/flush，不再直连某一具体驱动。
 *
 * MVP 定位：
 *   - 固定容量注册表（BLOCKDEV_MAX），无动态内存分配
 *   - 直通式分发（本层不做块缓存，缓存/合并留待后续里程碑）
 *   - flush 通过 ioctl(BLOCKDEV_IOCTL_FLUSH) 下发到驱动
 * ============================================================ */

#include "blockdev.h"

/* ioctl 请求码：驱动层据此实现落盘刷新等操作 */
#define BLOCKDEV_IOCTL_FLUSH 0x0001u

/* ---- 注册表（静态分配，避免早期无堆环境依赖） ---- */
static blockdev_t g_devs[BLOCKDEV_MAX];
static int        g_dev_used[BLOCKDEV_MAX];
static uint32_t   g_dev_count;
static int        g_inited;

/* ---- 极简字符串工具（内核早期环境，不依赖 libc） ---- */
static void bd_strcpy_bounded(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (src) {
        for (; i + 1 < cap && src[i]; i++) dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int bd_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

void blockdev_init(void) {
    uint32_t i;
    for (i = 0; i < BLOCKDEV_MAX; i++) {
        g_dev_used[i] = 0;
    }
    g_dev_count = 0;
    g_inited = 1;
}

int blockdev_register(const char *name, uint32_t major, uint32_t minor,
                      uint32_t sector_size, uint32_t sector_count,
                      blockdev_ops_t *ops, void *private_data) {
    uint32_t i;
    if (!g_inited) blockdev_init();
    if (!name || !ops || sector_size == 0) return -1;
    /* 名称去重 */
    if (blockdev_find(name)) return -2;
    /* 找空槽 */
    for (i = 0; i < BLOCKDEV_MAX; i++) {
        if (!g_dev_used[i]) {
            blockdev_t *d = &g_devs[i];
            bd_strcpy_bounded(d->name, name, BLOCKDEV_NAME_MAX);
            d->major = major;
            d->minor = minor;
            d->flags = 0;
            d->ref_count = 0;
            d->sector_size = sector_size;
            d->sector_count = sector_count;
            d->ops = ops;
            d->private_data = private_data;
            g_dev_used[i] = 1;
            g_dev_count++;
            return (int)i;
        }
    }
    return -3; /* 表满 */
}

int blockdev_unregister(const char *name) {
    uint32_t i;
    if (!name) return -1;
    for (i = 0; i < BLOCKDEV_MAX; i++) {
        if (g_dev_used[i] && bd_streq(g_devs[i].name, name)) {
            g_dev_used[i] = 0;
            if (g_dev_count) g_dev_count--;
            return 0;
        }
    }
    return -1;
}

blockdev_t *blockdev_find(const char *name) {
    uint32_t i;
    if (!name) return 0;
    for (i = 0; i < BLOCKDEV_MAX; i++) {
        if (g_dev_used[i] && bd_streq(g_devs[i].name, name))
            return &g_devs[i];
    }
    return 0;
}

blockdev_t *blockdev_find_by_devno(uint32_t major, uint32_t minor) {
    uint32_t i;
    for (i = 0; i < BLOCKDEV_MAX; i++) {
        if (g_dev_used[i] && g_devs[i].major == major && g_devs[i].minor == minor)
            return &g_devs[i];
    }
    return 0;
}

blockdev_t *blockdev_get_by_index(uint32_t index) {
    uint32_t i, seen = 0;
    for (i = 0; i < BLOCKDEV_MAX; i++) {
        if (g_dev_used[i]) {
            if (seen == index) return &g_devs[i];
            seen++;
        }
    }
    return 0;
}

uint32_t blockdev_count(void) {
    return g_dev_count;
}

int blockdev_open(blockdev_t *dev) {
    if (!dev || !dev->ops) return -1;
    dev->ref_count++;
    if (dev->ops->open) return dev->ops->open(dev);
    return 0;
}

int blockdev_close(blockdev_t *dev) {
    if (!dev || !dev->ops) return -1;
    if (dev->ref_count) dev->ref_count--;
    if (dev->ops->close) return dev->ops->close(dev);
    return 0;
}

int blockdev_read_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, void *buf) {
    if (!dev || !dev->ops || !dev->ops->read_blocks || !buf) return -1;
    if (count == 0) return 0;
    if (dev->sector_count && (lba + count) > dev->sector_count) return -2;
    return dev->ops->read_blocks(dev, lba, count, buf);
}

int blockdev_write_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, const void *buf) {
    if (!dev || !dev->ops || !dev->ops->write_blocks || !buf) return -1;
    if (count == 0) return 0;
    if (dev->sector_count && (lba + count) > dev->sector_count) return -2;
    return dev->ops->write_blocks(dev, lba, count, buf);
}

int blockdev_flush(blockdev_t *dev) {
    if (!dev || !dev->ops) return -1;
    if (dev->ops->ioctl) return dev->ops->ioctl(dev, BLOCKDEV_IOCTL_FLUSH, 0);
    return 0;
}

int blockdev_flush_all(void) {
    uint32_t i;
    int rc = 0;
    for (i = 0; i < BLOCKDEV_MAX; i++) {
        if (g_dev_used[i]) {
            int r = blockdev_flush(&g_devs[i]);
            if (r < 0) rc = r;
        }
    }
    return rc;
}

int blockdev_invalidate(blockdev_t *dev) {
    (void)dev; /* 本层暂无缓存 */
    return 0;
}

int blockdev_invalidate_all(void) {
    return 0;
}

void blockdev_cache_get_stats(blockdev_cache_stats_t *stats) {
    uint32_t i;
    if (!stats) return;
    for (i = 0; i < sizeof(*stats) / sizeof(uint32_t); i++)
        ((uint32_t *)stats)[i] = 0;
}

void blockdev_cache_reset_stats(void) {
    /* 无缓存，空实现 */
}

int blockdev_ioctl(blockdev_t *dev, uint32_t request, void *arg) {
    if (!dev || !dev->ops || !dev->ops->ioctl) return -1;
    return dev->ops->ioctl(dev, request, arg);
}

uint32_t blockdev_size_bytes(blockdev_t *dev) {
    if (!dev) return 0;
    return dev->sector_size * dev->sector_count;
}
