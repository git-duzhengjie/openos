/*
 * acpi_dsdt.c - ACPI DSDT parser for I²C HID device enumeration.
 *
 * Minimal AML parser focused on:
 *   - Device enumeration via namespace walk
 *   - _HID extraction for PNP0C50 (I²C HID) identification
 *   - _CRS execution for I²C bus address and interrupt line extraction
 *   - Resource template parsing (SerialBusI2C, Interrupt resources)
 */
#include <acpi_dsdt.h>
#include <klog64.h>
#include <string.h>
#include <types.h>
#include <stdint.h>

/* AML opcode definitions (subset needed for parsing) */
#define AML_OP_EXT_OPCODE        0x5B
#define AML_OP_SCOPE             0x10
#define AML_OP_DEVICE            0x82
#define AML_OP_NAME              0x08
#define AML_OP_METHOD            0x14
#define AML_OP_BUFFER            0x11
#define AML_OP_PACKAGE           0x12
#define AML_OP_VAR_PACKAGE       0x13
#define AML_OP_BYTE              0x0A
#define AML_OP_WORD              0x0B
#define AML_OP_DWORD             0x0C
#define AML_OP_QWORD             0x0E
#define AML_OP_STRING            0x0D
#define AML_OP_ZERO              0x00
#define AML_OP_ONE               0x01
#define AML_OP_ONES              0xFF

/* Extended opcodes (second byte after 0x5B) */
#define AML_EXT_OP_RESOURCE_IRQ  0x22
#define AML_EXT_OP_RESOURCE_DMA  0x23
#define AML_EXT_OP_RESOURCE_START_DEP 0x30
#define AML_EXT_OP_RESOURCE_END_DEP   0x31
#define AML_EXT_OP_RESOURCE_IO   0x47
#define AML_EXT_OP_RESOURCE_FIXED_IO 0x4B
#define AML_EXT_OP_RESOURCE_DMA_V2   0x55
#define AML_EXT_OP_RESOURCE_IRQ_V2   0x89
#define AML_EXT_OP_RESOURCE_GPIO     0x8C
#define AML_EXT_OP_RESOURCE_SERIAL_BUS 0x8E
#define AML_EXT_OP_RESOURCE_END_TAG  0x79

/* Resource descriptor types */
#define RESOURCE_SMALL_IRQ       0x04
#define RESOURCE_SMALL_DMA       0x05
#define RESOURCE_SMALL_START_DEP 0x06
#define RESOURCE_SMALL_END_DEP   0x07
#define RESOURCE_SMALL_IO        0x08
#define RESOURCE_SMALL_FIXED_IO  0x09
#define RESOURCE_LARGE_VENDOR    0x80
#define RESOURCE_LARGE_24BIT_MEM 0x81
#define RESOURCE_LARGE_VAR_IRQ   0x89
#define RESOURCE_LARGE_GPIO      0x8C
#define RESOURCE_LARGE_SERIAL_BUS 0x8E
#define RESOURCE_END_TAG         0x79

/* SerialBus type codes */
#define SERIAL_BUS_TYPE_I2C      0x01
#define SERIAL_BUS_TYPE_SPI      0x02
#define SERIAL_BUS_TYPE_UART     0x03

/* Internal state */
static acpi_i2c_controller_t  g_controllers[ACPI_DSDT_MAX_I2C_CONTROLLERS];
static acpi_i2c_hid_device_t  g_devices[ACPI_DSDT_MAX_I2C_HID_DEVICES];
static uint32_t               g_controller_count;
static uint32_t               g_device_count;

/* Device walk stack for scope tracking */
#define ACPI_DSDT_MAX_SCOPE_DEPTH 8
typedef struct {
    char path[ACPI_DSDT_MAX_PATH_LEN];
    size_t len;
} acpi_scope_entry_t;

static acpi_scope_entry_t g_scope_stack[ACPI_DSDT_MAX_SCOPE_DEPTH];
static int g_scope_depth;

static void scope_push(const char *name) {
    if (g_scope_depth >= ACPI_DSDT_MAX_SCOPE_DEPTH) return;
    acpi_scope_entry_t *entry = &g_scope_stack[g_scope_depth];
    if (g_scope_depth == 0) {
        strncpy(entry->path, name, sizeof(entry->path) - 1);
        entry->path[sizeof(entry->path) - 1] = '\0';
        entry->len = strlen(entry->path);
    } else {
        acpi_scope_entry_t *prev = &g_scope_stack[g_scope_depth - 1];
        strncpy(entry->path, prev->path, sizeof(entry->path) - 1);
        entry->path[sizeof(entry->path) - 1] = '\0';
        size_t remaining = sizeof(entry->path) - 1 - entry->len;
        if (remaining > 1) {
            entry->path[entry->len] = '.';
            entry->len++;
            remaining--;
            strncpy(entry->path + entry->len, name, remaining);
            entry->path[entry->len + remaining] = '\0';
            entry->len = strlen(entry->path);
        }
    }
    g_scope_depth++;
}

static void scope_pop(void) {
    if (g_scope_depth > 0) g_scope_depth--;
}

static const char *current_scope(void) {
    if (g_scope_depth == 0) return "";
    return g_scope_stack[g_scope_depth - 1].path;
}

/* AML name parsing: 4-char names, optional root '\\', parent '^' prefixes */
int acpi_aml_parse_name(acpi_aml_context_t *ctx, char *name_out, size_t max_len) {
    if (!ctx || !name_out || max_len < 5) return -1;
    const uint8_t *p = ctx->aml_ptr;
    if (p >= ctx->aml_end) return -1;
    char *out = name_out;
    size_t remain = max_len - 1;
    /* Skip prefixes */
    while (p < ctx->aml_end && (*p == '\\' || *p == '^')) {
        if (remain > 1) {
            *out++ = (char)*p++;
            remain--;
        } else {
            p++;
        }
    }
    /* Parse name segments: DualName (0x2E), MultiName (0x2F..0x3F), or 4-char */
    if (p < ctx->aml_end && *p == 0x2E) {
        /* DualName: 0x2E + 4 char + 4 char */
        p++;
        if (p + 8 > ctx->aml_end) return -1;
        for (int i = 0; i < 4 && remain > 1; i++, remain--)
            *out++ = (char)p[i] ? (char)p[i] : '_';
        if (remain > 1) { *out++ = '.'; remain--; }
        p += 4;
        for (int i = 0; i < 4 && remain > 1; i++, remain--)
            *out++ = (char)p[i] ? (char)p[i] : '_';
        p += 4;
    } else if (p < ctx->aml_end && *p >= 0x2F && *p <= 0x3F) {
        /* MultiName prefix: 0x2F = 1 segment, 0x30 = 2, ..., 0x3F = 16 */
        int segs = *p++ - 0x2E; /* 1..16 */
        for (int s = 0; s < segs; s++) {
            if (p + 4 > ctx->aml_end) return -1;
            if (s > 0 && remain > 1) { *out++ = '.'; remain--; }
            for (int i = 0; i < 4 && remain > 1; i++, remain--)
                *out++ = (char)p[i] ? (char)p[i] : '_';
            p += 4;
        }
    } else {
        /* Simple 4-char name */
        if (p + 4 > ctx->aml_end) return -1;
        for (int i = 0; i < 4 && remain > 1; i++, remain--)
            *out++ = (char)p[i] ? (char)p[i] : '_';
        p += 4;
    }
    *out = '\0';
    ctx->aml_ptr = p;
    return 0;
}

/* PkgLength parser: returns length consumed, sets *data_length to data size */
static int aml_parse_pkg_length(const uint8_t *p, const uint8_t *end,
                                size_t *data_length) {
    if (p >= end) { *data_length = 0; return 0; }
    if ((p[0] & 0xC0) == 0) {
        /* 1-byte format: bits 0-5 = length */
        *data_length = p[0] & 0x3F;
        return 1;
    } else {
        /* Multi-byte: byte0 bits 6-7 = nbytes-1 (1-3 extra bytes) */
        int n = ((p[0] >> 6) & 3) + 1; /* 2..4 bytes total */
        if (p + n > end) { *data_length = 0; return 0; }
        size_t len = (size_t)(p[0] & 0x0F);
        for (int i = 1; i < n; i++)
            len |= (size_t)p[i] << (4 + (i - 1) * 8);
        *data_length = len - n; /* subtract PkgLength bytes themselves */
        return n;
    }
}

uint64_t acpi_aml_parse_integer(acpi_aml_context_t *ctx) {
    if (!ctx || ctx->aml_ptr >= ctx->aml_end) return 0;
    const uint8_t *p = ctx->aml_ptr;
    uint64_t val = 0;
    switch (*p) {
    case AML_OP_ZERO:  val = 0; p++; break;
    case AML_OP_ONE:   val = 1; p++; break;
    case AML_OP_ONES:  val = ~0ULL; p++; break;
    case AML_OP_BYTE:  val = p[1]; p += 2; break;
    case AML_OP_WORD:  val = (uint64_t)p[1] | ((uint64_t)p[2] << 8); p += 3; break;
    case AML_OP_DWORD: val = (uint64_t)p[1] | ((uint64_t)p[2] << 8) |
                              ((uint64_t)p[3] << 16) | ((uint64_t)p[4] << 24);
                       p += 5; break;
    case AML_OP_QWORD: val = (uint64_t)p[1] | ((uint64_t)p[2] << 8) |
                              ((uint64_t)p[3] << 16) | ((uint64_t)p[4] << 24) |
                              ((uint64_t)p[5] << 32) | ((uint64_t)p[6] << 40) |
                              ((uint64_t)p[7] << 48) | ((uint64_t)p[8] << 56);
                       p += 9; break;
    default: return 0;
    }
    ctx->aml_ptr = p;
    return val;
}

int acpi_aml_parse_string(acpi_aml_context_t *ctx, char *str_out, size_t max_len) {
    if (!ctx || !str_out || max_len == 0) return -1;
    const uint8_t *p = ctx->aml_ptr;
    if (p >= ctx->aml_end || *p != AML_OP_STRING) return -1;
    p++;
    size_t i = 0;
    while (p < ctx->aml_end && *p != 0 && i < max_len - 1) {
        str_out[i++] = (char)*p++;
    }
    str_out[i] = '\0';
    ctx->aml_ptr = p + (*p == 0 ? 1 : 0);
    return 0;
}

int acpi_aml_parse_buffer(acpi_aml_context_t *ctx, const uint8_t **buf_out,
                          size_t *buf_len) {
    if (!ctx || !buf_out || !buf_len) return -1;
    const uint8_t *p = ctx->aml_ptr;
    if (p >= ctx->aml_end || *p != AML_OP_BUFFER) return -1;
    p++;
    size_t data_len;
    int pkg_bytes = aml_parse_pkg_length(p, ctx->aml_end, &data_len);
    if (pkg_bytes == 0) return -1;
    p += pkg_bytes;
    if (p + data_len > ctx->aml_end) return -1;
    *buf_out = p;
    *buf_len = data_len;
    ctx->aml_ptr = p + data_len;
    return 0;
}

/* Resource template parser - extracts I2C bus info and interrupts */
int acpi_parse_resource_template(const uint8_t *template_data, size_t len,
                                  acpi_resource_result_t *result)
{
    if (!template_data || !result) return -1;
    memset(result, 0, sizeof(acpi_resource_result_t));
    const uint8_t *p = template_data;
    const uint8_t *end = p + len;
    while (p < end) {
        uint8_t desc = *p;
        if (desc == RESOURCE_END_TAG) {
            break;
        } else if ((desc & 0x80) == 0) {
            /* Small resource descriptor */
            uint8_t type = (desc >> 3) & 0x0F;
            uint8_t small_len = desc & 0x07;
            p++;
            if (p + small_len > end) break;
            if (type == RESOURCE_SMALL_IRQ && small_len >= 2) {
                /* Small IRQ descriptor: mask(2b) + info(1b) */
                uint16_t irq_mask = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
                uint8_t info = (small_len >= 3) ? p[2] : 0;
                /* Find first set bit as GSI */
                for (int i = 0; i < 16; i++) {
                    if (irq_mask & (1 << i)) {
                        result->interrupt_gsi = i;
                        result->interrupt_polarity = (info & 0x02) ? 1 : 0;
                        result->interrupt_trigger = (info & 0x01) ? 1 : 0;
                        result->found_interrupt = 1;
                        break;
                    }
                }
            }
            p += small_len;
        } else {
            /* Large resource descriptor */
            uint8_t type = desc & 0x7F;
            if (p + 3 > end) break;
            uint16_t large_len = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
            p += 3;
            if (p + large_len > end) break;
            if (type == RESOURCE_LARGE_VAR_IRQ && large_len >= 6) {
                /* Extended IRQ descriptor */
                uint8_t flags = p[1];
                uint8_t count = p[2];
                if (count > 0 && large_len >= 6 + 4) {
                    result->interrupt_gsi = (uint16_t)p[3] | ((uint16_t)p[4] << 8) |
                                            ((uint16_t)p[5] << 16) | ((uint16_t)p[6] << 24);
                    result->interrupt_polarity = (flags & 0x02) ? 1 : 0;
                    result->interrupt_trigger = (flags & 0x01) ? 1 : 0;
                    result->found_interrupt = 1;
                }
            } else if (type == RESOURCE_LARGE_SERIAL_BUS && large_len >= 10) {
                /* SerialBus descriptor */
                uint8_t bus_type = p[0];
                if (bus_type == SERIAL_BUS_TYPE_I2C) {
                    /* I2C SerialBus: type(1) flags(2) rev(1) type_len(2) + data */
                    uint16_t type_specific_len = (uint16_t)p[4] | ((uint16_t)p[5] << 8);
                    if (type_specific_len >= 4) {
                        /* I2C specific: bus_speed_hz(4) slave_addr(2) */
                        result->i2c_address = (uint16_t)p[10] | ((uint16_t)p[11] << 8);
                        result->i2c_address &= 0x7F; /* 7-bit address */
                        result->found_i2c = 1;
                    }
                }
            }
            p += large_len;
        }
    }
    return 0;
}

/* Forward declaration */
static int walk_namespace(const uint8_t *start, size_t len,
                          acpi_aml_context_t *parent_ctx);

/* Parse a _HID method or object */
static int parse_hid_object(acpi_aml_context_t *ctx, char *hid_out, size_t max_len)
{
    const uint8_t *p = ctx->aml_ptr;
    if (p >= ctx->aml_end) return -1;
    if (*p == AML_OP_METHOD) {
        /* Method: opcode + name + flags */
        p++;
        p += 4; /* skip name */
        p++;    /* skip flags */
        ctx->aml_ptr = p;
        /* We don't execute methods for now; return dummy */
        strncpy(hid_out, "UNKNOWN", max_len);
        return 0;
    } else if (*p == AML_OP_STRING) {
        return acpi_aml_parse_string(ctx, hid_out, max_len);
    } else if (*p == AML_OP_BUFFER) {
        const uint8_t *buf;
        size_t buf_len;
        if (acpi_aml_parse_buffer(ctx, &buf, &buf_len) == 0 && buf_len > 0) {
            strncpy(hid_out, (const char *)buf, max_len);
            hid_out[max_len - 1] = '\0';
            return 0;
        }
    } else if (*p == AML_OP_DWORD || *p == AML_OP_WORD || *p == AML_OP_BYTE ||
               *p == AML_OP_ZERO || *p == AML_OP_ONE || *p == AML_OP_ONES ||
               *p == AML_OP_QWORD) {
        /* Integer-encoded EISA ID. ACPI encodes PNP0C50 etc as 32-bit
         * compressed EISA IDs: 7 bits per char (5 chars) + 3 bits mfr code.
         * Format: bits[31:26]=c1, [25:20]=c2, [19:14]=c3, [13:8]=c4, [7:1]=c5, [0]=0
         * Each char: 0x40..0x5F maps to 'A'..'Z', 0x30..0x39 maps to '0'..'9' */
        uint64_t val = acpi_aml_parse_integer(ctx);
        uint32_t eid = (uint32_t)val;
        char c1 = (char)(((eid >> 26) & 0x1F) + 0x40);
        char c2 = (char)(((eid >> 21) & 0x1F) + 0x40);
        char c3 = (char)(((eid >> 16) & 0x1F) + 0x40);
        char c4 = (char)(((eid >> 11) & 0x1F) + 0x40);
        char c5 = (char)(((eid >> 6)  & 0x1F) + 0x40);
        /* Build string: c1c2c3 followed by hex product number */
        uint16_t product = (uint16_t)(eid & 0xFFFF);
        /* For EISA ID: c1c2c3 are letters, c4c5 + product form the ID */
        /* Standard decoding: 3 letters + 4 hex digits */
        /* Fix: EISA ID is 7 chars: 3 letters + 4 hex digits */
        char eisa[8];
        eisa[0] = (((eid >> 26) & 0x1F) + 0x40);
        eisa[1] = (((eid >> 21) & 0x1F) + 0x40);
        eisa[2] = (((eid >> 16) & 0x1F) + 0x40);
        /* Product number: 4 hex digits from lower 16 bits */
        uint8_t hi = (eid >> 8) & 0xFF;
        uint8_t lo = eid & 0xFF;
        eisa[3] = (hi >> 4) < 10 ? '0' + (hi >> 4) : 'A' + (hi >> 4) - 10;
        eisa[4] = (hi & 0xF) < 10 ? '0' + (hi & 0xF) : 'A' + (hi & 0xF) - 10;
        eisa[5] = (lo >> 4) < 10 ? '0' + (lo >> 4) : 'A' + (lo >> 4) - 10;
        eisa[6] = (lo & 0xF) < 10 ? '0' + (lo & 0xF) : 'A' + (lo & 0xF) - 10;
        eisa[7] = '\0';
        (void)c1; (void)c2; (void)c3; (void)c4; (void)c5; (void)product;
        strncpy(hid_out, eisa, max_len);
        hid_out[max_len - 1] = '\0';
        return 0;
    }
    return -1;
}

/* Parse a _CRS buffer containing resource template */
static int parse_crs_object(acpi_aml_context_t *ctx, acpi_resource_result_t *res)
{
    const uint8_t *p = ctx->aml_ptr;
    if (p >= ctx->aml_end) return -1;
    if (*p == AML_OP_METHOD) {
        /* Method: skip for now */
        p++;
        p += 4; /* name */
        p++;    /* flags */
        ctx->aml_ptr = p;
        memset(res, 0, sizeof(acpi_resource_result_t));
        return 0;
    } else if (*p == AML_OP_BUFFER) {
        const uint8_t *buf;
        size_t buf_len;
        if (acpi_aml_parse_buffer(ctx, &buf, &buf_len) == 0) {
            return acpi_parse_resource_template(buf, buf_len, res);
        }
    }
    return -1;
}

/* Device context for collecting device info during namespace walk */
typedef struct {
    char name[ACPI_DSDT_MAX_NAME_LEN];
    char hid[ACPI_DSDT_MAX_NAME_LEN];
    uint8_t is_i2c_controller;
    uint8_t is_i2c_hid;
    acpi_resource_result_t resources;
} device_parse_ctx_t;

/* Process a single Device object and its children */
static int process_device_object(acpi_aml_context_t *ctx, const char *dev_name)
{
    device_parse_ctx_t dev_ctx;
    memset(&dev_ctx, 0, sizeof(dev_ctx));
    strncpy(dev_ctx.name, dev_name, sizeof(dev_ctx.name));
    strncpy(dev_ctx.hid, "UNKNOWN", sizeof(dev_ctx.hid));
    /* Skip PkgLength to get to device body.
     * PkgLength covers: PkgLength encoding + Name + body.
     * So body starts at ctx->aml_ptr + pkg_bytes + name_len (4 bytes).
     * body length = data_len - pkg_bytes - 4 */
    size_t data_len;
    int pkg_bytes = aml_parse_pkg_length(ctx->aml_ptr, ctx->aml_end, &data_len);
    if (pkg_bytes == 0) return -1;
    ctx->aml_ptr += pkg_bytes;
    /* Skip the 4-byte device name (already known via dev_name) */
    ctx->aml_ptr += 4;
    const uint8_t *body_start = ctx->aml_ptr;
    const uint8_t *body_end = body_start + data_len - pkg_bytes - 4;
    if (body_end > ctx->aml_end) body_end = ctx->aml_end;
    /* Push this device onto scope stack */
    scope_push(dev_name);
    /* Walk device body looking for _HID, _CRS, child devices */
    acpi_aml_context_t body_ctx;
    body_ctx.aml_start = body_start;
    body_ctx.aml_end = body_end;
    body_ctx.aml_ptr = body_start;
    body_ctx.error_count = 0;
    while (body_ctx.aml_ptr < body_ctx.aml_end) {
        const uint8_t *p = body_ctx.aml_ptr;
        uint8_t opcode = *p;
        if (opcode == AML_OP_EXT_OPCODE) {
            p++;
            if (p >= body_ctx.aml_end) break;
            opcode = *p;
            if (opcode == AML_OP_DEVICE) {
                /* Nested Device: same logic as top-level Device */
                p++;
                size_t dev_len;
                int dev_pkg = aml_parse_pkg_length(p, body_ctx.aml_end, &dev_len);
                if (dev_pkg > 0) {
                    const uint8_t *dev_name_start = p + dev_pkg;
                    body_ctx.aml_ptr = dev_name_start;
                    char child_name[ACPI_DSDT_MAX_NAME_LEN];
                    if (acpi_aml_parse_name(&body_ctx, child_name, sizeof(child_name)) == 0) {
                        process_device_object(&body_ctx, child_name);
                    }
                    body_ctx.aml_ptr = p + dev_len;
                }
                continue;
            }
            p++;
            body_ctx.aml_ptr = p;
            continue;
        }
        if (opcode == AML_OP_SCOPE) {
            p++;
            /* Scope inside Device body: same logic as top-level Scope */
            size_t scope_len;
            int scope_pkg = aml_parse_pkg_length(p, body_ctx.aml_end, &scope_len);
            if (scope_pkg > 0) {
                const uint8_t *scope_body_start = p + scope_pkg;
                body_ctx.aml_ptr = scope_body_start;
                char scope_name[ACPI_DSDT_MAX_NAME_LEN];
                if (acpi_aml_parse_name(&body_ctx, scope_name, sizeof(scope_name)) == 0) {
                    size_t name_bytes = (size_t)(body_ctx.aml_ptr - scope_body_start);
                    size_t body_len = scope_len - scope_pkg - name_bytes;
                    scope_push(scope_name);
                    walk_namespace(body_ctx.aml_ptr, body_len, &body_ctx);
                    scope_pop();
                }
                body_ctx.aml_ptr = p + scope_len;
            }
            continue;
        }
        if (opcode == AML_OP_NAME) {
            p++;
            char obj_name[ACPI_DSDT_MAX_NAME_LEN];
            body_ctx.aml_ptr = p;
            if (acpi_aml_parse_name(&body_ctx, obj_name, sizeof(obj_name)) != 0) {
                p = body_ctx.aml_ptr;
                continue;
            }
            p = body_ctx.aml_ptr;
            /* Check for _HID or _CID */
            if (strcmp(obj_name, "_HID") == 0 || strcmp(obj_name, "_CID") == 0) {
                char hid_val[ACPI_DSDT_MAX_NAME_LEN];
                if (parse_hid_object(&body_ctx, hid_val, sizeof(hid_val)) == 0) {
                    strncpy(dev_ctx.hid, hid_val, sizeof(dev_ctx.hid));
                    /* Detect device type */
                    if (strcmp(hid_val, "PNP0C50") == 0) {
                        dev_ctx.is_i2c_hid = 1;
                    } else if (strcmp(hid_val, "PNP0A03") == 0 ||
                               strcmp(hid_val, "PNP0A08") == 0 ||
                               strcmp(hid_val, "INT33C0") == 0 ||
                               strcmp(hid_val, "INT3432") == 0 ||
                               strcmp(hid_val, "ACPI0003") == 0) {
                        dev_ctx.is_i2c_controller = 1;
                    }
                }
                p = body_ctx.aml_ptr;
                continue;
            }
            /* Check for _CRS */
            if (strcmp(obj_name, "_CRS") == 0) {
                parse_crs_object(&body_ctx, &dev_ctx.resources);
                p = body_ctx.aml_ptr;
                continue;
            }
            /* Skip other name objects */
            body_ctx.aml_ptr++;
            p = body_ctx.aml_ptr;
            continue;
        }
        /* Skip other opcodes by advancing */
        body_ctx.aml_ptr++;
    }
    /* Record this device if it's interesting */
    if (dev_ctx.is_i2c_controller && g_controller_count < ACPI_DSDT_MAX_I2C_CONTROLLERS) {
        acpi_i2c_controller_t *ctrl = &g_controllers[g_controller_count++];
        memset(ctrl, 0, sizeof(acpi_i2c_controller_t));
        strncpy(ctrl->name, dev_ctx.name, sizeof(ctrl->name));
        strncpy(ctrl->path, current_scope(), sizeof(ctrl->path));
        strncpy(ctrl->hid, dev_ctx.hid, sizeof(ctrl->hid));
        ctrl->bus_number = g_controller_count - 1; /* Simplified mapping */
        ctrl->valid = 1;
        klog_emit(KLOG_INFO, KLOG_FAC_KERNEL, "ACPI DSDT: Found I2C controller");
    }
    if (dev_ctx.is_i2c_hid && g_device_count < ACPI_DSDT_MAX_I2C_HID_DEVICES) {
        acpi_i2c_hid_device_t *dev = &g_devices[g_device_count++];
        memset(dev, 0, sizeof(acpi_i2c_hid_device_t));
        strncpy(dev->name, dev_ctx.name, sizeof(dev->name));
        strncpy(dev->path, current_scope(), sizeof(dev->path));
        strncpy(dev->hid, dev_ctx.hid, sizeof(dev->hid));
        dev->i2c_bus_number = 0; /* TODO: map to controller via namespace */
        dev->i2c_address = dev_ctx.resources.found_i2c ? dev_ctx.resources.i2c_address : 0;
        dev->interrupt_gsi = dev_ctx.resources.found_interrupt ? dev_ctx.resources.interrupt_gsi : 0;
        dev->interrupt_polarity = dev_ctx.resources.interrupt_polarity;
        dev->interrupt_trigger = dev_ctx.resources.interrupt_trigger;
        dev->valid = 1;
        klog_emit(KLOG_INFO, KLOG_FAC_KERNEL, "ACPI DSDT: Found I2C HID device");
    }
    scope_pop();
    return 0;
}

/* Walk AML namespace looking for Device nodes */
static int walk_namespace(const uint8_t *start, size_t len,
                          acpi_aml_context_t *parent_ctx)
{
    acpi_aml_context_t ctx;
    ctx.aml_start = start;
    ctx.aml_end = start + len;
    ctx.aml_ptr = start;
    ctx.error_count = 0;
    while (ctx.aml_ptr < ctx.aml_end) {
        const uint8_t *p = ctx.aml_ptr;
        uint8_t opcode = *p;
        if (opcode == AML_OP_EXT_OPCODE) {
            p++;
            if (p >= ctx.aml_end) break;
            opcode = *p;
            if (opcode == AML_OP_DEVICE) {
                /* Device object: 0x5B 0x82 + PkgLength + Name + body
                 * PkgLength covers everything after 0x82, including the
                 * PkgLength encoding itself and the Name.
                 * After process_device_object returns, aml_ptr has been
                 * advanced past PkgLength + Name, but only through the
                 * body. We need to advance to the end of the Device. */
                p++;
                size_t dev_len;
                int dev_pkg = aml_parse_pkg_length(p, ctx.aml_end, &dev_len);
                if (dev_pkg > 0) {
                    const uint8_t *dev_name_start = p + dev_pkg;
                    ctx.aml_ptr = dev_name_start;
                    char dev_name[ACPI_DSDT_MAX_NAME_LEN];
                    if (acpi_aml_parse_name(&ctx, dev_name, sizeof(dev_name)) == 0) {
                        process_device_object(&ctx, dev_name);
                    }
                    /* Advance to end of this Device object */
                    ctx.aml_ptr = p + dev_len;
                }
                continue;
            }
            /* Other extended opcodes - skip */
            p++;
            ctx.aml_ptr = p;
            continue;
        }
        if (opcode == AML_OP_SCOPE) {
            p++;
            /* Scope: 0x10 + PkgLength + Name + body
             * PkgLength covers everything after the opcode, including
             * the PkgLength encoding itself and the Name.
             * So body starts at p + pkg_bytes + name_len, and body
             * length = scope_len - pkg_bytes - name_len */
            size_t scope_len;
            int scope_pkg = aml_parse_pkg_length(p, ctx.aml_end, &scope_len);
            if (scope_pkg > 0) {
                const uint8_t *scope_body_start = p + scope_pkg;
                /* Parse the scope name */
                ctx.aml_ptr = scope_body_start;
                char scope_name[ACPI_DSDT_MAX_NAME_LEN];
                if (acpi_aml_parse_name(&ctx, scope_name, sizeof(scope_name)) == 0) {
                    /* Body starts after the name, body length = scope_len - pkg_bytes - name_bytes */
                    size_t name_bytes = (size_t)(ctx.aml_ptr - scope_body_start);
                    size_t body_len = scope_len - scope_pkg - name_bytes;
                    scope_push(scope_name);
                    walk_namespace(ctx.aml_ptr, body_len, &ctx);
                    scope_pop();
                }
                /* Advance past the entire Scope object */
                ctx.aml_ptr = p + scope_len;
            }
            continue;
        }
        /* Skip other opcodes */
        ctx.aml_ptr++;
    }
    if (parent_ctx) parent_ctx->error_count += ctx.error_count;
    return ctx.error_count == 0 ? 0 : -1;
}

/* XSDT structure (forward declaration from acpi64.c) */
typedef struct {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    char     creator_id[4];
    uint32_t creator_revision;
    /* Followed by table entries: 8 bytes each for XSDT, 4 for RSDT */
} acpi_sdt_header_t;

/* FADT access (from acpi64.c) */
extern volatile acpi_sdt_header_t *g_fadt_header;

/* Initialize DSDT parser */
int acpi_dsdt_init(void)
{
    memset(g_controllers, 0, sizeof(g_controllers));
    memset(g_devices, 0, sizeof(g_devices));
    g_controller_count = 0;
    g_device_count = 0;
    g_scope_depth = 0;
    /* Get DSDT pointer from FADT */
    if (!g_fadt_header) {
        klog_emit(KLOG_INFO, KLOG_FAC_KERNEL, "ACPI DSDT: No FADT available, skipping enumeration");
        return -1;
    }
    /* FADT layout: header (36 bytes) + FACS pointer (4) + DSDT pointer (4) + ... */
    const uint8_t *fadt_bytes = (const uint8_t *)g_fadt_header;
    uint32_t dsdt_phys = 0;
    if (g_fadt_header->length >= 40) {
        dsdt_phys = *(const uint32_t *)(fadt_bytes + 40);
    }
    if (dsdt_phys == 0) {
        klog_emit(KLOG_INFO, KLOG_FAC_KERNEL, "ACPI DSDT: No DSDT pointer in FADT, skipping enumeration");
        return -1;
    }
    /* For now: treat physical address as virtual (identity mapped in early boot) */
    const acpi_sdt_header_t *dsdt_header = (const acpi_sdt_header_t *)(uintptr_t)dsdt_phys;
    /* Verify DSDT signature */
    if (memcmp(dsdt_header->signature, "DSDT", 4) != 0) {
        klog_emit(KLOG_WARN, KLOG_FAC_KERNEL, "ACPI DSDT: Invalid DSDT signature");
        return -1;
    }
    klog_emit(KLOG_INFO, KLOG_FAC_KERNEL, "ACPI DSDT: Parsing DSDT table");
    /* Start parsing the AML data (immediately after the DSDT SDT header) */
    const uint8_t *aml_data = (const uint8_t *)dsdt_header + sizeof(acpi_sdt_header_t);
    size_t aml_len = dsdt_header->length - sizeof(acpi_sdt_header_t);
    /* Walk the namespace */
    scope_push("\\");
    int ret = walk_namespace(aml_data, aml_len, NULL);
    scope_pop();
    if (ret == 0) {
        klog_emit(KLOG_INFO, KLOG_FAC_KERNEL, "ACPI DSDT: Enumeration complete");
    } else {
        klog_emit(KLOG_WARN, KLOG_FAC_KERNEL, "ACPI DSDT: Parse completed with errors");
    }
    return ret;
}

/* Public API implementations */
uint32_t acpi_dsdt_i2c_hid_device_count(void)
{
    return g_device_count;
}

const acpi_i2c_hid_device_t *acpi_dsdt_get_i2c_hid_device(uint32_t index)
{
    if (index >= g_device_count) return NULL;
    const acpi_i2c_hid_device_t *dev = &g_devices[index];
    return dev->valid ? dev : NULL;
}

uint32_t acpi_dsdt_i2c_controller_count(void)
{
    return g_controller_count;
}

const acpi_i2c_controller_t *acpi_dsdt_get_i2c_controller(uint32_t index)
{
    if (index >= g_controller_count) return NULL;
    const acpi_i2c_controller_t *ctrl = &g_controllers[index];
    return ctrl->valid ? ctrl : NULL;
}

const acpi_i2c_hid_device_t *acpi_dsdt_find_i2c_hid_by_hid(const char *hid)
{
    if (!hid) return NULL;
    for (uint32_t i = 0; i < g_device_count; i++) {
        acpi_i2c_hid_device_t *dev = &g_devices[i];
        if (dev->valid && strcmp(dev->hid, hid) == 0) {
            return dev;
        }
    }
    return NULL;
}
