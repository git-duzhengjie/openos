/* ============================================================
 * openos - 字符设备框架 (Phase 3)
 * ============================================================ */

#ifndef CHARDEV_H
#define CHARDEV_H

#include "types.h"

#define CHARDEV_NAME_MAX 32
#define CHARDEV_MAX      32

struct chardev;

typedef int (*chardev_open_fn_t)(struct chardev *dev);
typedef int (*chardev_close_fn_t)(struct chardev *dev);
typedef int (*chardev_read_fn_t)(struct chardev *dev, void *buf, uint32_t count);
typedef int (*chardev_write_fn_t)(struct chardev *dev, const void *buf, uint32_t count);

typedef struct chardev_ops {
    chardev_open_fn_t  open;
    chardev_close_fn_t close;
    chardev_read_fn_t  read;
    chardev_write_fn_t write;
} chardev_ops_t;

typedef struct chardev {
    char name[CHARDEV_NAME_MAX];
    uint32_t major;
    uint32_t minor;
    uint32_t flags;
    uint32_t ref_count;
    chardev_ops_t *ops;
    void *private_data;
} chardev_t;

void chardev_init(void);
int chardev_register(const char *name, uint32_t major, uint32_t minor,
                     chardev_ops_t *ops, void *private_data);
int chardev_unregister(const char *name);
chardev_t *chardev_find(const char *name);
chardev_t *chardev_find_by_devno(uint32_t major, uint32_t minor);
chardev_t *chardev_get_by_index(uint32_t index);
uint32_t chardev_count(void);

int chardev_open(chardev_t *dev);
int chardev_close(chardev_t *dev);
int chardev_read(chardev_t *dev, void *buf, uint32_t count);
int chardev_write(chardev_t *dev, const void *buf, uint32_t count);

/* 注册内核内置字符设备，并创建 /dev 节点 */
void chardev_register_builtin_devices(void);

#endif /* CHARDEV_H */
