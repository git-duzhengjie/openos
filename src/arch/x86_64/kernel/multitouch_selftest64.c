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

int arch_x86_64_multitouch_selftest_run(void) {
    int rc = 0;
    if (test_hid_parser()      != 0) { mt_log("[mt-selftest] FAIL: hid_parser\n");    rc = -1; }
    if (test_hid_report_parse()!= 0) { mt_log("[mt-selftest] FAIL: report_parse\n"); rc = -1; }
    if (test_multi_pinch_open()!= 0) { mt_log("[mt-selftest] FAIL: pinch_open\n");   rc = -1; }
    if (test_multi_pinch_close()!=0) { mt_log("[mt-selftest] FAIL: pinch_close\n");  rc = -1; }
    if (test_multi_rotate()    != 0) { mt_log("[mt-selftest] FAIL: rotate\n");       rc = -1; }
    if (test_multi_release()   != 0) { mt_log("[mt-selftest] FAIL: release\n");      rc = -1; }
    if (rc == 0) mt_log("[x86_64][mt-selftest] PASS\n");
    return rc;
}

