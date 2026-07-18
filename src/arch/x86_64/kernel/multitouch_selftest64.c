/* =========================================================================
 * multitouch_selftest64.c -- M8-C.4 boot-time selftests.
 *
 * Coverage:
 *   Test A  hid_parser: synthetic Windows Precision Touch descriptor
 *           parses 2 finger slots with correct Tip/X/Y bit offsets.
 *   Test B  hid_touch_report_parse: extracts two-finger sample.
 *   Test C  gesture_multi: two-finger pinch open → scale=~2000.
 *   Test D  gesture_multi: two-finger pinch close → scale ~ 500.
 *   Test E  gesture_multi: rotate 90 degrees → delta angle ≈ 90.
 *   Test F  gesture_multi: single finger + no fingers → nothing.
 * ========================================================================= */
#include "../include/multitouch_selftest64.h"
#include "../include/early_console64.h"
#include "../../../kernel/include/hid_parser.h"
#include "../../../kernel/include/hid_type_infer.h"
#include "../../../kernel/include/gesture_multi.h"

#include <stdint.h>
#include <stddef.h>

static void mt_log(const char *s) { early_console64_write(s); }

/* Captured multi-touch events. */
#define MT_LOG_MAX 32
static gesture_multi_event_t g_m_evs[MT_LOG_MAX];
static int                   g_m_n;
static void mt_listener(const gesture_multi_event_t *ev, void *u) {
    (void)u;
    if (g_m_n < MT_LOG_MAX) g_m_evs[g_m_n++] = *ev;
}
static void mt_reset(void) { g_m_n = 0; }

/* ------------------------------------------------------------------
 * Synthetic descriptor: Digitizer, 2 fingers, per finger:
 *   Tip Switch  (1 bit)
 *   pad         (7 bits)
 *   Contact ID  (8 bit)
 *   X           (16 bit, logical 0..4095)
 *   Y           (16 bit, logical 0..4095)
 * plus trailing Contact Count (8 bit).
 * No Report ID.
 * ------------------------------------------------------------------ */
static const uint8_t k_synth_desc[] = {
    0x05, 0x0D,             /* Usage Page (Digitizer) */
    0x09, 0x04,             /* Usage (Touch Screen) — we skip top collection tag */
    0xA1, 0x01,             /* Collection (Application) */
    /* --- finger 0 --- */
    0x09, 0x22,             /* Usage (Finger) */
    0xA1, 0x02,             /* Collection (Logical) */
    0x09, 0x42,             /* Usage (Tip Switch) */
    0x15, 0x00,             /* Logical Min 0 */
    0x25, 0x01,             /* Logical Max 1 */
    0x75, 0x01,             /* Report Size 1 */
    0x95, 0x01,             /* Report Count 1 */
    0x81, 0x02,             /* Input */
    0x75, 0x07,             /* Report Size 7 (pad) */
    0x95, 0x01,             /* Report Count 1 */
    0x81, 0x03,             /* Input const */
    0x09, 0x51,             /* Usage (Contact ID) */
    0x75, 0x08,             /* Report Size 8 */
    0x95, 0x01,
    0x25, 0x7F,             /* Logical Max 127 */
    0x81, 0x02,
    0x05, 0x01,             /* Usage Page (Generic Desktop) */
    0x09, 0x30,             /* Usage X */
    0x26, 0xFF, 0x0F,       /* Logical Max 4095 */
    0x75, 0x10,             /* Report Size 16 */
    0x95, 0x01,
    0x81, 0x02,
    0x09, 0x31,             /* Usage Y */
    0x81, 0x02,
    0xC0,                   /* End Collection (finger 0) */
    /* --- finger 1 (same layout) --- */
    0x05, 0x0D,
    0x09, 0x22,
    0xA1, 0x02,
    0x09, 0x42,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x01,
    0x81, 0x02,
    0x75, 0x07,
    0x95, 0x01,
    0x81, 0x03,
    0x09, 0x51,
    0x75, 0x08,
    0x95, 0x01,
    0x25, 0x7F,
    0x81, 0x02,
    0x05, 0x01,
    0x09, 0x30,
    0x26, 0xFF, 0x0F,
    0x75, 0x10,
    0x95, 0x01,
    0x81, 0x02,
    0x09, 0x31,
    0x81, 0x02,
    0xC0,                   /* End Collection (finger 1) */
    /* --- Contact Count --- */
    0x05, 0x0D,
    0x09, 0x54,
    0x25, 0x02,
    0x75, 0x08,
    0x95, 0x01,
    0x81, 0x02,
    0xC0,                   /* End Collection (application) */
};

static int test_hid_parser(void) {
    hid_touch_layout_t lay;
    if (!hid_parse_report_descriptor(k_synth_desc, sizeof(k_synth_desc), &lay)) {
        mt_log("[mt-selftest] parse_report_descriptor returned false\n");
        return -1;
    }
    if (lay.slot_count != 2) {
        mt_log("[mt-selftest] slot_count != 2\n");
        return -1;
    }
    /* finger 0: tip @0, cid @8, x @16, y @32; total 48 bits so far */
    if (lay.slots[0].tip.bit_offset != 0 || lay.slots[0].tip.bit_size != 1) return -1;
    if (lay.slots[0].contact_id.bit_offset != 8) return -1;
    if (lay.slots[0].x.bit_offset != 16 || lay.slots[0].x.bit_size != 16) return -1;
    if (lay.slots[0].y.bit_offset != 32 || lay.slots[0].y.bit_size != 16) return -1;
    if (lay.slots[0].x.logical_max != 4095) return -1;
    /* finger 1 starts at bit 48 */
    if (lay.slots[1].tip.bit_offset != 48) return -1;
    if (lay.slots[1].x.bit_offset != 64) return -1;
    if (lay.slots[1].y.bit_offset != 80) return -1;
    /* contact_count at bit 96, byte 12; total 13 bytes */
    if (lay.contact_count.bit_offset != 96) return -1;
    if (lay.report_bytes != 13) return -1;
    return 0;
}

static int test_hid_report_parse(void) {
    hid_touch_layout_t lay;
    (void)hid_parse_report_descriptor(k_synth_desc, sizeof(k_synth_desc), &lay);

    /* Build a report: f0 tip=1 cid=7 x=100 y=200 ; f1 tip=1 cid=8 x=300 y=400 ; count=2 */
    uint8_t rep[13];
    for (int i = 0; i < 13; i++) rep[i] = 0;
    rep[0]  = 1;                  /* tip 0 */
    rep[1]  = 7;                  /* cid 0 */
    rep[2]  = 100 & 0xFF; rep[3] = (100 >> 8) & 0xFF;
    rep[4]  = 200 & 0xFF; rep[5] = (200 >> 8) & 0xFF;
    rep[6]  = 1;                  /* tip 1 */
    rep[7]  = 8;
    rep[8]  = 300 & 0xFF; rep[9]  = (300 >> 8) & 0xFF;
    rep[10] = 400 & 0xFF; rep[11] = (400 >> 8) & 0xFF;
    rep[12] = 2;

    hid_touch_sample_t s;
    if (!hid_touch_report_parse(&lay, rep, sizeof(rep), &s)) return -1;
    if (s.slot_count != 2 || s.contact_count != 2) return -1;
    if (!s.fingers[0].tip || s.fingers[0].contact_id != 7) return -1;
    if (s.fingers[0].x != 100 || s.fingers[0].y != 200) return -1;
    if (!s.fingers[1].tip || s.fingers[1].contact_id != 8) return -1;
    if (s.fingers[1].x != 300 || s.fingers[1].y != 400) return -1;
    return 0;
}

static int test_multi_pinch_open(void) {
    gesture_multi_init(640, 480);
    gesture_multi_set_listener(mt_listener, NULL);
    gesture_multi_reset();
    mt_reset();

    gesture_multi_slot_t f[2];
    f[0].present = 1; f[0].tip = 1; f[0].x = 100; f[0].y = 240;
    f[1].present = 1; f[1].tip = 1; f[1].x = 200; f[1].y = 240;
    gesture_multi_feed(f, 2);              /* pinch begin, dist=100 */

    f[0].x = 50;  f[1].x = 250;
    gesture_multi_feed(f, 2);              /* pinch update, dist=200 => scale=2000 */

    if (g_m_n < 2) return -1;
    if (g_m_evs[0].type != GESTURE_MULTI_PINCH_BEGIN) return -1;
    if (g_m_evs[1].type != GESTURE_MULTI_PINCH_UPDATE) return -1;
    if (g_m_evs[1].scale_x1000 < 1900 || g_m_evs[1].scale_x1000 > 2100) return -1;
    return 0;
}

static int test_multi_pinch_close(void) {
    gesture_multi_reset();
    mt_reset();

    gesture_multi_slot_t f[2];
    f[0].present = 1; f[0].tip = 1; f[0].x = 100; f[0].y = 240;
    f[1].present = 1; f[1].tip = 1; f[1].x = 300; f[1].y = 240;
    gesture_multi_feed(f, 2);              /* begin dist=200 */

    f[0].x = 150; f[1].x = 250;
    gesture_multi_feed(f, 2);              /* update dist=100 => scale ~500 */
    if (g_m_n < 2) return -1;
    int32_t s = g_m_evs[1].scale_x1000;
    if (s < 450 || s > 550) return -1;
    return 0;
}

static int test_multi_rotate(void) {
    gesture_multi_reset();
    mt_reset();

    gesture_multi_slot_t f[2];
    /* horizontal alignment: angle ~0 */
    f[0].present = 1; f[0].tip = 1; f[0].x = 100; f[0].y = 200;
    f[1].present = 1; f[1].tip = 1; f[1].x = 200; f[1].y = 200;
    gesture_multi_feed(f, 2);              /* begin */

    /* rotate 90 deg → vertical alignment */
    f[0].x = 150; f[0].y = 250;
    f[1].x = 150; f[1].y = 150;
    gesture_multi_feed(f, 2);              /* update, delta angle ~ -90 (dy=-100) */

    if (g_m_n < 2) return -1;
    int32_t d = g_m_evs[g_m_n-1].delta_angle_deg;
    if (d < 0) d = -d;
    if (d < 80 || d > 100) return -1;
    return 0;
}

static int test_multi_release(void) {
    gesture_multi_reset();
    mt_reset();

    gesture_multi_slot_t f[2];
    f[0].present = 1; f[0].tip = 1; f[0].x = 100; f[0].y = 240;
    f[1].present = 1; f[1].tip = 1; f[1].x = 200; f[1].y = 240;
    gesture_multi_feed(f, 2);

    f[0].tip = 0;
    gesture_multi_feed(f, 2);              /* one finger up → pinch end */

    if (g_m_n < 2) return -1;
    if (g_m_evs[g_m_n-1].type != GESTURE_MULTI_PINCH_END) return -1;
    return 0;
}

/* M8-C.4 Two-finger scroll:
 *   Two fingers ~100 px apart, both drag downwards by 24 px per step.
 *   Distance & angle stay ~constant, so scale_abs<80 & angle_abs<=5 hold.
 *   Every 24 px accumulator produces one negative wheel tick (natural-scroll). */
static int test_multi_scroll(void) {
    gesture_multi_reset();
    mt_reset();

    gesture_multi_slot_t f[2];
    f[0].present = 1; f[0].tip = 1; f[0].x = 100; f[0].y = 200;
    f[1].present = 1; f[1].tip = 1; f[1].x = 200; f[1].y = 200;
    gesture_multi_feed(f, 2);              /* begin */

    /* Drag both fingers down by 24 px (center dy = +24) */
    f[0].y = 224; f[1].y = 224;
    gesture_multi_feed(f, 2);

    /* Expect a SCROLL_UPDATE with wheel_ticks == -1 (natural-scroll) */
    int have_scroll = 0;
    int32_t ticks = 0;
    for (int i = 0; i < g_m_n; i++) {
        if (g_m_evs[i].type == GESTURE_MULTI_SCROLL_UPDATE) {
            have_scroll = 1;
            ticks = g_m_evs[i].wheel_ticks;
        }
    }
    if (!have_scroll) return -1;
    if (ticks != -1) return -1;

    /* Second step: drag both up by 48 px (dy = -48) → wheel_ticks = +2 */
    f[0].y = 176; f[1].y = 176;
    gesture_multi_feed(f, 2);
    int last_scroll_ticks = 0;
    int found_second = 0;
    for (int i = 0; i < g_m_n; i++) {
        if (g_m_evs[i].type == GESTURE_MULTI_SCROLL_UPDATE) {
            last_scroll_ticks = g_m_evs[i].wheel_ticks;
            found_second = 1;
        }
    }
    if (!found_second) return -1;
    if (last_scroll_ticks != 2) return -1;

    /* Diagnostic accessor should mirror last event's ticks */
    if (gesture_multi_last_wheel_ticks() != 2) return -1;
    return 0;
}

/* Ensure a small two-finger drift (below NOTCH threshold) does NOT emit
 * a wheel tick but does produce a SCROLL_UPDATE with ticks==0. */
static int test_multi_scroll_below_notch(void) {
    gesture_multi_reset();
    mt_reset();

    gesture_multi_slot_t f[2];
    f[0].present = 1; f[0].tip = 1; f[0].x = 100; f[0].y = 200;
    f[1].present = 1; f[1].tip = 1; f[1].x = 200; f[1].y = 200;
    gesture_multi_feed(f, 2);              /* begin */

    f[0].y = 210; f[1].y = 210;            /* dy=+10, < NOTCH(24) */
    gesture_multi_feed(f, 2);

    int found = 0;
    for (int i = 0; i < g_m_n; i++) {
        if (g_m_evs[i].type == GESTURE_MULTI_SCROLL_UPDATE) {
            if (g_m_evs[i].wheel_ticks != 0) return -1;
            if (g_m_evs[i].scroll_dy != 10) return -1;
            found = 1;
        }
    }
    if (!found) return -1;
    return 0;
}

/* ------------------------------------------------------------------
 * M8-A.1 selftest: hid_type_infer 分类逻辑
 * ------------------------------------------------------------------ */
static int test_hid_type_infer(void) {
    /* 1. boot protocol 优先：proto=1 → KBD，proto=2 → MOUSE（即使 VID 是触屏）*/
    if (hid_type_infer(0x27C6, 0x0101, 1, 0, 0, 0) != HID_INFER_BOOT_KEYBD) return -1;
    if (hid_type_infer(0x046D, 0xC077, 2, 0, 0, 0) != HID_INFER_BOOT_MOUSE) return -1;

    /* 2. 不支持的 proto → UNKNOWN */
    if (hid_type_infer(0x0000, 0x0000, 3, 0, 0, 0) != HID_INFER_UNKNOWN) return -1;

    /* 3. proto=0 + 已知触屏 VID 白名单 → TOUCHSCREEN */
    if (hid_type_infer(0x27C6, 0x1234, 0, 0, 0, 0) != HID_INFER_TOUCHSCREEN) return -1; /* Goodix */
    if (hid_type_infer(0x2AE0, 0x0001, 0, 0, 0, 0) != HID_INFER_TOUCHSCREEN) return -1; /* Focaltech */
    if (hid_type_infer(0x222A, 0x0001, 0, 0, 0, 0) != HID_INFER_TOUCHSCREEN) return -1; /* ILITEK */

    /* 4. QEMU tablet：默认 TABLET；force_touch_test=1 → TOUCHSCREEN */
    if (hid_type_infer(0x0627, 0x0001, 0, 0, 0, 0) != HID_INFER_TABLET)      return -1;
    if (hid_type_infer(0x0627, 0x0001, 0, 0, 0, 1) != HID_INFER_TOUCHSCREEN) return -1;

    /* 5. proto=0 + 未知 VID + 无 desc → 兜底归为 TOUCHSCREEN */
    if (hid_type_infer(0xDEAD, 0xBEEF, 0, 0, 0, 0) != HID_INFER_TOUCHSCREEN) return -1;

    /* 6. Report Descriptor 探测：Usage Page (Digitizer) + Usage (Touch Screen) → TOUCHSCREEN */
    static const uint8_t desc_touch[] = {
        0x05, 0x0D,       /* Usage Page (Digitizer) */
        0x09, 0x04,       /* Usage (Touch Screen) */
        0xA1, 0x01,       /* Collection (Application) */
        0xC0              /* End Collection */
    };
    /* 即使 VID=QEMU tablet，desc 优先级更高，应得 TOUCHSCREEN */
    if (hid_type_infer(0x0627, 0x0001, 0, desc_touch, sizeof(desc_touch), 0)
        != HID_INFER_TOUCHSCREEN) return -1;

    /* 7. Report Descriptor 探测：Usage Page (Digitizer) + Usage (Pen) → TABLET */
    static const uint8_t desc_pen[] = {
        0x05, 0x0D,       /* Usage Page (Digitizer) */
        0x09, 0x02,       /* Usage (Pen) */
        0xA1, 0x01,
        0xC0
    };
    if (hid_type_infer(0x27C6, 0x9999, 0, desc_pen, sizeof(desc_pen), 0)
        != HID_INFER_TABLET) return -1;

    /* 8. hid_desc_scan_digitizer: 非 Digitizer Page → UNKNOWN */
    static const uint8_t desc_kbd[] = {
        0x05, 0x01,       /* Usage Page (Generic Desktop) */
        0x09, 0x06,       /* Usage (Keyboard) */
        0xA1, 0x01, 0xC0
    };
    if (hid_desc_scan_digitizer(desc_kbd, sizeof(desc_kbd)) != HID_INFER_UNKNOWN) return -1;

    /* 9. hid_vid_is_known_touch 直接验证 */
    if (!hid_vid_is_known_touch(0x27C6)) return -1;
    if ( hid_vid_is_known_touch(0x0000)) return -1;

    return 0;
}

int arch_x86_64_multitouch_selftest_run(void) {
    int rc = 0;
    if (test_hid_parser()      != 0) { mt_log("[mt-selftest] FAIL: hid_parser\n");    rc = -1; }
    if (test_hid_report_parse()!= 0) { mt_log("[mt-selftest] FAIL: report_parse\n"); rc = -1; }
    if (test_hid_type_infer()  != 0) { mt_log("[mt-selftest] FAIL: type_infer\n");   rc = -1; }
    if (test_multi_pinch_open()!= 0) { mt_log("[mt-selftest] FAIL: pinch_open\n");   rc = -1; }
    if (test_multi_pinch_close()!=0) { mt_log("[mt-selftest] FAIL: pinch_close\n");  rc = -1; }
    if (test_multi_rotate()    != 0) { mt_log("[mt-selftest] FAIL: rotate\n");       rc = -1; }
    if (test_multi_release()   != 0) { mt_log("[mt-selftest] FAIL: release\n");      rc = -1; }
    if (test_multi_scroll()    != 0) { mt_log("[mt-selftest] FAIL: scroll\n");       rc = -1; }
    if (test_multi_scroll_below_notch() != 0) { mt_log("[mt-selftest] FAIL: scroll_below_notch\n"); rc = -1; }
    if (rc == 0) mt_log("[x86_64][mt-selftest] PASS\n");
    return rc;
}

