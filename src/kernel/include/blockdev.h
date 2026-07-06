/* ============================================================
 * openos - 块设备框架 (Phase 3)
 * ============================================================ */

#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#include "types.h"

#define BLOCKDEV_NAME_MAX 32
#define BLOCKDEV_MAX      16
#define BLOCKDEV_SECTOR_SIZE_DEFAULT 512

struct blockdev;

typedef int (*blockdev_open_fn_t)(struct blockdev *dev);
typedef int (*blockdev_close_fn_t)(struct blockdev *dev);
typedef int (*blockdev_read_blocks_fn_t)(struct blockdev *dev, uint32_t lba, uint32_t count, void *buf);
typedef int (*blockdev_write_blocks_fn_t)(struct blockdev *dev, uint32_t lba, uint32_t count, const void *buf);
typedef int (*blockdev_ioctl_fn_t)(struct blockdev *dev, uint32_t request, void *arg);

typedef struct blockdev_ops {
    blockdev_open_fn_t         open;
    blockdev_close_fn_t        close;
    blockdev_read_blocks_fn_t  read_blocks;
    blockdev_write_blocks_fn_t write_blocks;
    blockdev_ioctl_fn_t        ioctl;
} blockdev_ops_t;

typedef struct blockdev {
    char name[BLOCKDEV_NAME_MAX];
    uint32_t major;
    uint32_t minor;
    uint32_t flags;
    uint32_t ref_count;
    uint32_t sector_size;
    uint32_t sector_count;
    blockdev_ops_t *ops;
    void *private_data;
} blockdev_t;

typedef struct blockdev_cache_stats {
    uint32_t entries;
    uint32_t valid_entries;
    uint32_t dirty_entries;
    uint32_t read_hits;
    uint32_t read_misses;
    uint32_t write_hits;
    uint32_t write_misses;
    uint32_t evictions;
    uint32_t flushes;
} blockdev_cache_stats_t;

void blockdev_init(void);
int blockdev_register(const char *name, uint32_t major, uint32_t minor,
                      uint32_t sector_size, uint32_t sector_count,
                      blockdev_ops_t *ops, void *private_data);
int blockdev_unregister(const char *name);
blockdev_t *blockdev_find(const char *name);
blockdev_t *blockdev_find_by_devno(uint32_t major, uint32_t minor);
blockdev_t *blockdev_get_by_index(uint32_t index);
uint32_t blockdev_count(void);

int blockdev_open(blockdev_t *dev);
int blockdev_close(blockdev_t *dev);
int blockdev_read_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, void *buf);
int blockdev_write_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, const void *buf);
int blockdev_flush(blockdev_t *dev);
int blockdev_flush_all(void);
int blockdev_invalidate(blockdev_t *dev);
int blockdev_invalidate_all(void);
void blockdev_cache_get_stats(blockdev_cache_stats_t *stats);
void blockdev_cache_reset_stats(void);
int blockdev_ioctl(blockdev_t *dev, uint32_t request, void *arg);
uint32_t blockdev_size_bytes(blockdev_t *dev);

/* 注册内核内置块设备，并创建 /dev 节点 */
void blockdev_register_builtin_devices(void);

/* x86_64 硬件探测注册（blockdev_hw.c 实现）：
 * 依据各驱动 *_present() 结果注册 nvme0/sda/hda/hdb，返回注册数量。 */
int blockdev_register_hw_devices(void);

#endif /* BLOCKDEV_H */
