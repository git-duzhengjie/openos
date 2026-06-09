#ifndef EXT4_H
#define EXT4_H

/*
 * openos - EXT4 filesystem driver
 *
 * 当前实现提供只读挂载、目录扫描和常规文件读取。
 * 写入/格式化需要 journal、位图和 inode/block 分配器支持，暂不启用，避免破坏磁盘。
 */
int ext4_mount(const char *dev_name, const char *mount_path);
int ext4_format_test_volume(const char *dev_name);

#endif /* EXT4_H */
