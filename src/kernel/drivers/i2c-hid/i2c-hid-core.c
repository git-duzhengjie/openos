/*
 * M8-D.4 I2C HID Touchscreen Driver - Core Implementation
 * HID over I2C Protocol Specification v1.0
 *
 * Copyright (c) 2024 OpenOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "i2c-hid.h"
#include <kernel/include/input_core.h>
#include <kernel/include/klog64.h>
#include <kernel/include/delay64.h>
#include <string.h>

#define LOG_TAG "i2c-hid"

/* Static device instance for default touchscreen */
static struct i2c_hid_device default_ts = {
    .name = "I2C HID Touchscreen",
    .i2c_addr = 0x00,
    .irq = 0,
    .powered = false,
    .suspended = false,
    .poll_interval_us = 8000, /* 125 Hz */
    .error_count = 0,
    .phy_x_min = 0,
    .phy_x_max = 0x7FFF, /* 16-bit default */
    .phy_y_min = 0,
    .phy_y_max = 0x7FFF,
    .scr_width = 1920,   /* Default 1080p */
    .scr_height = 1080,
};

/* ACPI probe - stub implementation */
int i2c_hid_acpi_get_descriptor(const char *hid, struct i2c_hid_desc *desc)
{
    /*
     * TODO: Real implementation needs:
     * 1. Enumerate ACPI namespace for matching _HID
     * 2. Call _DSM method to retrieve HID descriptor
     * 3. Parse the returned buffer into i2c_hid_desc
     *
     * For now, return -ENODEV to indicate stub
     */
    (void)hid;
    (void)desc;
    return -1; /* -ENODEV */
}

/* I2C read/write stubs - to be implemented by I2C controller driver */
static int i2c_hid_i2c_read_stub(struct i2c_hid_device *dev,
                                 uint16_t reg, uint8_t *buf, size_t len)
{
    (void)dev;
    (void)reg;
    (void)buf;
    (void)len;
    return -1; /* -ENODEV */
}

static int i2c_hid_i2c_write_stub(struct i2c_hid_device *dev,
                                  uint16_t reg, const uint8_t *buf, size_t len)
{
    (void)dev;
    (void)reg;
    (void)buf;
    (void)len;
    return -1; /* -ENODEV */
}

/* Scale physical coordinates to screen pixels */
void i2c_hid_scale_coordinates(struct i2c_hid_device *dev,
                               struct i2c_hid_contact *contact)
{
    uint32_t phy_range_x = dev->phy_x_max - dev->phy_x_min;
    uint32_t phy_range_y = dev->phy_y_max - dev->phy_y_min;

    if (phy_range_x == 0 || phy_range_y == 0)
        return;

    /* Linear scaling: (val - min) * screen / range */
    contact->x = ((contact->x - dev->phy_x_min) * dev->scr_width) / phy_range_x;
    contact->y = ((contact->y - dev->phy_y_min) * dev->scr_height) / phy_range_y;

    /* Clamp to screen bounds */
    if (contact->x >= dev->scr_width)
        contact->x = dev->scr_width - 1;
    if (contact->y >= dev->scr_height)
        contact->y = dev->scr_height - 1;
}

/* Parse HID input report (simplified for standard touch format) */
int i2c_hid_parse_report(struct i2c_hid_device *dev,
                         const uint8_t *raw, size_t len,
                         struct i2c_hid_report *report)
{
    (void)dev;

    if (len < 2)
        return -1;

    memset(report, 0, sizeof(*report));
    report->report_id = raw[0];

    /*
     * Standard I2C HID touch report format (simplified):
     * Byte 0: Report ID
     * Byte 1: Contact count (bits 0-3) | buttons (bits 4-7)
     * Then for each contact (6 bytes each):
     *   Byte n+0: Contact ID | Tip Switch | In Range
     *   Byte n+1: X low byte
     *   Byte n+2: X high byte | Y high nibble
     *   Byte n+3: Y low byte
     *   Byte n+4: Pressure / Width
     *   Byte n+5: Height / misc
     */

    report->contact_count = raw[1] & 0x0F;
    if (report->contact_count > I2C_HID_MAX_CONTACTS)
        report->contact_count = I2C_HID_MAX_CONTACTS;

    /* Simplified parsing for demo - real implementation needs HID desc parser */
    for (uint8_t i = 0; i < report->contact_count && i < I2C_HID_MAX_CONTACTS; i++) {
        size_t offset = 2 + i * 6;
        if (offset + 5 >= len)
            break;

        struct i2c_hid_contact *c = &report->contacts[i];

        c->contact_id = (raw[offset] >> 4) & 0x0F;
        c->tip_switch = (raw[offset] & 0x01) != 0;

        /* 16-bit coordinates */
        c->x = raw[offset + 1] | ((uint16_t)(raw[offset + 2] & 0x0F) << 8);
        c->y = raw[offset + 3] | ((uint16_t)(raw[offset + 2] >> 4) << 8);

        /* Pressure (8-bit) */
        c->pressure = raw[offset + 4];

        /* Contact size */
        c->width = (raw[offset + 4] >> 4) & 0x0F;
        c->height = raw[offset + 5] & 0x0F;
    }

    return 0;
}

/* Track contact lifecycle and inject input events */
int i2c_hid_inject_input(struct i2c_hid_device *dev,
                          const struct i2c_hid_report *report)
{
    for (uint8_t i = 0; i < report->contact_count && i < I2C_HID_MAX_CONTACTS; i++) {
        const struct i2c_hid_contact *c = &report->contacts[i];
        struct i2c_hid_tracking *t = &dev->tracking[c->contact_id];
        uint64_t now = 0; /* TODO: get current timestamp */

        if (c->tip_switch) {
            /* Finger down or moving */
            struct input_event evt;
            evt.type = INPUT_EV_ABS;

            if (!t->active) {
                /* New contact - report down */
                evt.code = INPUT_ABS_DOWN;
                evt.value = 1;
                input_report(&dev->input, &evt);
                t->active = true;
            }

            /* Scale coordinates */
            struct i2c_hid_contact scaled = *c;
            i2c_hid_scale_coordinates(dev, &scaled);

            /* Report X */
            evt.code = INPUT_ABS_X;
            evt.value = scaled.x;
            input_report(&dev->input, &evt);

            /* Report Y */
            evt.code = INPUT_ABS_Y;
            evt.value = scaled.y;
            input_report(&dev->input, &evt);

            /* Report pressure if available */
            if (c->pressure > 0) {
                evt.code = INPUT_ABS_PRESSURE;
                evt.value = c->pressure;
                input_report(&dev->input, &evt);
            }

            t->last_x = scaled.x;
            t->last_y = scaled.y;
            t->last_timestamp = now;
        } else if (t->active) {
            /* Finger lifted - report up */
            struct input_event evt;
            evt.type = INPUT_EV_ABS;
            evt.code = INPUT_ABS_DOWN;
            evt.value = 0;
            input_report(&dev->input, &evt);

            t->active = false;
        }
    }

    /* Sync event */
    struct input_event sync_evt;
    sync_evt.type = INPUT_EV_SYN;
    sync_evt.code = INPUT_SYN_REPORT;
    sync_evt.value = 0;
    input_report(&dev->input, &sync_evt);

    return 0;
}

/* Poll device for input report */
int i2c_hid_poll(struct i2c_hid_device *dev)
{
    uint8_t report_buf[256];
    struct i2c_hid_report report;
    int ret;

    if (!dev->powered)
        return -1;

    /* Read input report from device */
    ret = dev->i2c_read(dev, dev->desc.wInputRegister,
                        report_buf, sizeof(report_buf));
    if (ret < 0) {
        dev->error_count++;
        klog(LOG_TAG, "I2C read failed, error_count=%u\n", dev->error_count);
        return ret;
    }

    /* Parse report */
    ret = i2c_hid_parse_report(dev, report_buf, ret, &report);
    if (ret < 0) {
        klog(LOG_TAG, "Report parse failed\n");
        return ret;
    }

    /* Inject input events */
    return i2c_hid_inject_input(dev, &report);
}

/* Probe and initialize device */
int i2c_hid_probe(struct i2c_hid_device *dev)
{
    int ret;

    klog(LOG_TAG, "Probing I2C HID device %s\n", dev->name);

    /* Try to get descriptor from ACPI */
    ret = i2c_hid_acpi_get_descriptor(I2C_HID_HID_STANDARD, &dev->desc);
    if (ret < 0) {
        /* Try vendor specific HIDs */
        ret = i2c_hid_acpi_get_descriptor(I2C_HID_HID_SURFACE, &dev->desc);
    }
    if (ret < 0) {
        ret = i2c_hid_acpi_get_descriptor(I2C_HID_HID_SYNAPTICS, &dev->desc);
    }

    if (ret < 0) {
        klog(LOG_TAG, "No I2C HID ACPI device found (stub)\n");
        /* Continue with defaults for simulation */
    }

    /* Setup I2C ops - stubs for now */
    dev->i2c_read = i2c_hid_i2c_read_stub;
    dev->i2c_write = i2c_hid_i2c_write_stub;

    /* Initialize tracking state */
    memset(dev->tracking, 0, sizeof(dev->tracking));

    /* Register input device */
    dev->input.name = dev->name;
    dev->input.type = INPUT_DEV_TOUCHSCREEN;
    input_register_device(&dev->input);

    dev->powered = true;
    klog(LOG_TAG, "I2C HID touchscreen initialized\n");

    return 0;
}

/* Check if I2C HID hardware is present */
bool i2c_hid_present(void)
{
    struct i2c_hid_desc desc;
    int ret;

    /* Try standard HID first */
    ret = i2c_hid_acpi_get_descriptor(I2C_HID_HID_STANDARD, &desc);
    if (ret == 0)
        return true;

    /* Try Surface */
    ret = i2c_hid_acpi_get_descriptor(I2C_HID_HID_SURFACE, &desc);
    if (ret == 0)
        return true;

    /* Try Synaptics */
    ret = i2c_hid_acpi_get_descriptor(I2C_HID_HID_SYNAPTICS, &desc);
    if (ret == 0)
        return true;

    return false;
}

/* Driver initialization */
int i2c_hid_init(void)
{
    klog(LOG_TAG, "I2C HID Touchscreen Driver v1.0\n");
    return i2c_hid_probe(&default_ts);
}

/* Driver cleanup */
void i2c_hid_exit(void)
{
    if (default_ts.powered) {
        input_unregister_device(&default_ts.input);
        default_ts.powered = false;
    }
    klog(LOG_TAG, "I2C HID driver unloaded\n");
}
