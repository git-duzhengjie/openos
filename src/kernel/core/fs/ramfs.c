/* ============================================================
 * openos - RAM 文件系统实现 (ramfs) - Phase 3
 * ============================================================ */

#include "ramfs.h"
#include "serial.h"
#include "pmm.h"
#include "string.h"
#include "vmm.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

/* ---- ramfs file ops ---- */
static int ramfs_open(file_t *f);
static int ramfs_close(file_t *f);
static int ramfs_read(file_t *f, void *buf, uint32_t count);
static int ramfs_write(file_t *f, const void *buf, uint32_t count);
static int ramfs_seek(file_t *f, int offset, int whence);
static int ramfs_truncate(inode_t *inode, uint32_t size);

file_ops_t ramfs_file_ops;

static file_ops_t ramfs_dir_ops;
static fs_type_t ramfs_fs_type;

void ramfs_refresh_ops(void) {
    ramfs_fs_type.name[0] = 'r';
    ramfs_fs_type.name[1] = 'a';
    ramfs_fs_type.name[2] = 'm';
    ramfs_fs_type.name[3] = 'f';
    ramfs_fs_type.name[4] = 's';
    ramfs_fs_type.name[5] = '\0';
    ramfs_fs_type.magic = RAMFS_MAGIC;
    ramfs_fs_type.lookup = NULL;
    ramfs_fs_type.create = NULL;
    ramfs_fs_type.mkdir = NULL;
    ramfs_fs_type.unlink = NULL;
    ramfs_fs_type.data = NULL;

    ramfs_file_ops.open = ramfs_open;
    ramfs_file_ops.close = ramfs_close;
    ramfs_file_ops.read = ramfs_read;
    ramfs_file_ops.write = ramfs_write;
    ramfs_file_ops.seek = ramfs_seek;
    ramfs_file_ops.truncate = ramfs_truncate;
    ramfs_file_ops.readdir = NULL;

    ramfs_dir_ops.open = ramfs_open;
    ramfs_dir_ops.close = ramfs_close;
    ramfs_dir_ops.read = NULL;
    ramfs_dir_ops.write = NULL;
    ramfs_dir_ops.seek = NULL;
    ramfs_dir_ops.truncate = NULL;
    ramfs_dir_ops.readdir = NULL;
}

/* ---- ramfs file ops 实现 ---- */
static int ramfs_open(file_t *f) {
    (void)f;
    return 0;
}

static int ramfs_close(file_t *f) {
    (void)f;
    return 0;
}

static int ramfs_read(file_t *f, void *buf, uint32_t count) {
    if (!f || !f->inode || !buf) return -1;
    
    ramfs_file_t *rf = (ramfs_file_t *)f->inode->fs_data;
    if (!rf) return -1;
    
    uint32_t off = f->offset;
    if (off >= rf->size) return 0;
    
    /* 限制读取范围 */
    if (off + count > rf->size)
        count = rf->size - off;
    
    uint32_t read_bytes = 0;
    while (read_bytes < count) {
        uint32_t block_idx = (off + read_bytes) / RAMFS_BLOCK_SIZE;
        uint32_t block_off = (off + read_bytes) % RAMFS_BLOCK_SIZE;
        uint32_t chunk = RAMFS_BLOCK_SIZE - block_off;
        if (chunk > count - read_bytes) chunk = count - read_bytes;
        
        if (block_idx >= rf->nblocks || !rf->blocks[block_idx]) {
            /* 空洞，填零 */
            uint8_t *dst = (uint8_t *)buf + read_bytes;
            for (uint32_t i = 0; i < chunk; i++) dst[i] = 0;
        } else {
            uint8_t *src = (uint8_t *)(rf->blocks[block_idx]) + block_off;
            uint8_t *dst = (uint8_t *)buf + read_bytes;
            for (uint32_t i = 0; i < chunk; i++) dst[i] = src[i];
        }
        
        read_bytes += chunk;
    }
    
    f->offset = off + read_bytes;
    return (int)read_bytes;
}

int ramfs_write_fallback(file_t *f, const void *buf, uint32_t count) {
    return ramfs_write(f, buf, count);
}

static int ramfs_write(file_t *f, const void *buf, uint32_t count) {
    if (!f || !f->inode || !buf) return -1;
    
    ramfs_file_t *rf = (ramfs_file_t *)f->inode->fs_data;
    if (!rf) {
        /* 分配 ramfs_file 数据 */
        rf = (ramfs_file_t *)pmm_alloc_page();
        if (!rf) return -1;
        for (int i = 0; i < (int)sizeof(ramfs_file_t); i++) ((char *)rf)[i] = 0;
        f->inode->fs_data = rf;
    }
    
    uint32_t off = f->offset;
    if (count > 0xFFFFFFFFu - off) {
        serial_write("[RAMFS-W] overflow off/count\n");
        return -1;
    }
    uint32_t end = off + count;
    
    /* 按需分配数据块 */
    while (rf->nblocks * RAMFS_BLOCK_SIZE < end) {
        if (rf->nblocks >= RAMFS_MAX_BLOCKS) {
            serial_write("[RAMFS-W] max blocks reached\n");
            return -1;
        }
        uint32_t blk = (uint32_t)pmm_alloc_page();
        if (!blk) {
            serial_write("[RAMFS-W] alloc block failed\n");
            return -1;
        }
        /* 清零新块 */
        uint8_t *p = (uint8_t *)blk;
        for (int i = 0; i < RAMFS_BLOCK_SIZE; i++) p[i] = 0;
        rf->blocks[rf->nblocks++] = blk;
    }
    
    /* 写入数据 */
    uint32_t written = 0;
    while (written < count) {
        uint32_t block_idx = (off + written) / RAMFS_BLOCK_SIZE;
        uint32_t block_off = (off + written) % RAMFS_BLOCK_SIZE;
        uint32_t chunk = RAMFS_BLOCK_SIZE - block_off;
        if (chunk > count - written) chunk = count - written;
        
        if (block_idx >= rf->nblocks || !rf->blocks[block_idx]) {
            serial_write("[RAMFS-W] missing block\n");
            return -1;
        }
        
        uint8_t *dst = (uint8_t *)(rf->blocks[block_idx]) + block_off;
        const uint8_t *src = (const uint8_t *)buf + written;
        for (uint32_t i = 0; i < chunk; i++) dst[i] = src[i];
        
        written += chunk;
    }
    
    if (end > rf->size) rf->size = end;
    f->inode->size = rf->size;
    f->offset = off + written;
    
    return (int)written;
}

static int ramfs_seek(file_t *f, int offset, int whence) {
    if (!f) return -1;
    uint32_t new_off;
    uint32_t size = f->inode ? f->inode->size : 0;
    switch (whence) {
    case SEEK_SET:
        if (offset < 0) return -1;
        new_off = (uint32_t)offset;
        break;
    case SEEK_CUR:
        if (offset < 0 && (uint32_t)(-offset) > f->offset) return -1;
        new_off = f->offset + offset;
        break;
    case SEEK_END:
        if (offset < 0 && (uint32_t)(-offset) > size) return -1;
        new_off = size + offset;
        break;
    default: return -1;
    }
    f->offset = new_off;
    return (int)new_off;
}

static int ramfs_truncate(inode_t *inode, uint32_t size) {
    if (!inode) return -1;

    ramfs_file_t *rf = (ramfs_file_t *)inode->fs_data;
    if (!rf) {
        rf = (ramfs_file_t *)pmm_alloc_page();
        if (!rf) return -1;
        for (int i = 0; i < (int)sizeof(ramfs_file_t); i++) ((char *)rf)[i] = 0;
        inode->fs_data = rf;
    }

    uint32_t needed_blocks = 0;
    if (size > 0) {
        needed_blocks = (size + RAMFS_BLOCK_SIZE - 1) / RAMFS_BLOCK_SIZE;
        if (needed_blocks > RAMFS_MAX_BLOCKS) return -1;
    }

    for (uint32_t i = needed_blocks; i < rf->nblocks; i++) {
        if (rf->blocks[i]) {
            pmm_free_page((void *)rf->blocks[i]);
            rf->blocks[i] = 0;
        }
    }

    if (needed_blocks < rf->nblocks) {
        rf->nblocks = needed_blocks;
    }

    if (size < rf->size && needed_blocks > 0 && (size % RAMFS_BLOCK_SIZE) != 0) {
        uint32_t last = needed_blocks - 1;
        uint32_t off = size % RAMFS_BLOCK_SIZE;
        if (rf->blocks[last]) {
            uint8_t *p = (uint8_t *)rf->blocks[last];
            for (uint32_t i = off; i < RAMFS_BLOCK_SIZE; i++) p[i] = 0;
        }
    }

    rf->size = size;
    inode->size = size;
    return 0;
}

/* ---- ramfs fs_type ---- */
fs_type_t *ramfs_get_fs_type(void) {
    ramfs_refresh_ops();
    return &ramfs_fs_type;
}

/* ---- ramfs 初始化 ---- */
void ramfs_init(void) {
    ramfs_refresh_ops();
    /* VFS 已经创建了根目录，设置根 inode 的 ops */
    /* 需要在 VFS init 之后调用 */
    
    /* 为根目录分配 ramfs 数据 */
    ramfs_file_t *root_data = (ramfs_file_t *)pmm_alloc_page();
    if (root_data) {
        for (int i = 0; i < (int)sizeof(ramfs_file_t); i++) ((char *)root_data)[i] = 0;
    }
    
    /* ramfs 创建的文件自动使用 ramfs_file_ops */
    /* 通过 vfs_open 创建的新文件会从父目录继承 ops */
    
    serial_write("[OK] ramfs initialized\n");
}

/* ---- 为新文件/目录设置 ramfs ops ---- */
/* 在 vfs_open 创建新文件时，如果父目录是 ramfs，应设置 ops */
/* 这个函数由 VFS 通过 inode->fs_type 调用 */
void ramfs_setup_inode(inode_t *ip, uint32_t mode) {
    if (!ip) return;
    ramfs_refresh_ops();
    ip->fs_type = RAMFS_MAGIC;
    if ((mode & 0xF000) == FS_DIR) {
        ip->ops = &ramfs_dir_ops;
    } else {
        ip->ops = &ramfs_file_ops;
    }

    if (!ip->fs_data) {
        ramfs_file_t *rf = (ramfs_file_t *)pmm_alloc_page();
        if (rf) {
            for (int i = 0; i < (int)sizeof(ramfs_file_t); i++) ((char *)rf)[i] = 0;
            rf->size = ip->size;
            ip->fs_data = rf;
        }
    }
}
