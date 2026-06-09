/* ============================================================
 * openos - exFAT 文件系统驱动 (Phase 3)
 * ============================================================ */

#ifndef EXFAT_H
#define EXFAT_H

#include "types.h"

int exfat_mount(const char *dev_name, const char *mount_path);
int exfat_format_test_volume(const char *dev_name);

#endif /* EXFAT_H */
