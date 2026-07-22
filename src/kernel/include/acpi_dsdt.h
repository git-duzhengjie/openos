/*
 * acpi_dsdt.h — ACPI DSDT (Differentiated System Description Table) parser.
 *
 * Goals (M8-D.5+ prerequisite for I²C HID autodetection):
 *   - Parse the ACPI DSDT table AML bytecode
 *   - Identify I²C controller devices (PNP0A03, PNP0A08, INT33C0, etc.)
 *   - Identify HID over I²C devices (PNP0C50)
 *   - Extract device resources (_CRS methods): I²C bus number, address, interrupt
 *   - Provide API to enumerate all found I²C HID devices
 *
 * AML parsing scope (minimal, targeted):
 *   - Scope/Device opcodes to build the namespace hierarchy
 *   - Name declarations to identify device nodes
 *   - _HID/_CID methods to identify hardware IDs
 *   - _CRS method execution to extract resource templates
 *   - Basic integer/string/buffer AML object types
 */
#ifndef OPENOS_KERNEL_ACPI_DSDT_H
#define OPENOS_KERNEL_ACPI_DSDT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Max devices we track */
#define ACPI_DSDT_MAX_I2C_CONTROLLERS  8
#define ACPI_DSDT_MAX_I2C_HID_DEVICES  16
#define ACPI_DSDT_MAX_NAME_LEN         16
#define ACPI_DSDT_MAX_PATH_LEN         64

/* I²C HID device info extracted from DSDT */
typedef struct {
    char     name[ACPI_DSDT_MAX_NAME_LEN];    /* Device name in ACPI namespace */
    char     path[ACPI_DSDT_MAX_PATH_LEN];    /* Full path to device */
    char     hid[ACPI_DSDT_MAX_NAME_LEN];     /* _HID value (e.g., "PNP0C50") */
    uint16_t i2c_bus_number;                  /* I²C bus (controller) number */
    uint16_t i2c_address;                     /* I²C slave address (7-bit) */
    uint16_t interrupt_gsi;                   /* GPE number or GSI for interrupt */
    uint8_t  interrupt_polarity;              /* 0=active high, 1=active low */
    uint8_t  interrupt_trigger;               /* 0=edge, 1=level */
    uint8_t  valid;                           /* 1 if this entry is valid */
} acpi_i2c_hid_device_t;

/* I²C controller info */
typedef struct {
    char     name[ACPI_DSDT_MAX_NAME_LEN];
    char     path[ACPI_DSDT_MAX_PATH_LEN];
    char     hid[ACPI_DSDT_MAX_NAME_LEN];
    uint16_t bus_number;
    uint8_t  valid;
} acpi_i2c_controller_t;

/* AML parser context */
typedef struct {
    const uint8_t *aml_start;
    const uint8_t *aml_end;
    const uint8_t *aml_ptr;
    uint32_t       error_count;
} acpi_aml_context_t;

/* Resource template parser result */
typedef struct {
    uint16_t i2c_bus_number;
    uint16_t i2c_address;
    uint16_t interrupt_gsi;
    uint8_t  interrupt_polarity;
    uint8_t  interrupt_trigger;
    uint8_t  found_i2c;
    uint8_t  found_interrupt;
} acpi_resource_result_t;

/* Initialize DSDT parser and enumerate devices */
int acpi_dsdt_init(void);

/* Get count of detected I²C HID devices */
uint32_t acpi_dsdt_i2c_hid_device_count(void);

/* Get I²C HID device info by index (0..count-1) */
const acpi_i2c_hid_device_t *acpi_dsdt_get_i2c_hid_device(uint32_t index);

/* Get count of detected I²C controllers */
uint32_t acpi_dsdt_i2c_controller_count(void);

/* Get I²C controller info by index */
const acpi_i2c_controller_t *acpi_dsdt_get_i2c_controller(uint32_t index);

/* Find an I²C HID device by HID string */
const acpi_i2c_hid_device_t *acpi_dsdt_find_i2c_hid_by_hid(const char *hid);

/* AML utility functions (exported for reuse) */
int acpi_aml_parse_name(acpi_aml_context_t *ctx, char *name_out, size_t max_len);
uint64_t acpi_aml_parse_integer(acpi_aml_context_t *ctx);
int acpi_aml_parse_string(acpi_aml_context_t *ctx, char *str_out, size_t max_len);
int acpi_aml_parse_buffer(acpi_aml_context_t *ctx, const uint8_t **buf_out,
                          size_t *buf_len);

/* Resource template parser */
int acpi_parse_resource_template(const uint8_t *template_data, size_t len,
                                  acpi_resource_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_KERNEL_ACPI_DSDT_H */
