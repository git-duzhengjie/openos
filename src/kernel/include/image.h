#ifndef IMAGE_H
#define IMAGE_H

#include "types.h"

typedef enum image_status {
    IMAGE_OK = 0,
    IMAGE_ERR_INVALID = -1,
    IMAGE_ERR_UNSUPPORTED = -2,
    IMAGE_ERR_NO_MEMORY = -3,
    IMAGE_ERR_BUFFER_TOO_SMALL = -4
} image_status_t;

typedef enum image_format {
    IMAGE_FORMAT_UNKNOWN = 0,
    IMAGE_FORMAT_BMP = 1
} image_format_t;

typedef struct image_bitmap {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t *pixels; /* 0xAARRGGBB */
} image_bitmap_t;

typedef struct image_info {
    image_format_t format;
    uint32_t width;
    uint32_t height;
    uint16_t bits_per_pixel;
    uint32_t data_offset;
    uint32_t image_size;
} image_info_t;

image_status_t image_probe(const uint8_t *data, uint32_t size, image_info_t *info);
image_status_t image_decode_bmp(const uint8_t *data, uint32_t size, image_bitmap_t *out);
image_status_t image_decode_bmp_into(const uint8_t *data, uint32_t size,
                                     uint32_t *pixels, uint32_t width,
                                     uint32_t height, uint32_t pitch);
void image_free(image_bitmap_t *bitmap);
int image_selftest(void);

#endif /* IMAGE_H */
