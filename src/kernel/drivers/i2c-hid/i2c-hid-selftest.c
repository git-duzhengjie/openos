/*
 * M8-D.4 I2C HID Touchscreen Driver - Self Tests
 *
 * Copyright (c) 2024 OpenOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "i2c-hid.h"
#include <kernel/include/klog64.h>
#include <kernel/include/acpi_selftest64.h>
#include <string.h>

#define LOG_TAG "i2c-hid-selftest"

/* Test result structure */
struct selftest_result {
    const char *name;
    bool passed;
    const char *error;
};

/* Test 1: Device presence detection */
static struct selftest_result test_presence(void)
{
    struct selftest_result result = {
        .name = "Device Presence Detection",
        .passed = false,
        .error = NULL
    };

    bool present = i2c_hid_present();

    /*
     * Note: In QEMU environment without real I2C HID hardware,
     * this will return false. The test is designed to SKIP
     * rather than FAIL when no hardware is detected.
     */
    if (present) {
        result.passed = true;
        klog(LOG_TAG, "I2C HID hardware detected\n");
    } else {
        /* Return SKIP status for selftest framework */
        result.error = "No I2C HID hardware detected (SKIP)";
        klog(LOG_TAG, "No I2C HID hardware detected, skipping hardware tests\n");
    }

    return result;
}

/* Test 2: Coordinate scaling logic */
static struct selftest_result test_coordinate_scaling(void)
{
    struct selftest_result result = {
        .name = "Coordinate Scaling Logic",
        .passed = true,
        .error = NULL
    };

    struct i2c_hid_device dev;
    struct i2c_hid_contact contact;

    /* Setup 1920x1080 screen with 16-bit coordinate range */
    dev.phy_x_min = 0;
    dev.phy_x_max = 0x7FFF;
    dev.phy_y_min = 0;
    dev.phy_y_max = 0x7FFF;
    dev.scr_width = 1920;
    dev.scr_height = 1080;

    /* Test center point */
    contact.x = 0x4000;
    contact.y = 0x4000;
    i2c_hid_scale_coordinates(&dev, &contact);

    /* Should be ~960, ~540 */
    if (contact.x < 950 || contact.x > 970 ||
        contact.y < 530 || contact.y > 550) {
        result.passed = false;
        result.error = "Center point scaling failed";
        klog(LOG_TAG, "Center scaling failed: got (%u, %u)\n",
             contact.x, contact.y);
        return result;
    }

    /* Test corner points */
    contact.x = 0;
    contact.y = 0;
    i2c_hid_scale_coordinates(&dev, &contact);
    if (contact.x != 0 || contact.y != 0) {
        result.passed = false;
        result.error = "Top-left corner scaling failed";
        return result;
    }

    contact.x = 0x7FFF;
    contact.y = 0x7FFF;
    i2c_hid_scale_coordinates(&dev, &contact);
    if (contact.x != 1919 || contact.y != 1079) {
        result.passed = false;
        result.error = "Bottom-right corner scaling failed";
        return result;
    }

    klog(LOG_TAG, "Coordinate scaling tests passed\n");
    return result;
}

/* Test 3: HID report parsing */
static struct selftest_result test_report_parsing(void)
{
    struct selftest_result result = {
        .name = "HID Report Parsing",
        .passed = true,
        .error = NULL
    };

    struct i2c_hid_device dev;
    struct i2c_hid_report report;

    /* Simulated single-touch report */
    uint8_t single_touch_report[] = {
        0x01,       /* Report ID */
        0x01,       /* 1 contact */
        0x11,       /* Contact ID 1, tip switch on */
        0x00, 0x40, /* X = 0x4000 */
        0x00, 0x40, /* Y = 0x4000 */
        0xFF,       /* Max pressure */
        0x05,       /* Contact size */
    };

    int ret = i2c_hid_parse_report(&dev, single_touch_report,
                                    sizeof(single_touch_report), &report);
    if (ret < 0) {
        result.passed = false;
        result.error = "Single touch report parse failed";
        return result;
    }

    if (report.report_id != 0x01 ||
        report.contact_count != 1 ||
        report.contacts[0].contact_id != 1 ||
        !report.contacts[0].tip_switch) {
        result.passed = false;
        result.error = "Single touch report fields incorrect";
        return result;
    }

    /* Test empty report (no contacts) */
    uint8_t no_touch_report[] = { 0x01, 0x00 };
    ret = i2c_hid_parse_report(&dev, no_touch_report,
                               sizeof(no_touch_report), &report);
    if (ret < 0) {
        result.passed = false;
        result.error = "No-touch report parse failed";
        return result;
    }

    if (report.contact_count != 0) {
        result.passed = false;
        result.error = "No-touch report should have 0 contacts";
        return result;
    }

    klog(LOG_TAG, "Report parsing tests passed\n");
    return result;
}

/* Test 4: Contact tracking state machine */
static struct selftest_result test_contact_tracking(void)
{
    struct selftest_result result = {
        .name = "Contact Tracking State Machine",
        .passed = true,
        .error = NULL
    };

    struct i2c_hid_device dev;
    struct i2c_hid_report report;

    /* Initialize tracking */
    memset(dev.tracking, 0, sizeof(dev.tracking));

    /* Simulate finger down */
    report.report_id = 0x01;
    report.contact_count = 1;
    report.contacts[0].contact_id = 0;
    report.contacts[0].tip_switch = true;
    report.contacts[0].x = 100;
    report.contacts[0].y = 100;
    report.contacts[0].pressure = 128;

    /* First inject should mark contact active */
    int ret = i2c_hid_inject_input(&dev, &report);
    if (ret < 0) {
        result.passed = false;
        result.error = "Finger down injection failed";
        return result;
    }

    if (!dev.tracking[0].active) {
        result.passed = false;
        result.error = "Tracking state not marked active on finger down";
        return result;
    }

    /* Simulate finger up */
    report.contacts[0].tip_switch = false;
    ret = i2c_hid_inject_input(&dev, &report);
    if (ret < 0) {
        result.passed = false;
        result.error = "Finger up injection failed";
        return result;
    }

    if (dev.tracking[0].active) {
        result.passed = false;
        result.error = "Tracking state not cleared on finger up";
        return result;
    }

    klog(LOG_TAG, "Contact tracking tests passed\n");
    return result;
}

/* Test 5: Driver initialization flow */
static struct selftest_result test_driver_init(void)
{
    struct selftest_result result = {
        .name = "Driver Initialization Flow",
        .passed = true,
        .error = NULL
    };

    /* Test init returns success (even with stub hardware) */
    int ret = i2c_hid_init();

    /* Init should succeed even without real hardware */
    if (ret < 0) {
        result.passed = false;
        result.error = "Driver initialization failed";
        return result;
    }

    /* Cleanup should also succeed */
    i2c_hid_exit();

    klog(LOG_TAG, "Driver init/exit flow tests passed\n");
    return result;
}

/* Main selftest entry point */
int i2c_hid_run_selftest(void)
{
    klog(LOG_TAG, "=== I2C HID Touchscreen Selftest Starting ===\n");

    struct selftest_result (*tests[])(void) = {
        test_presence,
        test_coordinate_scaling,
        test_report_parsing,
        test_contact_tracking,
        test_driver_init,
    };

    const int num_tests = sizeof(tests) / sizeof(tests[0]);
    int passed = 0;
    int skipped = 0;
    int failed = 0;

    for (int i = 0; i < num_tests; i++) {
        struct selftest_result result = tests[i]();

        if (result.error && strstr(result.error, "SKIP")) {
            klog(LOG_TAG, "[SKIP] %s: %s\n", result.name, result.error);
            skipped++;
        } else if (result.passed) {
            klog(LOG_TAG, "[PASS] %s\n", result.name);
            passed++;
        } else {
            klog(LOG_TAG, "[FAIL] %s: %s\n", result.name,
                 result.error ? result.error : "Unknown error");
            failed++;
        }
    }

    klog(LOG_TAG, "=== Selftest Complete: %d PASSED, %d SKIPPED, %d FAILED ===\n",
         passed, skipped, failed);

    /* Return FAIL only if actual test failures, not skips */
    return (failed > 0) ? -1 : 0;
}
