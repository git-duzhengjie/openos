#ifndef OPENOS_ARCH_AARCH64_GT911_H
#define OPENOS_ARCH_AARCH64_GT911_H

#include <stdint.h>
#include "aarch64_i2c_bus.h"

/*
 * Goodix GT911 capacitive touch controller driver (M11-D.2).
 *
 * Register map (subset):
 *   0x8140  Product ID (4 bytes ASCII "911\0")
 *   0x814E  Status: bit7=buffer_ready, bit3:0=touch_count
 *   0x814F  Touch #0: [track_id, x_lo, x_hi, y_lo, y_hi, size_lo, size_hi, reserved]
 *   +8      Touch #1 ... up to 5
 *
 * The controller latches; driver must write 0 back to 0x814E after read.
 */

#define AARCH64_GT911_MAX_TOUCHES  5u
#define AARCH64_GT911_DEFAULT_ADDR 0x5du

typedef struct aarch64_gt911_touch {
    uint8_t  track_id;
    uint16_t x;
    uint16_t y;
    uint16_t size;
    uint8_t  active;
} aarch64_gt911_touch_t;

typedef struct aarch64_gt911_frame {
    uint32_t              touch_count;
    aarch64_gt911_touch_t points[AARCH64_GT911_MAX_TOUCHES];
} aarch64_gt911_frame_t;

typedef struct aarch64_gt911_device {
    aarch64_i2c_bus_t *bus;
    uint16_t           addr;
    uint8_t            probed;
    char               product_id[8];
    uint32_t           poll_count;
    uint32_t           report_count;
    uint32_t           error_count;
} aarch64_gt911_device_t;

int  aarch64_gt911_init(aarch64_gt911_device_t *dev,
                        aarch64_i2c_bus_t *bus,
                        uint16_t addr);

int  aarch64_gt911_probe(aarch64_gt911_device_t *dev);

int  aarch64_gt911_poll(aarch64_gt911_device_t *dev,
                        aarch64_gt911_frame_t *frame);

/* Post a GT911 frame into the M8-E input_core event bus.
 * Returns number of events posted. */
int  aarch64_gt911_post_events(const aarch64_gt911_frame_t *frame);

/* --- Test hook: preload stub GT911 register bank ------------------------ */
void aarch64_gt911_stub_preload(aarch64_i2c_stub_dev_t *dev);
void aarch64_gt911_stub_inject_frame(aarch64_i2c_stub_dev_t *dev,
                                     const aarch64_gt911_frame_t *frame);

#endif /* OPENOS_ARCH_AARCH64_GT911_H */
