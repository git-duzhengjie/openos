#ifndef KERNEL_FS_FAT32_H
#define KERNEL_FS_FAT32_H

#include <stdint.h>

#define FAT32_MAGIC 0xFA732032u

int fat32_mount(const char *path, const char *dev_name);
int fat32_format_demo(const char *dev_name);

#endif /* KERNEL_FS_FAT32_H */
