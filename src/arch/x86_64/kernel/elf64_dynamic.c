/*
 * elf64_dynamic.c - M5.1a 动态段解析实现
 *
 * 从内存中的 ELF 镜像解析 PT_INTERP / PT_DYNAMIC，抽取 .dynamic 表的关键项，
 * 填充 openos_elf64_dyninfo_t。本阶段只解析、不重定位。
 */

#include "../include/elf64_dynamic.h"

/*
 * 仅需两个串口打印符号，为避免拖入 early_console64.h 的 arch64_types 依赖
 * （便于宿主机单元测试直接编译本文件），这里采用前向声明。
 */
void early_console64_write(const char *text);
void early_console64_write_hex64(uint64_t value);

/* 简单内存清零 */
static void dyn_memzero(void *p, uint64_t n) {
    uint8_t *b = (uint8_t *)p;
    for (uint64_t i = 0; i < n; i++) b[i] = 0;
}

/* 校验 ELF magic + 64 位 + x86_64 */
static int dyn_check_ehdr(const openos_elf64_ehdr_t *e) {
    if (e->e_ident[0] != 0x7f || e->e_ident[1] != 'E' ||
        e->e_ident[2] != 'L'  || e->e_ident[3] != 'F') {
        return -1;              /* bad magic */
    }
    if (e->e_ident[4] != 2) return -2;   /* ELFCLASS64 */
    if (e->e_ident[5] != 1) return -3;   /* ELFDATA2LSB */
    if (e->e_machine != 62)  return -4;  /* EM_X86_64 */
    if (e->e_phentsize != sizeof(openos_elf64_phdr_t)) return -5;
    return 0;
}

int openos_elf64_parse_dynamic(const void *image, uint64_t image_size,
                               uint64_t load_bias,
                               openos_elf64_dyninfo_t *out) {
    if (!image || !out) return -1;
    if (image_size < sizeof(openos_elf64_ehdr_t)) return -2;

    dyn_memzero(out, sizeof(*out));

    const uint8_t *base = (const uint8_t *)image;
    const openos_elf64_ehdr_t *eh = (const openos_elf64_ehdr_t *)base;

    int chk = dyn_check_ehdr(eh);
    if (chk != 0) return -10 + chk;

    out->load_bias = load_bias;
    out->is_pie    = (eh->e_type == OPENOS_ET_DYN) ? 1 : 0;
    out->entry     = eh->e_entry + load_bias;

    /* 遍历 program header，定位 PT_DYNAMIC / PT_INTERP */
    if (eh->e_phoff == 0 || eh->e_phnum == 0) return -3;
    uint64_t ph_end = eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize;
    if (ph_end > image_size) return -4;

    const openos_elf64_phdr_t *dyn_ph = 0;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const openos_elf64_phdr_t *ph =
            (const openos_elf64_phdr_t *)(base + eh->e_phoff +
                                          (uint64_t)i * eh->e_phentsize);
        if (ph->p_type == OPENOS_PT_INTERP) {
            /* interp 字符串在文件偏移 p_offset 处 */
            if (ph->p_offset < image_size) {
                out->interp = (const char *)(base + ph->p_offset);
            }
        } else if (ph->p_type == OPENOS_PT_DYNAMIC) {
            dyn_ph = ph;
        }
    }

    if (!dyn_ph) {
        /* 静态可执行文件：无 .dynamic，合法，直接返回成功 */
        return 0;
    }

    /*
     * .dynamic 的运行时地址：优先用 p_vaddr + load_bias（段已映射）。
     * 表项数量由 p_memsz / sizeof(dyn) 推算，遇 DT_NULL 提前结束。
     */
    const openos_elf64_dyn_t *dyn =
        (const openos_elf64_dyn_t *)(dyn_ph->p_vaddr + load_bias);
    uint64_t max_ent = dyn_ph->p_memsz / sizeof(openos_elf64_dyn_t);

    out->dyn = dyn;

    for (uint64_t i = 0; i < max_ent; i++) {
        int64_t  tag = dyn[i].d_tag;
        uint64_t val = dyn[i].d_val;
        if (tag == OPENOS_DT_NULL) { out->dyn_count = i + 1; break; }
        out->dyn_count = i + 1;

        switch (tag) {
        case OPENOS_DT_STRTAB:
            out->strtab = (const char *)(val + load_bias); break;
        case OPENOS_DT_STRSZ:
            out->strsz = val; break;
        case OPENOS_DT_SYMTAB:
            out->symtab = (const openos_elf64_sym_t *)(val + load_bias); break;
        case OPENOS_DT_SYMENT:
            out->syment = val; break;
        case OPENOS_DT_RELA:
            out->rela = (const openos_elf64_rela_t *)(val + load_bias); break;
        case OPENOS_DT_RELASZ:
            out->rela_sz = val; break;
        case OPENOS_DT_RELAENT:
            out->rela_ent = val; break;
        case OPENOS_DT_JMPREL:
            out->jmprel = (const openos_elf64_rela_t *)(val + load_bias); break;
        case OPENOS_DT_PLTRELSZ:
            out->pltrel_sz = val; break;
        case OPENOS_DT_PLTREL:
            out->pltrel_type = (int64_t)val; break;
        case OPENOS_DT_PLTGOT:
            out->pltgot = (uint64_t *)(val + load_bias); break;
        case OPENOS_DT_INIT:
            out->init = val + load_bias; break;
        case OPENOS_DT_FINI:
            out->fini = val + load_bias; break;
        case OPENOS_DT_INIT_ARRAY:
            out->init_array = val + load_bias; break;
        case OPENOS_DT_INIT_ARRAYSZ:
            out->init_arraysz = val; break;
        case OPENOS_DT_NEEDED:
            if (out->needed_count < 16) {
                out->needed[out->needed_count++] = (uint32_t)val;
            }
            break;
        default:
            break;
        }
    }

    return 0;
}

/* ---- 调试输出 ---- */

static void dyn_kv(const char *k, uint64_t v) {
    early_console64_write(k);
    early_console64_write_hex64(v);
    early_console64_write("\n");
}

void openos_elf64_dyninfo_dump(const openos_elf64_dyninfo_t *info) {
    if (!info) { early_console64_write("[dyn] (null)\n"); return; }
    early_console64_write("[dyn] ==== dynamic info ====\n");
    dyn_kv("[dyn] is_pie=", (uint64_t)info->is_pie);
    dyn_kv("[dyn] load_bias=", info->load_bias);
    dyn_kv("[dyn] entry=", info->entry);
    if (info->interp) {
        early_console64_write("[dyn] interp=");
        early_console64_write(info->interp);
        early_console64_write("\n");
    }
    dyn_kv("[dyn] dyn_count=", info->dyn_count);
    dyn_kv("[dyn] strtab=", (uint64_t)info->strtab);
    dyn_kv("[dyn] strsz=", info->strsz);
    dyn_kv("[dyn] symtab=", (uint64_t)info->symtab);
    dyn_kv("[dyn] rela=", (uint64_t)info->rela);
    dyn_kv("[dyn] rela_sz=", info->rela_sz);
    dyn_kv("[dyn] jmprel=", (uint64_t)info->jmprel);
    dyn_kv("[dyn] pltrel_sz=", info->pltrel_sz);
    dyn_kv("[dyn] pltgot=", (uint64_t)info->pltgot);
    dyn_kv("[dyn] init=", info->init);
    dyn_kv("[dyn] needed_count=", (uint64_t)info->needed_count);
    /* 打印 DT_NEEDED 依赖名 */
    if (info->strtab) {
        for (int i = 0; i < info->needed_count; i++) {
            uint32_t off = info->needed[i];
            if (off < info->strsz) {
                early_console64_write("[dyn]   needed: ");
                early_console64_write(info->strtab + off);
                early_console64_write("\n");
            }
        }
    }
    early_console64_write("[dyn] ======================\n");
}

/* ==================== M5.1b：重定位 ==================== */

/*
 * 解析单个重定位需要的符号值 S。
 * 优先级：
 *   1) 符号在本模块 symtab 内已定义（st_shndx != 0）-> st_value + load_bias
 *   2) 否则调 resolver 回调（跨模块，M5.1c）
 *   3) 弱符号未定义 -> 0（合法）
 * out_is_weak_undef：置 1 表示“未解析但是弱符号”（不计错）。
 * 返回：解析到的地址；0 表示未解析。
 */
static uint64_t reloc_resolve_symbol(const openos_elf64_dyninfo_t *info,
                                     uint32_t sym_idx,
                                     openos_elf64_symresolve_fn resolver,
                                     void *resolver_user,
                                     int *out_is_weak_undef) {
    *out_is_weak_undef = 0;
    if (sym_idx == 0 || !info->symtab) {
        return 0;
    }
    const openos_elf64_sym_t *sym = &info->symtab[sym_idx];

    /* 本模块内已定义（st_shndx != SHN_UNDEF=0） */
    if (sym->st_shndx != 0) {
        return sym->st_value + info->load_bias;
    }

    /* 未定义：试图跨模块解析 */
    const char *name = 0;
    if (info->strtab && sym->st_name) {
        name = info->strtab + sym->st_name;
    }
    if (resolver && name) {
        uint64_t r = resolver(name, resolver_user);
        if (r != 0) {
            return r;
        }
    }

    /* 弱符号未解析合法（值为 0） */
    if (OPENOS_ELF64_ST_BIND(sym->st_info) == OPENOS_STB_WEAK) {
        *out_is_weak_undef = 1;
        return 0;
    }
    return 0;
}

/* 处理一张 RELA 表（主表或 PLT 表）。返回未解析非弱符号个数。 */
static uint64_t reloc_apply_table(const openos_elf64_dyninfo_t *info,
                                  const openos_elf64_rela_t *table,
                                  uint64_t table_sz,
                                  openos_elf64_symresolve_fn resolver,
                                  void *resolver_user,
                                  openos_elf64_reloc_stats_t *stats) {
    uint64_t unresolved = 0;
    if (!table || table_sz == 0) {
        return 0;
    }
    uint64_t count = table_sz / sizeof(openos_elf64_rela_t);
    for (uint64_t i = 0; i < count; i++) {
        const openos_elf64_rela_t *r = &table[i];
        uint32_t type = OPENOS_ELF64_R_TYPE(r->r_info);
        uint32_t sym  = OPENOS_ELF64_R_SYM(r->r_info);
        /* 重定位写入目标：r_offset 是模块内虚地址，加 load_bias 后为运行时地址 */
        uint64_t *where = (uint64_t *)(r->r_offset + info->load_bias);
        if (stats) stats->total++;

        switch (type) {
        case OPENOS_R_X86_64_NONE:
            if (stats) stats->skipped_count++;
            break;

        case OPENOS_R_X86_64_RELATIVE:
            /* B + A */
            *where = info->load_bias + (uint64_t)r->r_addend;
            if (stats) stats->relative_count++;
            break;

        case OPENOS_R_X86_64_64: {
            /* S + A */
            int weak = 0;
            uint64_t s = reloc_resolve_symbol(info, sym, resolver,
                                              resolver_user, &weak);
            if (s == 0 && !weak) { unresolved++; if (stats) stats->unresolved_count++; }
            *where = s + (uint64_t)r->r_addend;
            if (stats) stats->abs64_count++;
            break;
        }

        case OPENOS_R_X86_64_GLOB_DAT: {
            /* S */
            int weak = 0;
            uint64_t s = reloc_resolve_symbol(info, sym, resolver,
                                              resolver_user, &weak);
            if (s == 0 && !weak) { unresolved++; if (stats) stats->unresolved_count++; }
            *where = s;
            if (stats) stats->glob_dat_count++;
            break;
        }

        case OPENOS_R_X86_64_JUMP_SLOT: {
            /* S（M5.1b：立即绑定，直接填入符号地址） */
            int weak = 0;
            uint64_t s = reloc_resolve_symbol(info, sym, resolver,
                                              resolver_user, &weak);
            if (s == 0 && !weak) { unresolved++; if (stats) stats->unresolved_count++; }
            *where = s;
            if (stats) stats->jump_slot_count++;
            break;
        }

        case OPENOS_R_X86_64_IRELATIVE:
            /* ifunc：解析器返回值为 (B + A) 处的函数，调用它得到实际地址。
             * 内核态不宜直接调用用户 ifunc resolver；此阶段仅填入 B+A 占位。 */
            *where = info->load_bias + (uint64_t)r->r_addend;
            if (stats) stats->irelative_count++;
            break;

        default:
            /* 未支持类型（PC32/TLS 等）：暂跳过，不写入 */
            if (stats) stats->skipped_count++;
            break;
        }
    }
    return unresolved;
}

int openos_elf64_apply_relocations(const openos_elf64_dyninfo_t *info,
                                   openos_elf64_symresolve_fn resolver,
                                   void *resolver_user,
                                   openos_elf64_reloc_stats_t *stats) {
    if (!info) return -1;
    if (stats) dyn_memzero(stats, sizeof(*stats));

    uint64_t unresolved = 0;

    /* 1) DT_RELA 主重定位表 */
    unresolved += reloc_apply_table(info, info->rela, info->rela_sz,
                                    resolver, resolver_user, stats);

    /* 2) DT_JMPREL PLT 重定位表（M5.1b 立即绑定）。
     *    仅当 PLTREL 类型为 DT_RELA(7) 时才能按 rela 处理。 */
    if (info->jmprel && info->pltrel_sz > 0 &&
        (info->pltrel_type == OPENOS_DT_RELA || info->pltrel_type == 0)) {
        unresolved += reloc_apply_table(info, info->jmprel, info->pltrel_sz,
                                        resolver, resolver_user, stats);
    }

    return (int)unresolved;
}
