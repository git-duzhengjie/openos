#ifndef OPENOS_ARCH_AARCH64_DTB_H
#define OPENOS_ARCH_AARCH64_DTB_H

#include <stdint.h>
#include <stddef.h>

#define FDT_MAGIC       0xd00dfeedU
#define FDT_BEGIN_NODE  0x00000001U
#define FDT_END_NODE    0x00000002U
#define FDT_PROP        0x00000003U
#define FDT_NOP         0x00000004U
#define FDT_END         0x00000009U

typedef struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
} fdt_header_t;

typedef struct aarch64_dtb_parser {
    const uint8_t *base;
    const fdt_header_t *header;
    const uint8_t *struct_base;
    const uint8_t *strings_base;
    size_t struct_size;
    size_t strings_size;
    size_t struct_offset;
} aarch64_dtb_parser_t;

typedef struct aarch64_dtb_memory_info {
    uint64_t base;
    uint64_t size;
    int valid;
} aarch64_dtb_memory_info_t;

typedef struct aarch64_dtb_chosen_info {
    const char *bootargs;
    uint64_t initrd_start;
    uint64_t initrd_end;
    int valid;
} aarch64_dtb_chosen_info_t;

int aarch64_dtb_parser_init(aarch64_dtb_parser_t *parser, uint64_t dtb_base);
int aarch64_dtb_parse_memory(aarch64_dtb_parser_t *parser, aarch64_dtb_memory_info_t *mem_info);
int aarch64_dtb_parse_chosen(aarch64_dtb_parser_t *parser, aarch64_dtb_chosen_info_t *chosen_info);
uint32_t aarch64_dtb_be32_to_cpu(uint32_t value);
uint64_t aarch64_dtb_be64_to_cpu(uint64_t value);

#endif /* OPENOS_ARCH_AARCH64_DTB_H */
