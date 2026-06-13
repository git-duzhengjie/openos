/* ============================================================
 * openos - ELF32 加载器实现 (Phase 3)
 *
 * 从 ramfs 加载简单 ELF 可执行文件
 * 假设：恒等映射地址空间，所有程序共享内核页目录
 * ============================================================ */

#include "elf_loader.h"
#include "vmm.h"
#include "pmm.h"
#include "serial.h"
#include "vfs.h"
#include "process.h"
#include <stddef.h>
#include <string.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

#define ELF_USER_MIN 0x00100000u
#define ELF_USER_MAX 0x10000000u
#define ELF_MAX_PHNUM 64u

/* ============================================================
 * 验证 ELF 头
 * ============================================================ */
int elf_validate_header(Elf32_Ehdr *ehdr) {
    /* 检查魔数 */
    if (ehdr->e_ident[0] != 0x7F ||
        ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' ||
        ehdr->e_ident[3] != 'F') {
        serial_write("[ELF] Invalid magic\n");
        return 0;
    }

    /* 32位 */
    if (ehdr->e_ident[4] != 1) {  /* EI_CLASS = ELFCLASS32 */
        serial_write("[ELF] Not 32-bit\n");
        return 0;
    }

    /* 小端 */
    if (ehdr->e_ident[5] != 1) {  /* EI_DATA = ELFDATA2LSB */
        serial_write("[ELF] Not little-endian\n");
        return 0;
    }

    /* 可执行文件 */
    if (ehdr->e_type != ET_EXEC) {
        serial_write("[ELF] Not executable\n");
        return 0;
    }

    /* i386 */
    if (ehdr->e_machine != EM_386) {
        serial_write("[ELF] Not i386\n");
        return 0;
    }

    return 1;
}

/* ============================================================
 * 加载 ELF 文件
 * ============================================================ */
elf_load_result_t elf_load(int fd) {
    elf_load_result_t result = {0, 0, 0};

    /* 读取 ELF 头 */
    Elf32_Ehdr ehdr;
    int ret = vfs_read(fd, &ehdr, sizeof(ehdr));
    if (ret != sizeof(ehdr)) {
        serial_write("[ELF] Cannot read header\n");
        return result;
    }

    /* 验证 */
    if (!elf_validate_header(&ehdr)) {
        serial_write("[ELF] Header validation failed\n");
        return result;
    }

    serial_write("[ELF] Valid ELF, entry=0x");
    serial_write_hex(ehdr.e_entry);
    serial_write(" phnum=");
    serial_write_hex(ehdr.e_phnum);
    serial_write("\n");

    /* 读取程序头表 */
    if (ehdr.e_phnum == 0 || ehdr.e_phnum > ELF_MAX_PHNUM ||
        ehdr.e_phentsize != sizeof(Elf32_Phdr) ||
        ehdr.e_phoff == 0 ||
        ehdr.e_phoff > 0x7FFFFFFFu - (uint32_t)ehdr.e_phnum * sizeof(Elf32_Phdr)) {
        serial_write("[ELF] Invalid program headers\n");
        return result;
    }

    /* 分配内存存放程序头表 */
    Elf32_Phdr *phdrs = (Elf32_Phdr *)pmm_alloc_page();
    if (!phdrs) {
        serial_write("[ELF] Cannot allocate memory for phdrs\n");
        return result;
    }

    /* 定位到程序头表 */
    vfs_seek(fd, ehdr.e_phoff, SEEK_SET);
    ret = vfs_read(fd, phdrs, ehdr.e_phnum * sizeof(Elf32_Phdr));
    if ((uint32_t)ret != ehdr.e_phnum * sizeof(Elf32_Phdr)) {
        serial_write("[ELF] Cannot read program headers\n");
        pmm_free_page((void *)phdrs);
        return result;
    }

    /* 加载每个 PT_LOAD 段 */
    uint32_t max_addr = 0;
    int num_loaded = 0;
    int entry_in_load_segment = 0;

    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr *ph = &phdrs[i];

        if (ph->p_type != PT_LOAD) {
            continue;  /* 忽略非加载段 */
        }

        serial_write("[ELF] PT_LOAD segment: vaddr=0x");
        serial_write_hex(ph->p_vaddr);
        serial_write(" filesz=");
        serial_write_hex(ph->p_filesz);
        serial_write(" memsz=");
        serial_write_hex(ph->p_memsz);
        serial_write(" flags=");
        serial_write_hex(ph->p_flags);
        serial_write("\n");

        /* 检查段大小和地址范围 */
        if (ph->p_memsz < ph->p_filesz || ph->p_memsz == 0) {
            serial_write("[ELF] Invalid segment size\n");
            pmm_free_page((void *)phdrs);
            return result;
        }
        if (ph->p_vaddr < ELF_USER_MIN || ph->p_vaddr >= ELF_USER_MAX ||
            ph->p_memsz > ELF_USER_MAX - ph->p_vaddr) {
            serial_write("[ELF] Invalid vaddr range\n");
            pmm_free_page((void *)phdrs);
            return result;
        }

        /* 计算需要的页数 */
        uint32_t start = ph->p_vaddr & PAGE_MASK;
        uint32_t end = ph->p_vaddr + ph->p_memsz;
        uint32_t num_pages = (end - start + PAGE_SIZE - 1) / PAGE_SIZE;
        if (num_pages == 0 || num_pages > 4096) {
            serial_write("[ELF] Invalid page count\n");
            pmm_free_page((void *)phdrs);
            return result;
        }

        /* 分配并映射页面 */
        for (uint32_t page = 0; page < num_pages; page++) {
            uint32_t vaddr = start + page * PAGE_SIZE;

            /* 确保页对齐 */
            vaddr &= PAGE_MASK;

            /* 分配物理页 */
            uint32_t phys = (uint32_t)pmm_alloc_page();
            if (!phys) {
                serial_write("[ELF] Cannot allocate page\n");
                pmm_free_page((void *)phdrs);
                return result;
            }

            /* 映射页面 */
            uint32_t flags = VMM_USER;
            if (ph->p_flags & PF_W) {
                flags = VMM_USER;  /* 可写 */
            } else {
                flags = VMM_RO | PTE_USER;  /* 只读 */
            }

            vmm_map_page(vaddr, phys, flags);

            /* 清零页面 */
            memset((void *)vaddr, 0, PAGE_SIZE);
        }

        /* 定位到段数据并读取 */
        vfs_seek(fd, ph->p_offset, SEEK_SET);

        if (ph->p_filesz > 0) {
            ret = vfs_read(fd, (void *)ph->p_vaddr, ph->p_filesz);
            if ((uint32_t)ret != ph->p_filesz) {
                serial_write("[ELF] Segment read incomplete: got ");
                serial_write_hex(ret);
                serial_write(" expected ");
                serial_write_hex(ph->p_filesz);
                serial_write("\n");
                pmm_free_page((void *)phdrs);
                return result;
            }
        }

        if (ehdr.e_entry >= ph->p_vaddr && ehdr.e_entry < end)
            entry_in_load_segment = 1;

        /* 更新最大地址 (用于 brk) */
        if (end > max_addr) {
            max_addr = end;
        }

        num_loaded++;
    }

    /* 释放程序头表内存 */
    pmm_free_page((void *)phdrs);

    if (num_loaded == 0 || !entry_in_load_segment) {
        serial_write("[ELF] No loadable segment or invalid entry\n");
        return result;
    }

    /* 设置结果 */
    result.entry = ehdr.e_entry;
    result.brk_start = (max_addr + PAGE_SIZE - 1) & PAGE_MASK;  /* 页对齐 */
    result.num_segments = num_loaded;

    serial_write("[ELF] Loaded ");
    serial_write_hex(num_loaded);
    serial_write(" segments, brk_start=0x");
    serial_write_hex(result.brk_start);
    serial_write("\n");

    return result;
}