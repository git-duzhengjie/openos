#include "image.h"
#include "heap.h"
#include "string.h"

#define BMP_FILE_HEADER_SIZE 14u
#define BMP_INFO_HEADER_MIN_SIZE 40u
#define BMP_COMPRESSION_RGB 0u

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t rd32s(const uint8_t *p) {
    return (int32_t)rd32(p);
}

static uint32_t abs_i32_to_u32(int32_t value) {
    if (value < 0) return (uint32_t)(-value);
    return (uint32_t)value;
}

static int mul_overflow_u32(uint32_t a, uint32_t b, uint32_t *out) {
    if (a != 0 && b > 0xFFFFFFFFu / a) return 1;
    *out = a * b;
    return 0;
}

static uint32_t bmp_stride(uint32_t width, uint16_t bpp) {
    uint32_t bits;
    if (mul_overflow_u32(width, (uint32_t)bpp, &bits)) return 0;
    return ((bits + 31u) / 32u) * 4u;
}

static uint32_t bmp_palette_color(const uint8_t *palette, uint32_t entries, uint32_t index) {
    const uint8_t *c;
    if (!palette || index >= entries) return 0xFFFF00FFu;
    c = palette + index * 4u;
    return 0xFF000000u | ((uint32_t)c[2] << 16) | ((uint32_t)c[1] << 8) | (uint32_t)c[0];
}

static image_status_t bmp_read_info(const uint8_t *data, uint32_t size, image_info_t *info,
                                    int32_t *raw_height, uint32_t *palette_entries) {
    uint32_t dib_size;
    int32_t width_s;
    int32_t height_s;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t data_offset;
    uint32_t colors_used;
    uint32_t width;
    uint32_t height;
    uint32_t row_stride;
    uint32_t needed_rows;
    uint32_t needed_size;

    if (!data || size < BMP_FILE_HEADER_SIZE + BMP_INFO_HEADER_MIN_SIZE) return IMAGE_ERR_INVALID;
    if (data[0] != 'B' || data[1] != 'M') return IMAGE_ERR_INVALID;

    data_offset = rd32(data + 10);
    dib_size = rd32(data + 14);
    if (dib_size < BMP_INFO_HEADER_MIN_SIZE) return IMAGE_ERR_UNSUPPORTED;
    if (14u + dib_size > size) return IMAGE_ERR_INVALID;

    width_s = rd32s(data + 18);
    height_s = rd32s(data + 22);
    planes = rd16(data + 26);
    bpp = rd16(data + 28);
    compression = rd32(data + 30);
    colors_used = rd32(data + 46);

    if (width_s <= 0 || height_s == 0) return IMAGE_ERR_INVALID;
    if (planes != 1 || compression != BMP_COMPRESSION_RGB) return IMAGE_ERR_UNSUPPORTED;
    if (!(bpp == 1 || bpp == 4 || bpp == 8 || bpp == 24 || bpp == 32)) return IMAGE_ERR_UNSUPPORTED;

    width = (uint32_t)width_s;
    height = abs_i32_to_u32(height_s);
    if (width == 0 || height == 0) return IMAGE_ERR_INVALID;

    row_stride = bmp_stride(width, bpp);
    if (row_stride == 0) return IMAGE_ERR_INVALID;
    if (mul_overflow_u32(row_stride, height, &needed_rows)) return IMAGE_ERR_INVALID;
    needed_size = data_offset + needed_rows;
    if (needed_size < data_offset || needed_size > size) return IMAGE_ERR_INVALID;

    if (bpp <= 8) {
        uint32_t max_entries = 1u << bpp;
        uint32_t palette_offset = BMP_FILE_HEADER_SIZE + dib_size;
        if (colors_used == 0 || colors_used > max_entries) colors_used = max_entries;
        if (palette_offset + colors_used * 4u > data_offset) return IMAGE_ERR_INVALID;
        if (palette_entries) *palette_entries = colors_used;
    } else if (palette_entries) {
        *palette_entries = 0;
    }

    if (info) {
        info->format = IMAGE_FORMAT_BMP;
        info->width = width;
        info->height = height;
        info->bits_per_pixel = bpp;
        info->data_offset = data_offset;
        info->image_size = needed_rows;
    }
    if (raw_height) *raw_height = height_s;
    return IMAGE_OK;
}

image_status_t image_probe(const uint8_t *data, uint32_t size, image_info_t *info) {
    return bmp_read_info(data, size, info, NULL, NULL);
}

static uint32_t bmp_get_indexed_pixel(const uint8_t *row, uint32_t x, uint16_t bpp) {
    if (bpp == 8) return row[x];
    if (bpp == 4) {
        uint8_t v = row[x / 2u];
        return (x & 1u) ? (uint32_t)(v & 0x0Fu) : (uint32_t)(v >> 4);
    }
    return (row[x / 8u] >> (7u - (x & 7u))) & 1u;
}

image_status_t image_decode_bmp_into(const uint8_t *data, uint32_t size,
                                     uint32_t *pixels, uint32_t width,
                                     uint32_t height, uint32_t pitch) {
    image_info_t info;
    int32_t raw_height;
    uint32_t palette_entries = 0;
    image_status_t st;
    uint32_t row_stride;
    const uint8_t *pixel_data;
    const uint8_t *palette;
    uint32_t y;

    if (!pixels || pitch < width) return IMAGE_ERR_INVALID;
    st = bmp_read_info(data, size, &info, &raw_height, &palette_entries);
    if (st != IMAGE_OK) return st;
    if (width != info.width || height != info.height) return IMAGE_ERR_BUFFER_TOO_SMALL;

    row_stride = bmp_stride(info.width, info.bits_per_pixel);
    pixel_data = data + info.data_offset;
    palette = (info.bits_per_pixel <= 8) ? (data + BMP_FILE_HEADER_SIZE + rd32(data + 14)) : NULL;

    for (y = 0; y < info.height; y++) {
        uint32_t src_y = (raw_height < 0) ? y : (info.height - 1u - y);
        const uint8_t *row = pixel_data + src_y * row_stride;
        uint32_t *dst = pixels + y * pitch;
        uint32_t x;
        for (x = 0; x < info.width; x++) {
            if (info.bits_per_pixel == 32) {
                const uint8_t *p = row + x * 4u;
                uint32_t alpha = p[3] ? (uint32_t)p[3] : 0xFFu;
                dst[x] = (alpha << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[0];
            } else if (info.bits_per_pixel == 24) {
                const uint8_t *p = row + x * 3u;
                dst[x] = 0xFF000000u | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[0];
            } else {
                uint32_t idx = bmp_get_indexed_pixel(row, x, info.bits_per_pixel);
                dst[x] = bmp_palette_color(palette, palette_entries, idx);
            }
        }
    }
    return IMAGE_OK;
}

image_status_t image_decode_bmp(const uint8_t *data, uint32_t size, image_bitmap_t *out) {
    image_info_t info;
    image_status_t st;
    uint32_t pixel_count;
    uint32_t byte_count;
    uint32_t *pixels;

    if (!out) return IMAGE_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    st = image_probe(data, size, &info);
    if (st != IMAGE_OK) return st;
    if (mul_overflow_u32(info.width, info.height, &pixel_count)) return IMAGE_ERR_INVALID;
    if (mul_overflow_u32(pixel_count, sizeof(uint32_t), &byte_count)) return IMAGE_ERR_INVALID;
    pixels = (uint32_t *)kmalloc(byte_count);
    if (!pixels) return IMAGE_ERR_NO_MEMORY;
    st = image_decode_bmp_into(data, size, pixels, info.width, info.height, info.width);
    if (st != IMAGE_OK) {
        kfree(pixels);
        return st;
    }
    out->width = info.width;
    out->height = info.height;
    out->pitch = info.width;
    out->pixels = pixels;
    return IMAGE_OK;
}

void image_free(image_bitmap_t *bitmap) {
    if (!bitmap) return;
    if (bitmap->pixels) kfree(bitmap->pixels);
    memset(bitmap, 0, sizeof(*bitmap));
}

int image_selftest(void) {
    static const uint8_t bmp_2x2_24[] = {
        'B','M', 70,0,0,0, 0,0,0,0, 54,0,0,0,
        40,0,0,0, 2,0,0,0, 2,0,0,0, 1,0, 24,0,
        0,0,0,0, 16,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0,
        255,0,0, 255,255,255, 0,0,
        0,0,255, 0,255,0, 0,0
    };
    uint32_t pixels[4];
    image_info_t info;
    if (image_probe(bmp_2x2_24, sizeof(bmp_2x2_24), &info) != IMAGE_OK) return 0;
    if (info.width != 2 || info.height != 2 || info.bits_per_pixel != 24) return 0;
    if (image_decode_bmp_into(bmp_2x2_24, sizeof(bmp_2x2_24), pixels, 2, 2, 2) != IMAGE_OK) return 0;
    if (pixels[0] != 0xFFFF0000u) return 0;
    if (pixels[1] != 0xFF00FF00u) return 0;
    if (pixels[2] != 0xFF0000FFu) return 0;
    if (pixels[3] != 0xFFFFFFFFu) return 0;
    return 1;
}
