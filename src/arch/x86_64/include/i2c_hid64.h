/*
 * M8-D I2C HID Touchscreen Driver
 * HID over I2C Protocol Specification v1.0
 *
 * Copyright (c) 2024 OpenOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef I2C_HID64_H
#define I2C_HID64_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Standard HID over I2C constants */
#define I2C_HID_HID_DESC_REG       0x0001
#define I2C_HID_REPORT_DESC_REG    0x0002
#define I2C_HID_INPUT_REG          0x0003
#define I2C_HID_OUTPUT_REG         0x0004
#define I2C_HID_CMD_REG            0x0005
#define I2C_HID_DATA_REG           0x0006

#define I2C_HID_CMD_POWER_ON       0x00
#define I2C_HID_CMD_POWER_SLEEP    0x01
#define I2C_HID_CMD_RESET          0x02

/* Maximum supported contacts (M8-C multitouch) */
#define I2C_HID_MAX_CONTACTS       10

/* Standard vendor/product IDs for common devices */
#define I2C_HID_VID_MICROSOFT      0x045E
#define I2C_HID_PID_SURFACE_GO     0x001F
#define I2C_HID_VID_SYNAPTICS      0x06CB
#define I2C_HID_VID_ELAN           0x04F3
#define I2C_HID_VID_GOODIX         0x27C6

/* ACPI _HID patterns (M8-D.4 real device detection) */
#define I2C_HID_ACPI_HID_STANDARD  "PNP0C50"
#define I2C_HID_ACPI_HID_MS        "MSHW0001"
#define I2C_HID_ACPI_HID_SYNAPTICS "SYNA2B2C"
#define I2C_HID_ACPI_HID_ELAN      "ELAN0001"
#define I2C_HID_ACPI_HID_GOODIX    "GDIX1001"

/* I2C HID Device Descriptor (ACPI _DSM) */
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
    uint8_t  reserved[1];
} __attribute__((packed));

/* Single touch contact data structure (M8-C multitouch) */
struct i2c_hid_contact {
    uint8_t  contact_id;
    bool     tip_switch;
    uint16_t x;
    uint16_t y;
    uint16_t pressure;
    uint16_t contact_width;
    uint16_t contact_height;
};

/* Parsed input report (M8-C report parsing) */
struct i2c_hid_report {
    uint8_t  report_id;
    uint8_t  contact_count;
    struct i2c_hid_contact contacts[I2C_HID_MAX_CONTACTS];
};

/* Contact tracking state machine (M8-B gesture engine input) */
struct i2c_hid_tracking {
    bool     active;
    uint8_t  last_id;
    uint16_t last_x;
    uint16_t last_y;
    uint64_t last_timestamp_ms;
};

/* I2C bus operations abstraction (M8-D.3 I2C controller driver) */
struct i2c_hid_ops {
    int (*read_reg16)(uint16_t slave_addr, uint16_t reg, uint8_t *buf, size_t len);
    int (*write_reg16)(uint16_t slave_addr, uint16_t reg, const uint8_t *buf, size_t len);
};

/* I2C HID Device Instance */
struct i2c_hid_device {
    const char *name;
    uint16_t    i2c_slave_addr;
    uint16_t    irq_num;
    bool        present;
    bool        powered;

    /* Descriptors */
    struct i2c_hid_desc    dev_desc;
    uint8_t               *report_desc;
    size_t                  report_desc_len;

    /* Hardware capabilities */
    uint32_t    phy_x_min;
    uint32_t    phy_x_max;
    uint32_t    phy_y_min;
    uint32_t    phy_y_max;
    uint32_t    screen_w;
    uint32_t    screen_h;
    uint32_t    max_pressure;

    /* Runtime state */
    struct i2c_hid_tracking tracking[I2C_HID_MAX_CONTACTS];
    uint64_t    poll_count;
    uint64_t    report_count;
    uint64_t    error_count;

    /* I2C bus binding */
    const struct i2c_hid_ops *ops;

    /* Gesture engine binding (M8-B) */
    void (*report_ready_cb)(const struct i2c_hid_report *report, void *user_data);
    void *gesture_user_data;
};

/* Public API */
int  i2c_hid_init(struct i2c_hid_device *dev);
void i2c_hid_destroy(struct i2c_hid_device *dev);
int  i2c_hid_probe(struct i2c_hid_device *dev);
int  i2c_hid_poll(struct i2c_hid_device *dev);
int  i2c_hid_parse_report(struct i2c_hid_device *dev, const uint8_t *raw, size_t len, struct i2c_hid_report *report);
void i2c_hid_scale_coordinates(struct i2c_hid_device *dev, struct i2c_hid_contact *contact);
int  i2c_hid_inject_input(const struct i2c_hid_report *report);
bool i2c_hid_present(void);
void i2c_hid_register_gesture_callback(struct i2c_hid_device *dev, void (*cb)(const struct i2c_hid_report *, void *), void *user_data);

/* I2C operation wrappers with error handling */
static inline int i2c_hid_read_reg(struct i2c_hid_device *dev, uint16_t reg, uint8_t *buf, size_t len) {
    if (!dev || !dev->ops || !dev->ops->read_reg16) return -1;
    return dev->ops->read_reg16(dev->i2c_slave_addr, reg, buf, len);
}

static inline int i2c_hid_write_reg(struct i2c_hid_device *dev, uint16_t reg, const uint8_t *buf, size_t len) {
    if (!dev || !dev->ops || !dev->ops->write_reg16) return -1;
    return dev->ops->write_reg16(dev->i2c_slave_addr, reg, buf, len);
}

/* Register selftest flag (M8-D selftest module) */
#define I2C_HID_SELFTEST_PLACEHOLDER 0x12345678

#endif /* I2C_HID64_H */
