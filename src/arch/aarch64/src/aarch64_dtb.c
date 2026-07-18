#include "aarch64_dtb.h"

uint32_t aarch64_dtb_be32_to_cpu(uint32_t value)
{
    return ((value & 0x000000ffU) << 24) |
           ((value & 0x0000ff00U) << 8) |
           ((value & 0x00ff0000U) >> 8) |
           ((value & 0xff000000U) >> 24);
}

uint64_t aarch64_dtb_be64_to_cpu(uint64_t value)
{
    return ((uint64_t)aarch64_dtb_be32_to_cpu((uint32_t)(value & 0xffffffffU)) << 32) |
           (uint64_t)aarch64_dtb_be32_to_cpu((uint32_t)(value >> 32));
}

static int aarch64_dtb_streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

static int aarch64_dtb_starts_with(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s != *prefix) {
            return 0;
        }
        ++s;
        ++prefix;
    }
    return 1;
}

static uint32_t aarch64_dtb_read_u32(const uint8_t *ptr)
{
    uint32_t raw = ((uint32_t)ptr[0]) |
                   ((uint32_t)ptr[1] << 8) |
                   ((uint32_t)ptr[2] << 16) |
                   ((uint32_t)ptr[3] << 24);
    return aarch64_dtb_be32_to_cpu(raw);
}

static uint64_t aarch64_dtb_read_u64(const uint8_t *ptr)
{
    uint64_t hi = aarch64_dtb_read_u32(ptr);
    uint64_t lo = aarch64_dtb_read_u32(ptr + 4);
    return (hi << 32) | lo;
}

int aarch64_dtb_parser_init(aarch64_dtb_parser_t *parser, uint64_t dtb_base)
{
    if (!parser || !dtb_base) {
        return -1;
    }
    const fdt_header_t *header = (const fdt_header_t *)(uintptr_t)dtb_base;
    uint32_t magic = aarch64_dtb_be32_to_cpu(header->magic);
    if (magic != FDT_MAGIC) {
        parser->base = 0;
        parser->header = 0;
        return -1;
    }
    parser->base = (const uint8_t *)(uintptr_t)dtb_base;
    parser->header = header;
    parser->struct_base = parser->base + aarch64_dtb_be32_to_cpu(header->off_dt_struct);
    parser->strings_base = parser->base + aarch64_dtb_be32_to_cpu(header->off_dt_strings);
    parser->struct_size = aarch64_dtb_be32_to_cpu(header->size_dt_struct);
    parser->strings_size = aarch64_dtb_be32_to_cpu(header->size_dt_strings);
    parser->struct_offset = 0;
    return 0;
}

/* find node by exact name, return offset of first FDT_PROP token, -1 if not found */
static long aarch64_dtb_find_node(const aarch64_dtb_parser_t *parser, const char *target_name)
{
    if (!parser || !parser->struct_base) {
        return -1;
    }
    size_t offset = 0;
    int depth = -1;
    int in_target = 0;
    int target_depth = -1;
    while (offset + 4 <= parser->struct_size) {
        uint32_t token = aarch64_dtb_read_u32(parser->struct_base + offset);
        offset += 4;
        if (token == FDT_BEGIN_NODE) {
            depth++;
            const char *name = (const char *)(parser->struct_base + offset);
            size_t name_len = 0;
            while (offset + name_len < parser->struct_size && name[name_len]) {
                name_len++;
            }
            offset += name_len + 1;
            offset = (offset + 3) & ~((size_t)3);
            /* match target */
            if (!in_target) {
                int matched = 0;
                if (aarch64_dtb_streq(target_name, "/") && depth == 0) {
                    matched = (name_len == 0);
                } else if (depth >= 1) {
                    /* match by prefix (e.g. "memory@40000000" starts with "memory") */
                    matched = aarch64_dtb_starts_with(name, target_name);
                }
                if (matched) {
                    in_target = 1;
                    target_depth = depth;
                    return (long)offset;
                }
            }
        } else if (token == FDT_END_NODE) {
            if (in_target && depth == target_depth) {
                in_target = 0;
            }
            depth--;
        } else if (token == FDT_PROP) {
            if (offset + 8 > parser->struct_size) {
                break;
            }
            uint32_t len = aarch64_dtb_read_u32(parser->struct_base + offset);
            offset += 8; /* len + nameoff */
            offset += len;
            offset = (offset + 3) & ~((size_t)3);
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            break;
        } else {
            break;
        }
    }
    return -1;
}

/* iterate props inside a node starting at prop_start; return prop value ptr and length */
static const uint8_t *aarch64_dtb_find_prop_in_node(const aarch64_dtb_parser_t *parser,
                                                   size_t start_offset,
                                                   const char *prop_name,
                                                   uint32_t *out_len)
{
    size_t offset = start_offset;
    int depth = 0;
    while (offset + 4 <= parser->struct_size) {
        uint32_t token = aarch64_dtb_read_u32(parser->struct_base + offset);
        offset += 4;
        if (token == FDT_BEGIN_NODE) {
            depth++;
            const char *name = (const char *)(parser->struct_base + offset);
            size_t name_len = 0;
            while (offset + name_len < parser->struct_size && name[name_len]) {
                name_len++;
            }
            offset += name_len + 1;
            offset = (offset + 3) & ~((size_t)3);
        } else if (token == FDT_END_NODE) {
            if (depth == 0) {
                return 0;
            }
            depth--;
        } else if (token == FDT_PROP) {
            if (offset + 8 > parser->struct_size) {
                return 0;
            }
            uint32_t len = aarch64_dtb_read_u32(parser->struct_base + offset);
            uint32_t nameoff = aarch64_dtb_read_u32(parser->struct_base + offset + 4);
            offset += 8;
            if (depth == 0 && nameoff < parser->strings_size) {
                const char *name = (const char *)(parser->strings_base + nameoff);
                if (aarch64_dtb_streq(name, prop_name)) {
                    if (out_len) {
                        *out_len = len;
                    }
                    return parser->struct_base + offset;
                }
            }
            offset += len;
            offset = (offset + 3) & ~((size_t)3);
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            break;
        } else {
            break;
        }
    }
    return 0;
}

int aarch64_dtb_parse_memory(aarch64_dtb_parser_t *parser, aarch64_dtb_memory_info_t *mem_info)
{
    if (!parser || !mem_info) {
        return -1;
    }
    mem_info->base = 0;
    mem_info->size = 0;
    mem_info->valid = 0;
    long node_start = aarch64_dtb_find_node(parser, "memory");
    if (node_start < 0) {
        return -1;
    }
    uint32_t reg_len = 0;
    const uint8_t *reg = aarch64_dtb_find_prop_in_node(parser, (size_t)node_start, "reg", &reg_len);
    if (!reg || reg_len < 16) {
        return -1;
    }
    /* Assume #address-cells=2, #size-cells=2 (QEMU virt default) */
    mem_info->base = aarch64_dtb_read_u64(reg);
    mem_info->size = aarch64_dtb_read_u64(reg + 8);
    mem_info->valid = 1;
    return 0;
}

int aarch64_dtb_parse_chosen(aarch64_dtb_parser_t *parser, aarch64_dtb_chosen_info_t *chosen_info)
{
    if (!parser || !chosen_info) {
        return -1;
    }
    chosen_info->bootargs = 0;
    chosen_info->initrd_start = 0;
    chosen_info->initrd_end = 0;
    chosen_info->valid = 0;
    long node_start = aarch64_dtb_find_node(parser, "chosen");
    if (node_start < 0) {
        return -1;
    }
    uint32_t len = 0;
    const uint8_t *bootargs = aarch64_dtb_find_prop_in_node(parser, (size_t)node_start, "bootargs", &len);
    if (bootargs && len > 0) {
        chosen_info->bootargs = (const char *)bootargs;
    }
    const uint8_t *start = aarch64_dtb_find_prop_in_node(parser, (size_t)node_start, "linux,initrd-start", &len);
    if (start) {
        chosen_info->initrd_start = (len == 8) ? aarch64_dtb_read_u64(start) : (uint64_t)aarch64_dtb_read_u32(start);
    }
    const uint8_t *end = aarch64_dtb_find_prop_in_node(parser, (size_t)node_start, "linux,initrd-end", &len);
    if (end) {
        chosen_info->initrd_end = (len == 8) ? aarch64_dtb_read_u64(end) : (uint64_t)aarch64_dtb_read_u32(end);
    }
    chosen_info->valid = 1;
    return 0;
}
