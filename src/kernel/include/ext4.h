#ifndef EXT4_H
#define EXT4_H

/*
 * openos - EXT4 filesystem driver
 *
 * 当前实现提供挂载、目录扫描、常规文件读取和已分配块内的写入/截断。
 * 当前不分配新 inode/block，也不启用 journal；超出已分配块的扩容会被拒绝。
 */
int ext4_mount(const char *dev_name, const char *mount_path);
int ext4_format_test_volume(const char *dev_name);

#endif /* EXT4_H */
