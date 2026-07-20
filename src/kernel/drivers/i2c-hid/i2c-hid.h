/*
 * M8-D.4 I2C HID Touchscreen Driver
 * HID over I2C Protocol Specification v1.0
 *
 * Copyright (c) 2024 OpenOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef I2C_HID_H
#define I2C_HID_H

#include <kernel/include/input_core.h>
#include <stdint.h>
#include <stdbool.h>

/* HID over I2C standard constants */
#define I2C_HID_CMD_SET_POWER        0x01
#define I2C_HID_CMD_GET_REPORT_DESC  0x02
#define I2C_HID_CMD_SET_REPORT       0x03
#define I2C_HID_CMD_GET_REPORT       0x04
#define I2C_HID_CMD_GET_IDLE         0x05
#define I2C_HID_CMD_SET_IDLE         0x06
#define I2C_HID_CMD_GET_PROTOCOL     0x07
#define I2C_HID_CMD_SET_PROTOCOL     0x08

#define I2C_HID_POWER_ON             0x00
#define I2C_HID_POWER_SLEEP          0x01

/* Vendor/Product ID definitions */
#define I2C_HID_VID_MICROSOFT        0x045E
#define I2C_HID_VID_SYNAPTICS        0x06CB
#define I2C_HID_VID_ELAN             0x04F3
#define I2C_HID_VID_GOODIX           0x27C6

/* ACPI _HID strings for probe */
#define I2C_HID_HID_STANDARD         "PNP0C50"
#define I2C_HID_HID_SURFACE          "MSHW0001"
#define I2C_HID_HID_SYNAPTICS        "SYNA2B2C"
#define I2C_HID_HID_ELAN             "ELAN0001"
#define I2C_HID_HID_GOODIX           "GDIX1001"

/* Maximum supported contacts */
#define I2C_HID_MAX_CONTACTS         10

/* Report descriptor parsing */
#define HID_USAGE_PAGE_DIGITIZER     0x0D
#define HID_USAGE_TOUCHSCREEN        0x04
#define HID_USAGE_CONTACT_COUNT      0x54
#define HID_USAGE_CONTACT_ID         0x51
#define HID_USAGE_TIP_SWITCH         0x42
#define HID_USAGE_X                  0x30
#define HID_USAGE_Y                  0x31
#define HID_USAGE_PRESSURE           0x30
#define HID_USAGE_WIDTH              0x48
#define HID_USAGE_HEIGHT             0x49

/* I2C HID device descriptor from ACPI _DSM */
struct i2c_hid_desc {
    uint16_t wHIDDescLength;
    uint16_t bcdVersion;
    uint16_t wReportDescLength;
    uint16_t wReportDescRegister;
    uint16_t wInputRegister;
    uint16_t wMaxInputLength;
    uint16_t wOutputRegister;
    uint16_t wMaxOutputLength;
    uint16_t wCommandRegister;
    uint16_t wDataRegister;
    uint16_t wVendorID;
    uint16_t wProductID;
    uint16_t wVersionID;
    uint8_t  bCapabilities;
    uint8_t  bReserved;
} __attribute__((packed));

/* Single touch contact data */
struct i2c_hid_contact {
    uint8_t  contact_id;
    bool     tip_switch;
    uint16_t x;
    uint16_t y;
    uint16_t pressure;
    uint16_t width;
    uint16_t height;
};

/* Parsed input report */
struct i2c_hid_report {
    uint8_t  report_id;
    uint8_t  contact_count;
    struct i2c_hid_contact contacts[I2C_HID_MAX_CONTACTS];
};

/* Contact tracking state */
struct i2c_hid_tracking {
    bool     active;
    uint8_t  last_id;
    uint16_t last_x;
    uint16_t last_y;
    uint64_t last_timestamp;
};

/* I2C HID device instance */
struct i2c_hid_device {
    const char *name;
    uint16_t    i2c_addr;
    uint16_t    irq;

    /* Device descriptor from ACPI */
    struct i2c_hid_desc desc;

    /* Input report parsing */
    uint8_t    *report_desc;
    size_t      report_desc_len;

    /* Coordinate scaling */
    uint32_t    phy_x_min, phy_x_max;
    uint32_t    phy_y_min, phy_y_max;
    uint32_t    scr_width, scr_height;

    /* Contact tracking */
    struct i2c_hid_tracking tracking[I2C_HID_MAX_CONTACTS];

    /* Input device registration */
    struct input_dev input;

    /* Runtime state */
    bool        powered;
    bool        suspended;
    uint32_t    poll_interval_us;
    uint32_t    error_count;

    /* I2C bus ops (to be filled by I2C controller driver) */
    int (*i2c_read)(struct i2c_hid_device *dev, uint16_t reg,
                    uint8_t *buf, size_t len);
    int (*i2c_write)(struct i2c_hid_device *dev, uint16_t reg,
                     const uint8_t *buf, size_t len);
};

/* Public API */
int  i2c_hid_init(void);
void i2c_hid_exit(void);
bool i2c_hid_present(void);
int  i2c_hid_probe(struct i2c_hid_device *dev);
int  i2c_hid_poll(struct i2c_hid_device *dev);
int  i2c_hid_parse_report(struct i2c_hid_device *dev,
                          const uint8_t *raw, size_t len,
                          struct i2c_hid_report *report);
void i2c_hid_scale_coordinates(struct i2c_hid_device *dev,
                               struct i2c_hid_contact *contact);
int  i2c_hid_inject_input(struct i2c_hid_device *dev,
                          const struct i2c_hid_report *report);

/* Platform ops - implemented by architecture specific code */
int  i2c_hid_acpi_get_descriptor(const char *hid, struct i2c_hid_desc *desc);

#endif /* I2C_HID_H */
