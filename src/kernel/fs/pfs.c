/* ============================================================
 * openos - PFS: minimal persistent block-device filesystem
 * ============================================================ */

#include "pfs.h"
#include "blockdev.h"
#include "string.h"
#include "process.h"

#define PFS_MAX_MOUNTS       2
#define PFS_MAX_NODES        64
#define PFS_MAX_DATA_BLOCKS  448
#define PFS_BLOCK_SIZE       512
#define PFS_ROOT_INO         1
#define PFS_NAME_LEN         MAX_NAME
#define PFS_META_LBA         1
#define PFS_META_BLOCKS      15
#define PFS_DATA_START_LBA   16
#define PFS_VERSION          1

#define PFS_NODE_FILE        1
#define PFS_NODE_DIR         2

typedef struct pfs_disk_header {
    uint32_t magic;
    uint32_t version;
    uint32_t node_count;
    uint32_t data_block_count;
    uint32_t root_ino;
    uint32_t checksum;
} pfs_disk_header_t;

typedef struct pfs_disk_node {
    uint32_t used;
    uint32_t ino;
    uint32_t parent_ino;
    uint32_t type;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t first_block;
    uint32_t block_count;
    char name[PFS_NAME_LEN];
} pfs_disk_node_t;

typedef struct pfs_node {
    pfs_disk_node_t disk;
} pfs_node_t;

typedef struct pfs_mount_data {
    int used;
    blockdev_t *dev;
    fs_type_t fs;
    pfs_node_t nodes[PFS_MAX_NODES];
    uint8_t data_bitmap[PFS_MAX_DATA_BLOCKS];
} pfs_mount_data_t;

static pfs_mount_data_t pfs_mounts[PFS_MAX_MOUNTS];

static int pfs_read(file_t *f, void *buf, uint32_t count);
static int pfs_write(file_t *f, const void *buf, uint32_t count);
static int pfs_seek(file_t *f, int offset, int whence);
static int pfs_truncate(inode_t *inode, uint32_t size);
static int pfs_chmod(inode_t *inode, uint32_t mode);
static int pfs_chown(inode_t *inode, uint32_t uid, uint32_t gid);

static file_ops_t pfs_file_ops = {
    .read = pfs_read,
    .write = pfs_write,
    .seek = pfs_seek,
    .truncate = pfs_truncate,
};

static file_ops_t pfs_dir_ops = {0};

static inode_ops_t pfs_inode_ops = {
    .chmod = pfs_chmod,
    .chown = pfs_chown,
};

static uint32_t pfs_checksum(uint8_t *buf, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum = (sum << 5) - sum + buf[i];
    return sum;
}

static pfs_mount_data_t *pfs_mount_from_node(pfs_node_t *node) {
    if (!node) return NULL;
    for (int i = 0; i < PFS_MAX_MOUNTS; i++) {
        if (!pfs_mounts[i].used) continue;
        if (node >= &pfs_mounts[i].nodes[0] && node < &pfs_mounts[i].nodes[PFS_MAX_NODES])
            return &pfs_mounts[i];
    }
    return NULL;
}

static pfs_node_t *pfs_node_from_inode(inode_t *inode) {
    if (!inode || inode->fs_type != PFS_MAGIC || !inode->fs_data) return NULL;
    return (pfs_node_t *)inode->fs_data;
}

static pfs_mount_data_t *pfs_mount_from_inode(inode_t *inode) {
    return pfs_mount_from_node(pfs_node_from_inode(inode));
}

static pfs_node_t *pfs_find_ino(pfs_mount_data_t *mnt, uint32_t ino) {
    if (!mnt) return NULL;
    for (int i = 0; i < PFS_MAX_NODES; i++) {
        if (mnt->nodes[i].disk.used && mnt->nodes[i].disk.ino == ino) return &mnt->nodes[i];
    }
    return NULL;
}

static pfs_node_t *pfs_find_child(pfs_mount_data_t *mnt, uint32_t parent_ino, const char *name) {
    if (!mnt || !name) return NULL;
    for (int i = 0; i < PFS_MAX_NODES; i++) {
        pfs_disk_node_t *dn = &mnt->nodes[i].disk;
        if (dn->used && dn->parent_ino == parent_ino && strcmp(dn->name, name) == 0)
            return &mnt->nodes[i];
    }
    return NULL;
}

static uint32_t pfs_next_ino(pfs_mount_data_t *mnt) {
    uint32_t max_ino = PFS_ROOT_INO;
    for (int i = 0; i < PFS_MAX_NODES; i++) {
        if (mnt->nodes[i].disk.used && mnt->nodes[i].disk.ino > max_ino)
            max_ino = mnt->nodes[i].disk.ino;
    }
    return max_ino + 1;
}

static void pfs_bind_inode(pfs_node_t *node, inode_t *inode) {
    inode->ino = node->disk.ino;
    inode->mode = node->disk.mode;
    inode->uid = node->disk.uid;
    inode->gid = node->disk.gid;
    inode->size = node->disk.size;
    inode->fs_type = PFS_MAGIC;
    inode->fs_data = node;
    inode->ops = (node->disk.type == PFS_NODE_DIR) ? &pfs_dir_ops : &pfs_file_ops;
    inode->iops = &pfs_inode_ops;
}

static int pfs_sync_meta(pfs_mount_data_t *mnt) {
    uint8_t buf[PFS_META_BLOCKS * PFS_BLOCK_SIZE];
    pfs_disk_header_t *hdr = (pfs_disk_header_t *)buf;
    pfs_disk_node_t *nodes = (pfs_disk_node_t *)(buf + sizeof(pfs_disk_header_t));
    if (!mnt || !mnt->dev) return -1;
    memset(buf, 0, sizeof(buf));
    hdr->magic = PFS_MAGIC;
    hdr->version = PFS_VERSION;
    hdr->node_count = PFS_MAX_NODES;
    hdr->data_block_count = PFS_MAX_DATA_BLOCKS;
    hdr->root_ino = PFS_ROOT_INO;
    for (int i = 0; i < PFS_MAX_NODES; i++) nodes[i] = mnt->nodes[i].disk;
    hdr->checksum = 0;
    hdr->checksum = pfs_checksum(buf, sizeof(buf));
    return blockdev_write_blocks(mnt->dev, PFS_META_LBA, PFS_META_BLOCKS, buf);
}

static int pfs_zero_blocks(pfs_mount_data_t *mnt, uint32_t first, uint32_t count) {
    uint8_t zero[PFS_BLOCK_SIZE];
    memset(zero, 0, sizeof(zero));
    for (uint32_t i = 0; i < count; i++) {
        if (blockdev_write_blocks(mnt->dev, PFS_DATA_START_LBA + first + i, 1, zero) < 0)
            return -1;
    }
    return 0;
}

static int pfs_alloc_extent(pfs_mount_data_t *mnt, uint32_t blocks, uint32_t *first_out) {
    if (!mnt || !first_out || blocks == 0 || blocks > PFS_MAX_DATA_BLOCKS) return -1;
    for (uint32_t start = 0; start + blocks <= PFS_MAX_DATA_BLOCKS; start++) {
        int ok = 1;
        for (uint32_t i = 0; i < blocks; i++) {
            if (mnt->data_bitmap[start + i]) { ok = 0; break; }
        }
        if (!ok) continue;
        for (uint32_t i = 0; i < blocks; i++) mnt->data_bitmap[start + i] = 1;
        *first_out = start;
        if (pfs_zero_blocks(mnt, start, blocks) < 0) return -1;
        return 0;
    }
    return -1;
}

static void pfs_free_extent(pfs_mount_data_t *mnt, uint32_t first, uint32_t blocks) {
    if (!mnt) return;
    for (uint32_t i = 0; i < blocks && first + i < PFS_MAX_DATA_BLOCKS; i++)
        mnt->data_bitmap[first + i] = 0;
}

static int pfs_load_meta(pfs_mount_data_t *mnt) {
    uint8_t buf[PFS_META_BLOCKS * PFS_BLOCK_SIZE];
    pfs_disk_header_t *hdr = (pfs_disk_header_t *)buf;
    pfs_disk_node_t *nodes = (pfs_disk_node_t *)(buf + sizeof(pfs_disk_header_t));
    uint32_t saved;
    if (!mnt || !mnt->dev) return -1;
    if (blockdev_read_blocks(mnt->dev, PFS_META_LBA, PFS_META_BLOCKS, buf) < 0) return -1;
    if (hdr->magic != PFS_MAGIC || hdr->version != PFS_VERSION) return -1;
    saved = hdr->checksum;
    hdr->checksum = 0;
    if (saved != pfs_checksum(buf, sizeof(buf))) return -1;

    memset(mnt->nodes, 0, sizeof(mnt->nodes));
    memset(mnt->data_bitmap, 0, sizeof(mnt->data_bitmap));
    for (int i = 0; i < PFS_MAX_NODES; i++) {
        mnt->nodes[i].disk = nodes[i];
        if (mnt->nodes[i].disk.used) {
            uint32_t first = mnt->nodes[i].disk.first_block;
            uint32_t blocks = mnt->nodes[i].disk.block_count;
            for (uint32_t b = 0; b < blocks && first + b < PFS_MAX_DATA_BLOCKS; b++)
                mnt->data_bitmap[first + b] = 1;
        }
    }
    return pfs_find_ino(mnt, PFS_ROOT_INO) ? 0 : -1;
}

static int pfs_resize(pfs_mount_data_t *mnt, pfs_node_t *node, uint32_t size) {
    uint32_t need = (size + PFS_BLOCK_SIZE - 1) / PFS_BLOCK_SIZE;
    uint32_t old_first = node->disk.first_block;
    uint32_t old_count = node->disk.block_count;
    uint32_t new_first = 0;
    uint8_t tmp[PFS_BLOCK_SIZE];
    if (need == old_count) {
        node->disk.size = size;
        return pfs_sync_meta(mnt);
    }
    if (need > 0 && pfs_alloc_extent(mnt, need, &new_first) < 0) return -1;
    for (uint32_t b = 0; b < old_count && b < need; b++) {
        if (blockdev_read_blocks(mnt->dev, PFS_DATA_START_LBA + old_first + b, 1, tmp) < 0) return -1;
        if (blockdev_write_blocks(mnt->dev, PFS_DATA_START_LBA + new_first + b, 1, tmp) < 0) return -1;
    }
    if (old_count > 0) pfs_free_extent(mnt, old_first, old_count);
    node->disk.first_block = new_first;
    node->disk.block_count = need;
    node->disk.size = size;
    return pfs_sync_meta(mnt);
}

static int pfs_read(file_t *f, void *buf, uint32_t count) {
    pfs_node_t *node = pfs_node_from_inode(f ? f->inode : NULL);
    pfs_mount_data_t *mnt = pfs_mount_from_inode(f ? f->inode : NULL);
    uint8_t block[PFS_BLOCK_SIZE];
    uint8_t *out = (uint8_t *)buf;
    uint32_t done = 0;
    if (!f || !buf || !node || !mnt || node->disk.type != PFS_NODE_FILE) return -1;
    if (f->offset >= node->disk.size) return 0;
    if (count > node->disk.size - f->offset) count = node->disk.size - f->offset;
    while (done < count) {
        uint32_t pos = f->offset + done;
        uint32_t bi = pos / PFS_BLOCK_SIZE;
        uint32_t bo = pos % PFS_BLOCK_SIZE;
        uint32_t chunk = PFS_BLOCK_SIZE - bo;
        if (chunk > count - done) chunk = count - done;
        if (blockdev_read_blocks(mnt->dev, PFS_DATA_START_LBA + node->disk.first_block + bi, 1, block) < 0) return -1;
        memcpy(out + done, block + bo, chunk);
        done += chunk;
    }
    f->offset += done;
    return (int)done;
}

static int pfs_write(file_t *f, const void *buf, uint32_t count) {
    pfs_node_t *node = pfs_node_from_inode(f ? f->inode : NULL);
    pfs_mount_data_t *mnt = pfs_mount_from_inode(f ? f->inode : NULL);
    uint8_t block[PFS_BLOCK_SIZE];
    const uint8_t *in = (const uint8_t *)buf;
    uint32_t done = 0;
    uint32_t end;
    if (!f || !buf || !node || !mnt || node->disk.type != PFS_NODE_FILE) return -1;
    end = f->offset + count;
    if (end < f->offset) return -1;
    if (end > node->disk.size && pfs_resize(mnt, node, end) < 0) return -1;
    while (done < count) {
        uint32_t pos = f->offset + done;
        uint32_t bi = pos / PFS_BLOCK_SIZE;
        uint32_t bo = pos % PFS_BLOCK_SIZE;
        uint32_t chunk = PFS_BLOCK_SIZE - bo;
        if (chunk > count - done) chunk = count - done;
        if (blockdev_read_blocks(mnt->dev, PFS_DATA_START_LBA + node->disk.first_block + bi, 1, block) < 0) return -1;
        memcpy(block + bo, in + done, chunk);
        if (blockdev_write_blocks(mnt->dev, PFS_DATA_START_LBA + node->disk.first_block + bi, 1, block) < 0) return -1;
        done += chunk;
    }
    f->offset += done;
    f->inode->size = node->disk.size;
    return (int)done;
}

static int pfs_seek(file_t *f, int offset, int whence) {
    uint32_t base;
    if (!f || !f->inode) return -1;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = f->offset;
    else if (whence == SEEK_END) base = f->inode->size;
    else return -1;
    if (offset < 0 && (uint32_t)(-offset) > base) return -1;
    f->offset = (offset < 0) ? base - (uint32_t)(-offset) : base + (uint32_t)offset;
    return (int)f->offset;
}

static int pfs_truncate(inode_t *inode, uint32_t size) {
    pfs_node_t *node = pfs_node_from_inode(inode);
    pfs_mount_data_t *mnt = pfs_mount_from_inode(inode);
    if (!node || !mnt || node->disk.type != PFS_NODE_FILE) return -1;
    if (pfs_resize(mnt, node, size) < 0) return -1;
    inode->size = node->disk.size;
    return 0;
}

static int pfs_chmod(inode_t *inode, uint32_t mode) {
    pfs_node_t *node = pfs_node_from_inode(inode);
    pfs_mount_data_t *mnt = pfs_mount_from_inode(inode);
    if (!node || !mnt) return -1;
    node->disk.mode = (node->disk.mode & ~S_IRWXUGO) | (mode & S_IRWXUGO);
    inode->mode = node->disk.mode;
    return pfs_sync_meta(mnt);
}

static int pfs_chown(inode_t *inode, uint32_t uid, uint32_t gid) {
    pfs_node_t *node = pfs_node_from_inode(inode);
    pfs_mount_data_t *mnt = pfs_mount_from_inode(inode);
    if (!node || !mnt) return -1;
    node->disk.uid = uid;
    node->disk.gid = gid;
    inode->uid = uid;
    inode->gid = gid;
    return pfs_sync_meta(mnt);
}

int pfs_bind_created_node(dentry_t *parent, dentry_t *child) {
    pfs_node_t *pnode;
    pfs_mount_data_t *mnt;
    pfs_node_t *slot = NULL;
    uint32_t type;
    if (!parent || !child || !parent->inode || !child->inode) return -1;
    pnode = pfs_node_from_inode(parent->inode);
    mnt = pfs_mount_from_node(pnode);
    if (!pnode || !mnt) return -1;
    if (pfs_find_child(mnt, pnode->disk.ino, child->name)) return -1;
    type = (child->inode->mode & FS_DIR) ? PFS_NODE_DIR : PFS_NODE_FILE;
    for (int i = 0; i < PFS_MAX_NODES; i++) {
        if (!mnt->nodes[i].disk.used) { slot = &mnt->nodes[i]; break; }
    }
    if (!slot) return -1;
    memset(slot, 0, sizeof(*slot));
    slot->disk.used = 1;
    slot->disk.ino = pfs_next_ino(mnt);
    slot->disk.parent_ino = pnode->disk.ino;
    slot->disk.type = type;
    slot->disk.mode = child->inode->mode;
    slot->disk.uid = child->inode->uid;
    slot->disk.gid = child->inode->gid;
    slot->disk.size = 0;
    strncpy(slot->disk.name, child->name, PFS_NAME_LEN - 1);
    pfs_bind_inode(slot, child->inode);
    return pfs_sync_meta(mnt);
}

int pfs_remove_node(dentry_t *d) {
    pfs_node_t *node = pfs_node_from_inode(d ? d->inode : NULL);
    pfs_mount_data_t *mnt = pfs_mount_from_node(node);
    if (!d || !node || !mnt) return -1;
    if (node->disk.type == PFS_NODE_DIR) {
        for (int i = 0; i < PFS_MAX_NODES; i++) {
            if (mnt->nodes[i].disk.used && mnt->nodes[i].disk.parent_ino == node->disk.ino)
                return -1;
        }
    }
    if (node->disk.block_count > 0) pfs_free_extent(mnt, node->disk.first_block, node->disk.block_count);
    memset(node, 0, sizeof(*node));
    return pfs_sync_meta(mnt);
}

static inode_t *pfs_lookup(fs_type_t *fs, const char *path) {
    (void)fs;
    (void)path;
    /* Let VFS allocate the mount root inode from its inode pool. pfs_populate()
     * then binds that inode to the persistent root node. Returning a pfs_node_t
     * cast as inode_t would make VFS free a non-pool object on umount.
     */
    return NULL;
}

int pfs_populate(dentry_t *root_dentry) {
    pfs_node_t *root_node;
    pfs_mount_data_t *mnt;
    if (!root_dentry || !root_dentry->inode) return -1;
    root_node = pfs_node_from_inode(root_dentry->inode);
    mnt = pfs_mount_from_node(root_node);
    if (!root_node) {
        mnt = (pfs_mount_data_t *)root_dentry->inode->fs_data;
        root_node = pfs_find_ino(mnt, PFS_ROOT_INO);
        if (root_node) pfs_bind_inode(root_node, root_dentry->inode);
    }
    if (!root_node || !mnt) return -1;
    for (int pass = 0; pass < PFS_MAX_NODES; pass++) {
        int made = 0;
        for (int i = 0; i < PFS_MAX_NODES; i++) {
            pfs_node_t *node = &mnt->nodes[i];
            if (!node->disk.used || node->disk.ino == PFS_ROOT_INO) continue;
            if (node->disk.parent_ino == PFS_ROOT_INO) {
                dentry_t *d = vfs_create_node_under(root_dentry, node->disk.name, node->disk.mode,
                                                    node->disk.type == PFS_NODE_DIR ? &pfs_dir_ops : &pfs_file_ops,
                                                    node, node->disk.size);
                if (d && d->inode) {
                    pfs_bind_inode(node, d->inode);
                    made++;
                }
            }
        }
        if (!made) break;
    }
    return 0;
}

int pfs_format(const char *dev_name) {
    blockdev_t *dev = blockdev_find(dev_name);
    pfs_mount_data_t tmp;
    if (!dev) return -1;
    memset(&tmp, 0, sizeof(tmp));
    tmp.dev = dev;
    tmp.nodes[0].disk.used = 1;
    tmp.nodes[0].disk.ino = PFS_ROOT_INO;
    tmp.nodes[0].disk.parent_ino = PFS_ROOT_INO;
    tmp.nodes[0].disk.type = PFS_NODE_DIR;
    tmp.nodes[0].disk.mode = FS_DIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    tmp.nodes[0].disk.uid = 0;
    tmp.nodes[0].disk.gid = 0;
    strcpy(tmp.nodes[0].disk.name, "/");
    if (pfs_zero_blocks(&tmp, 0, PFS_MAX_DATA_BLOCKS) < 0) return -1;
    return pfs_sync_meta(&tmp);
}

fs_type_t *pfs_mount(const char *dev_name) {
    blockdev_t *dev = blockdev_find(dev_name);
    pfs_mount_data_t *mnt = NULL;
    if (!dev) return NULL;
    for (int i = 0; i < PFS_MAX_MOUNTS; i++) {
        if (!pfs_mounts[i].used) { mnt = &pfs_mounts[i]; break; }
    }
    if (!mnt) return NULL;
    memset(mnt, 0, sizeof(*mnt));
    mnt->used = 1;
    mnt->dev = dev;
    strcpy(mnt->fs.name, "pfs");
    mnt->fs.magic = PFS_MAGIC;
    mnt->fs.lookup = pfs_lookup;
    mnt->fs.data = mnt;
    if (pfs_load_meta(mnt) < 0) {
        memset(mnt, 0, sizeof(*mnt));
        return NULL;
    }
    return &mnt->fs;
}
