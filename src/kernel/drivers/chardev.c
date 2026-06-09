/* ============================================================
 * openos - 字符设备框架与内置字符设备 (Phase 3)
 * ============================================================ */

#include "../include/chardev.h"
#include "../include/string.h"
#include "../include/input_buffer.h"
#include "../include/vga.h"
#include "../include/serial.h"
#include "../fs/vfs.h"

static chardev_t chardev_table[CHARDEV_MAX];
static uint32_t chardev_table_count = 0;

static void chardev_copy_name(char *dst, const char *src) {
    uint32_t i = 0;
    if (!dst) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (; i < CHARDEV_NAME_MAX - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

void chardev_init(void) {
    memset(chardev_table, 0, sizeof(chardev_table));
    chardev_table_count = 0;
}

int chardev_register(const char *name, uint32_t major, uint32_t minor,
                     chardev_ops_t *ops, void *private_data) {
    chardev_t *dev;

    if (!name || !ops) return -1;
    if (chardev_table_count >= CHARDEV_MAX) return -1;
    if (chardev_find(name)) return -1;
    if (chardev_find_by_devno(major, minor)) return -1;

    dev = &chardev_table[chardev_table_count++];
    memset(dev, 0, sizeof(chardev_t));
    chardev_copy_name(dev->name, name);
    dev->major = major;
    dev->minor = minor;
    dev->ops = ops;
    dev->private_data = private_data;
    dev->ref_count = 0;
    return 0;
}

int chardev_unregister(const char *name) {
    uint32_t i;

    if (!name) return -1;
    for (i = 0; i < chardev_table_count; i++) {
        if (strcmp(chardev_table[i].name, name) == 0) {
            if (chardev_table[i].ref_count != 0) return -1;
            if (i + 1 < chardev_table_count) {
                memcpy(&chardev_table[i], &chardev_table[chardev_table_count - 1], sizeof(chardev_t));
            }
            memset(&chardev_table[chardev_table_count - 1], 0, sizeof(chardev_t));
            chardev_table_count--;
            return 0;
        }
    }
    return -1;
}

chardev_t *chardev_find(const char *name) {
    uint32_t i;

    if (!name) return 0;
    for (i = 0; i < chardev_table_count; i++) {
        if (strcmp(chardev_table[i].name, name) == 0) {
            return &chardev_table[i];
        }
    }
    return 0;
}

chardev_t *chardev_find_by_devno(uint32_t major, uint32_t minor) {
    uint32_t i;

    for (i = 0; i < chardev_table_count; i++) {
        if (chardev_table[i].major == major && chardev_table[i].minor == minor) {
            return &chardev_table[i];
        }
    }
    return 0;
}

chardev_t *chardev_get_by_index(uint32_t index) {
    if (index >= chardev_table_count) return 0;
    return &chardev_table[index];
}

uint32_t chardev_count(void) {
    return chardev_table_count;
}

int chardev_open(chardev_t *dev) {
    int ret;

    if (!dev) return -1;
    if (dev->ops && dev->ops->open) {
        ret = dev->ops->open(dev);
        if (ret < 0) return ret;
    }
    dev->ref_count++;
    return 0;
}

int chardev_close(chardev_t *dev) {
    int ret = 0;

    if (!dev) return -1;
    if (dev->ops && dev->ops->close) {
        ret = dev->ops->close(dev);
    }
    if (dev->ref_count > 0) dev->ref_count--;
    return ret;
}

int chardev_read(chardev_t *dev, void *buf, uint32_t count) {
    if (!dev || !buf) return -1;
    if (!dev->ops || !dev->ops->read) return -1;
    return dev->ops->read(dev, buf, count);
}

int chardev_write(chardev_t *dev, const void *buf, uint32_t count) {
    if (!dev || !buf) return -1;
    if (!dev->ops || !dev->ops->write) return -1;
    return dev->ops->write(dev, buf, count);
}

/* ---------------- 内置设备：console ---------------- */
static int console_read(chardev_t *dev, void *buf, uint32_t count) {
    char *out = (char *)buf;
    uint32_t n = 0;
    (void)dev;

    while (n < count && input_has_data()) {
        out[n++] = input_getc();
    }
    return (int)n;
}

static int console_write(chardev_t *dev, const void *buf, uint32_t count) {
    const char *in = (const char *)buf;
    uint32_t i;
    (void)dev;

    for (i = 0; i < count; i++) {
        vga_putc(in[i]);
        if (in[i] == '\n') {
            serial_putc('\r');
        }
        serial_putc(in[i]);
    }
    return (int)count;
}

static chardev_ops_t console_ops = {
    0,
    0,
    console_read,
    console_write
};

/* ---------------- 内置设备：keyboard ---------------- */
static int keyboard_read(chardev_t *dev, void *buf, uint32_t count) {
    char *out = (char *)buf;
    uint32_t n = 0;
    (void)dev;

    while (n < count && input_has_data()) {
        out[n++] = input_getc();
    }
    return (int)n;
}

static chardev_ops_t keyboard_ops = {
    0,
    0,
    keyboard_read,
    0
};

/* ---------------- 内置设备：null ---------------- */
static int null_read(chardev_t *dev, void *buf, uint32_t count) {
    (void)dev;
    (void)buf;
    (void)count;
    return 0;
}

static int null_write(chardev_t *dev, const void *buf, uint32_t count) {
    (void)dev;
    (void)buf;
    return (int)count;
}

static chardev_ops_t null_ops = {
    0,
    0,
    null_read,
    null_write
};

/* ---------------- 内置设备：zero ---------------- */
static int zero_read(chardev_t *dev, void *buf, uint32_t count) {
    (void)dev;
    memset(buf, 0, count);
    return (int)count;
}

static chardev_ops_t zero_ops = {
    0,
    0,
    zero_read,
    null_write
};

/* ---------------- 内置设备：random ---------------- */
static uint32_t random_state = 0x12345678;

static uint32_t random_next(void) {
    random_state = random_state * 1103515245u + 12345u;
    return random_state;
}

static int random_read(chardev_t *dev, void *buf, uint32_t count) {
    uint8_t *out = (uint8_t *)buf;
    uint32_t i;
    uint32_t value = 0;
    (void)dev;

    for (i = 0; i < count; i++) {
        if ((i & 3) == 0) value = random_next();
        out[i] = (uint8_t)(value >> ((i & 3) * 8));
    }
    return (int)count;
}

static chardev_ops_t random_ops = {
    0,
    0,
    random_read,
    0
};

void chardev_register_builtin_devices(void) {
    vfs_mkdir("/dev", 0755);

    chardev_register("console", 5, 1, &console_ops, 0);
    chardev_register("keyboard", 13, 0, &keyboard_ops, 0);
    chardev_register("null", 1, 3, &null_ops, 0);
    chardev_register("zero", 1, 5, &zero_ops, 0);
    chardev_register("random", 1, 8, &random_ops, 0);

    vfs_mknod("/dev/console", FS_DEVICE | 0666, "console");
    vfs_mknod("/dev/keyboard", FS_DEVICE | 0444, "keyboard");
    vfs_mknod("/dev/null", FS_DEVICE | 0666, "null");
    vfs_mknod("/dev/zero", FS_DEVICE | 0444, "zero");
    vfs_mknod("/dev/random", FS_DEVICE | 0444, "random");
}
