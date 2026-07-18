/* =========================================================================
 * hid_parser.c -- Minimal HID Report Descriptor parser (M8-C.1).
 * ========================================================================= */

#include "hid_parser.h"

/* Item type / tag (short items). */
#define HID_ITEM_TYPE_MAIN   0x00
#define HID_ITEM_TYPE_GLOBAL 0x01
#define HID_ITEM_TYPE_LOCAL  0x02

#define HID_TAG_INPUT              0x08
#define HID_TAG_COLLECTION         0x0A
#define HID_TAG_END_COLLECTION     0x0C

#define HID_TAG_USAGE_PAGE         0x00
#define HID_TAG_LOGICAL_MIN        0x01
#define HID_TAG_LOGICAL_MAX        0x02
#define HID_TAG_REPORT_SIZE        0x07
#define HID_TAG_REPORT_ID          0x08
#define HID_TAG_REPORT_COUNT       0x09

#define HID_TAG_USAGE              0x00
#define HID_TAG_USAGE_MIN          0x01
#define HID_TAG_USAGE_MAX          0x02

#define USAGE_PAGE_GENERIC_DESKTOP 0x01
#define USAGE_PAGE_DIGITIZER       0x0D

#define USAGE_GD_X                 0x30
#define USAGE_GD_Y                 0x31

#define USAGE_DIG_FINGER           0x22
#define USAGE_DIG_TIP_SWITCH       0x42
#define USAGE_DIG_CONTACT_ID       0x51
#define USAGE_DIG_CONTACT_COUNT    0x54

#define LOCAL_USAGE_STACK_MAX 16

/* Global state machine (section 6.2.2) */
typedef struct {
    uint8_t  report_id;
    uint16_t report_size_bits;
    uint16_t report_count;
    int32_t  logical_min;
    int32_t  logical_max;
    uint16_t usage_page;
    uint8_t  has_report_id;
} global_ctx_t;

/* Local state machine */
typedef struct {
    uint32_t usages[LOCAL_USAGE_STACK_MAX];
    uint8_t  usage_count;
} local_ctx_t;

/* Parser object: accumulator */
typedef struct {
    hid_touch_layout_t *out;
    global_ctx_t global;
    local_ctx_t  local;
    uint16_t bit_offset;
    uint8_t  in_digitizer_finger; /* inside Collection Finger */
    uint8_t  in_report_id;        /* inside a Report ID block */
    uint8_t  cur_finger_idx;
    uint8_t  current_report_id;   /* 当前 Report ID */
} parser_t;

/* Collect remaining state from a single short item. */
static inline uint32_t
hid_get_item_data(const uint8_t *desc, uint8_t size_enc, size_t pos, size_t desc_len) {
    uint32_t val = 0;
    size_t i, n = (size_enc == 3 ? 4 : size_enc);
    for (i = 0; i < n; i++) {
        if (pos + 1 + i >= desc_len) break;
        val |= (uint32_t)desc[pos + 1 + i] << (i * 8);
    }
    return val;
}

static bool
parser_handle_main(parser_t *p, uint8_t tag, uint32_t data) {
    if (tag == HID_TAG_COLLECTION) {
        bool is_finger =
            p->local.usage_count >= 1 &&
            p->local.usages[0] == (((uint32_t)USAGE_PAGE_DIGITIZER) << 16 | USAGE_DIG_FINGER);
        if (is_finger && !p->in_digitizer_finger) {
            p->in_digitizer_finger = 1;
            if (p->cur_finger_idx < HID_TOUCH_MAX_SLOTS) {
                p->out->slots[p->cur_finger_idx].present = 1;
            }
        }
        p->local.usage_count = 0;
        return true;
    }

    if (tag == HID_TAG_END_COLLECTION) {
        if (p->in_digitizer_finger) {
            p->in_digitizer_finger = 0;
            if (p->cur_finger_idx < HID_TOUCH_MAX_SLOTS) {
                p->cur_finger_idx++;
                p->out->slot_count = p->cur_finger_idx;
            }
        }
        p->local.usage_count = 0;
        return true;
    }

    if (tag == HID_TAG_INPUT) {
        /* Each Input item advances bit_offset by count*size. */
        /* If we have multiple local usages, each usage gets one field at same logical params. */
        uint16_t bits_per_field = p->global.report_size_bits;
        uint16_t field_count = p->local.usage_count >= p->global.report_count ?
                               p->local.usage_count : p->global.report_count;

        for (uint16_t fi = 0; fi < field_count; fi++) {
            uint32_t usage = (fi < p->local.usage_count) ? p->local.usages[fi] : 0;
            uint16_t page = (uint16_t)(usage >> 16);
            uint16_t u = (uint16_t)(usage & 0xFFFF);

            hid_field_t f;
            f.present = 1;
            f.bit_offset = p->bit_offset + fi * bits_per_field;
            f.bit_size = bits_per_field;
            f.logical_min = p->global.logical_min;
            f.logical_max = p->global.logical_max;

            if (page == USAGE_PAGE_DIGITIZER) {
                if (u == USAGE_DIG_TIP_SWITCH) {
                    if (p->in_digitizer_finger && p->cur_finger_idx < HID_TOUCH_MAX_SLOTS) {
                        p->out->slots[p->cur_finger_idx].tip = f;
                    }
                }
                if (u == USAGE_DIG_CONTACT_ID) {
                    if (p->in_digitizer_finger && p->cur_finger_idx < HID_TOUCH_MAX_SLOTS) {
                        p->out->slots[p->cur_finger_idx].contact_id = f;
                    }
                }
                if (u == USAGE_DIG_CONTACT_COUNT) {
                    p->out->contact_count = f;
                }
            }
            if (page == USAGE_PAGE_GENERIC_DESKTOP && p->in_digitizer_finger) {
                if (u == USAGE_GD_X && p->cur_finger_idx < HID_TOUCH_MAX_SLOTS) {
                    p->out->slots[p->cur_finger_idx].x = f;
                }
                if (u == USAGE_GD_Y && p->cur_finger_idx < HID_TOUCH_MAX_SLOTS) {
                    p->out->slots[p->cur_finger_idx].y = f;
                }
            }
        }

        p->bit_offset += p->global.report_size_bits * p->global.report_count;
        /* Local usages are consumed after Input; pop them. */
        p->local.usage_count = 0;
        return true;
    }

    /* Any other Main item also resets local state per HID spec. */
    (void)tag;
    return true;
}

static void
parser_handle_global(parser_t *p, uint8_t tag, uint32_t data) {
    switch (tag) {
    case HID_TAG_USAGE_PAGE:
        p->global.usage_page = (uint16_t)data;
        break;
    case HID_TAG_LOGICAL_MIN:
        p->global.logical_min = (int32_t)data;
        break;
    case HID_TAG_LOGICAL_MAX:
        p->global.logical_max = (int32_t)data;
        break;
    case HID_TAG_REPORT_SIZE:
        p->global.report_size_bits = (uint16_t)data;
        break;
    case HID_TAG_REPORT_COUNT:
        p->global.report_count = (uint16_t)data;
        break;
    case HID_TAG_REPORT_ID:
        p->global.report_id = (uint8_t)data;
        p->global.has_report_id = 1;
        if (!p->out->has_report_id) {
            p->out->has_report_id = 1;
            p->out->report_id = (uint8_t)data;
            /* First byte of every report will be the Report ID; advance bit_offset. */
            p->bit_offset = 8;
        }
        break;
    default:
        break;
    }
}

static void
parser_handle_local(parser_t *p, uint8_t tag, uint32_t data) {
    if (tag == HID_TAG_USAGE) {
        if (p->local.usage_count < LOCAL_USAGE_STACK_MAX) {
            /* Short-form usage carries only the low bits; combine with current page. */
            uint32_t page = (data >> 16) ? (data >> 16) : p->global.usage_page;
            uint32_t u = data & 0xFFFF;
            p->local.usages[p->local.usage_count++] = (page << 16) | u;
        }
    }
    /* Ignore usage-min/max: not needed for our supported layouts. */
    (void)tag;
    (void)data;
}

bool
hid_parse_report_descriptor(const uint8_t *desc, size_t desc_len,
                            hid_touch_layout_t *out) {
    if (!desc || !out) return false;

    /* Zero-out result. */
    for (size_t i = 0; i < sizeof(*out); i++) ((uint8_t *)out)[i] = 0;

    parser_t p;
    for (size_t i = 0; i < sizeof(p); i++) ((uint8_t *)&p)[i] = 0;
    p.out = out;

    size_t pos = 0;
    while (pos < desc_len) {
        uint8_t prefix = desc[pos];
        /* Long items: prefix == 0xFE, we don't support -> skip. */
        if (prefix == 0xFE) {
            if (pos + 2 >= desc_len) break;
            size_t data_size = desc[pos + 1];
            pos += 3 + data_size;
            continue;
        }

        uint8_t size_enc = prefix & 0x03;
        uint8_t type = (prefix >> 2) & 0x03;
        uint8_t tag = (prefix >> 4) & 0x0F;
        uint32_t data = hid_get_item_data(desc, size_enc, pos, desc_len);

        switch (type) {
        case HID_ITEM_TYPE_MAIN:
            parser_handle_main(&p, tag, data);
            break;
        case HID_ITEM_TYPE_GLOBAL:
            parser_handle_global(&p, tag, data);
            break;
        case HID_ITEM_TYPE_LOCAL:
            parser_handle_local(&p, tag, data);
            break;
        default:
            break;
        }

        /* advance */
        size_t data_size = (size_enc == 3) ? 4 : size_enc;
        pos += 1 + data_size;
    }

    out->report_bytes = (uint16_t)((p.bit_offset + 7) / 8);

    /* Success if at least one slot has X/Y/Tip filled. */
    for (uint8_t si = 0; si < out->slot_count; si++) {
        if (out->slots[si].tip.present && out->slots[si].x.present && out->slots[si].y.present) {
            return true;
        }
    }
    return false;
}

/* ---------- Report parsing ---------- */

static int32_t
field_extract(const uint8_t *data, size_t data_len, const hid_field_t *f) {
    if (!f->present || f->bit_size == 0) return 0;
    uint32_t byte_off = f->bit_offset >> 3;
    uint32_t bit_off = f->bit_offset & 7;
    uint32_t remaining = f->bit_size;
    uint32_t value = 0;
    uint32_t vshift = 0;

    while (remaining > 0) {
        if (byte_off >= data_len) break;
        uint32_t bits_this = 8 - bit_off;
        if (bits_this > remaining) bits_this = remaining;
        uint32_t mask = (uint32_t)((1u << bits_this) - 1u);
        uint32_t chunk = ((uint32_t)data[byte_off] >> bit_off) & mask;
        value |= chunk << vshift;
        vshift += bits_this;
        remaining -= bits_this;
        byte_off++;
        bit_off = 0;
    }

    /* Sign-extend if logical_min < 0. */
    if (f->logical_min < 0 && f->bit_size < 32) {
        uint32_t sign_bit = 1u << (f->bit_size - 1);
        if (value & sign_bit) {
            value |= ~((1u << f->bit_size) - 1u);
        }
    }
    return (int32_t)value;
}

bool
hid_touch_report_parse(const hid_touch_layout_t *layout,
                       const uint8_t *report, size_t report_len,
                       hid_touch_sample_t *sample) {
    if (!layout || !report || !sample) return false;
    if (report_len < layout->report_bytes) return false;

    if (layout->has_report_id) {
        if (report[0] != layout->report_id) return false;
    }

    for (size_t i = 0; i < sizeof(*sample); i++) ((uint8_t *)sample)[i] = 0;

    sample->slot_count = layout->slot_count;
    sample->contact_count = (uint8_t)field_extract(report, report_len, &layout->contact_count);

    for (uint8_t si = 0; si < layout->slot_count; si++) {
        const hid_finger_slot_t *slot = &layout->slots[si];
        if (!slot->present) continue;
        uint8_t tip = (uint8_t)field_extract(report, report_len, &slot->tip);
        int32_t cid = field_extract(report, report_len, &slot->contact_id);
        int32_t x = field_extract(report, report_len, &slot->x);
        int32_t y = field_extract(report, report_len, &slot->y);

        sample->fingers[si].present = 1;
        sample->fingers[si].tip = tip;
        sample->fingers[si].contact_id = cid;
        sample->fingers[si].x = x;
        sample->fingers[si].y = y;
        sample->fingers[si].logical_max_x = slot->x.logical_max;
        sample->fingers[si].logical_max_y = slot->y.logical_max;
    }
    return true;
}
