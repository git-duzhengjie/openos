#ifndef PFS_H
#define PFS_H

#include "vfs.h"

#define PFS_MAGIC 0x50465331u

int pfs_format(const char *dev_name);
fs_type_t *pfs_mount(const char *dev_name);
int pfs_bind_created_node(dentry_t *parent, dentry_t *child);
int pfs_remove_node(dentry_t *d);
int pfs_populate(dentry_t *root_dentry);

#endif /* PFS_H */
