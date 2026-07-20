/*
 * M8-D I2C HID Touchscreen Driver - Selftest Module
 *
 * Copyright (c) 2024 OpenOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "i2c_hid64.h"
#include "kernel64.h"
#include "early_console64.h"
#include <stdbool.h>

/* Selftest result structure */
struct i2c_hid_selftest_result {
    const char *test_name;
    bool passed;
    int error_code;
    const char *message;
};

/* Test 1: Device detection and initialization */
static struct i2c_hid_selftest_result test_detection(void) {
    struct i2c_hid_selftest_result result = {
        .test_name = "Device Detection",
        .passed = false,
        .error_code = 0,
        .message = NULL
    };

    struct i2c_hid_device dev = {0};
    dev.name = "Selftest Device";

    /* Test init/probe */
    int ret = i2c_hid_init(&dev);
    if (ret != 0) {
        result.message = "Driver init failed";
        result.error_code = ret;
        return result;
    }

    /* Test presence detection API */
    bool present = i2c_hid_present();
    if (!present) {
        result.message = "i2c_hid_present() returned false";
        result.error_code = -2;
        i2c_hid_destroy(&dev);
        return result;
    }

    result.passed = true;
    result.message = "Probe and detection working correctly";
    i2c_hid_destroy(&dev);
    return result;
}

/* Test 2: HID report parsing (M8-C) */
static struct i2c_hid_selftest_result test_report_parsing(void) {
    struct i2c_hid_selftest_result result = {
        .test_name = "HID Report Parsing",
        .passed = false,
        .error_code = 0,
        .message = NULL
    };

    struct i2c_hid_device dev = {0};
    dev.phy_x_min = 0;
    dev.phy_x_max = 4095;
    dev.phy_y_min = 0;
    dev.phy_y_max = 4095;
    dev.screen_w = 1920;
    dev.screen_h = 1080;

    /* Standard single-touch report (1 contact at (1024, 512), pressure 128) */
    uint8_t test_report[] = {
        0x01, /* Report ID */
        0x01, /* 1 contact */
        0x11, /* Contact ID 1, Tip Switch on */
        0x00, 0x04, /* X = 1024 */
        0x00, 0x02, /* Y = 512 */
        0x80  /* Pressure = 128 */
    };

    struct i2c_hid_report parsed;
    int ret = i2c_hid_parse_report(&dev, test_report, sizeof(test_report), &parsed);
    if (ret != 0) {
        result.message = "Report parsing returned error";
        result.error_code = ret;
        return result;
    }

    /* Validate parsed fields */
    if (parsed.report_id != 0x01) {
        result.message = "Report ID mismatch";
        result.error_code = parsed.report_id;
        return result;
    }

    if (parsed.contact_count != 1) {
        result.message = "Contact count mismatch";
        result.error_code = parsed.contact_count;
        return result;
    }

    const struct i2c_hid_contact *contact = &parsed.contacts[0];
    if (contact->contact_id != 1 || !contact->tip_switch) {
        result.message = "Contact metadata mismatch";
        result.error_code = contact->contact_id;
        return result;
    }

    if (contact->x != 1024 || contact->y != 512) {
        result.message = "Coordinate parsing mismatch";
        result.error_code = (contact->x << 16) | contact->y;
        return result;
    }

    if (contact->pressure != 128) {
        result.message = "Pressure parsing mismatch";
        result.error_code = contact->pressure;
        return result;
    }

    result.passed = true;
    result.message = "Single-touch report parsing correct";
    return result;
}

/* Test 3: Coordinate scaling (M8-A) */
static struct i2c_hid_selftest_result test_coordinate_scaling(void) {
    struct i2c_hid_selftest_result result = {
        .test_name = "Coordinate Scaling",
        .passed = false,
        .error_code = 0,
        .message = NULL
    };

    struct i2c_hid_device dev = {0};
    dev.phy_x_min = 0;
    dev.phy_x_max = 4095;  /* 12-bit sensor */
    dev.phy_y_min = 0;
    dev.phy_y_max = 4095;
    dev.screen_w = 1920;   /* Full HD */
    dev.screen_h = 1080;

    /* Test center point */
    struct i2c_hid_contact contact = {
        .contact_id = 1,
        .tip_switch = true,
        .x = 2048,
        .y = 2048,
        .pressure = 200
    };

    i2c_hid_scale_coordinates(&dev, &contact);

    /* Should be approximately (960, 540) */
    if (contact.x < 950 || contact.x > 970 ||
        contact.y < 530 || contact.y > 550) {
        result.message = "Center point scaling failed";
        result.error_code = (contact.x << 16) | contact.y;
        return result;
    }

    /* Test corner points */
    contact.x = 0; contact.y = 0;
    i2c_hid_scale_coordinates(&dev, &contact);
    if (contact.x != 0 || contact.y != 0) {
        result.message = "Top-left corner scaling failed";
        result.error_code = (contact.x << 16) | contact.y;
        return result;
    }

    contact.x = 4095; contact.y = 4095;
    i2c_hid_scale_coordinates(&dev, &contact);
    if (contact.x != 1919 || contact.y != 1079) {
        result.message = "Bottom-right corner scaling failed";
        result.error_code = (contact.x << 16) | contact.y;
        return result;
    }

    result.passed = true;
    result.message = "Coordinate scaling working correctly";
    return result;
}

/* Test 4: Contact tracking and input injection (M8-B) */
static struct i2c_hid_selftest_result test_contact_tracking(void) {
    struct i2c_hid_selftest_result result = {
        .test_name = "Contact Tracking / Input Injection",
        .passed = false,
        .error_code = 0,
        .message = NULL
    };

    /* Test input injection API */
    struct i2c_hid_report test_report = {
        .report_id = 0x01,
        .contact_count = 1,
        .contacts = {{
            .contact_id = 1,
            .tip_switch = true,
            .x = 960,
            .y = 540,
            .pressure = 150
        }}
    };

    int ret = i2c_hid_inject_input(&test_report);
    if (ret != 0) {
        result.message = "Input injection failed";
        result.error_code = ret;
        return result;
    }

    /* Test no-contact case (finger up) */
    test_report.contact_count = 0;
    ret = i2c_hid_inject_input(&test_report);
    if (ret != 0) {
        result.message = "No-contact injection failed";
        result.error_code = ret;
        return result;
    }

    result.passed = true;
    result.message = "Contact tracking and injection working correctly";
    return result;
}

/* Test 5: Polling and driver lifecycle */
static struct i2c_hid_selftest_result test_driver_lifecycle(void) {
    struct i2c_hid_selftest_result result = {
        .test_name = "Driver Lifecycle",
        .passed = false,
        .error_code = 0,
        .message = NULL
    };

    struct i2c_hid_device dev = {0};
    dev.name = "Lifecycle Test Device";

    /* Test full init */
    int ret = i2c_hid_init(&dev);
    if (ret != 0) {
        result.message = "Driver init failed";
        result.error_code = ret;
        return result;
    }

    /* Test multiple poll calls */
    for (int i = 0; i < 10; i++) {
        ret = i2c_hid_poll(&dev);
        if (ret != 0) {
            result.message = "Poll failed during loop";
            result.error_code = ret;
            i2c_hid_destroy(&dev);
            return result;
        }
    }

    /* Verify statistics are incrementing */
    if (dev.poll_count < 10 || dev.report_count < 1) {
        result.message = "Driver statistics not updating";
        result.error_code = (dev.poll_count << 32) | dev.report_count;
        i2c_hid_destroy(&dev);
        return result;
    }

    /* Test destroy */
    i2c_hid_destroy(&dev);

    if (dev.present || dev.powered) {
        result.message = "Driver cleanup incomplete";
        result.error_code = -1;
        return result;
    }

    result.passed = true;
    result.message = "Full driver lifecycle working correctly";
    return result;
}

/*
 * i2c_hid_run_selftest - Main selftest entry point
 *
 * Runs all tests and returns 0 if all passed, non-zero on failure.
 * Prints detailed results to early console.
 */
int i2c_hid_run_selftest(void) {
    early_printf("\n");
    early_printf("========================================\n");
    early_printf("  M8-D I2C HID Touchscreen Selftest\n");
    early_printf("========================================\n");

    struct i2c_hid_selftest_result (*tests[])(void) = {
        test_detection,
        test_report_parsing,
        test_coordinate_scaling,
        test_contact_tracking,
        test_driver_lifecycle
    };

    const int num_tests = sizeof(tests) / sizeof(tests[0]);
    int passed = 0;
    int failed = 0;

    for (int i = 0; i < num_tests; i++) {
        struct i2c_hid_selftest_result result = tests[i]();

        if (result.passed) {
            early_printf("[PASS] Test %d: %s\n", i + 1, result.test_name);
            early_printf("       %s\n", result.message);
            passed++;
        } else {
            early_printf("[FAIL] Test %d: %s\n", i + 1, result.test_name);
            early_printf("       Error: %s (code: %d)\n", result.message, result.error_code);
            failed++;
        }
    }

    early_printf("----------------------------------------\n");
    early_printf("  Results: %d PASSED, %d FAILED\n", passed, failed);
    early_printf("========================================\n\n");

    /* Return 0 if all passed, otherwise number of failed tests */
    return failed;
}
