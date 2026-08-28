#include "bmp_writer.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define BMP_FILE_HEADER_SIZE 14u
#define BMP_INFO_HEADER_SIZE 40u
#define BMP_HEADER_SIZE      (BMP_FILE_HEADER_SIZE + BMP_INFO_HEADER_SIZE)
#define BMP_MAX_ROW_BYTES    (400u * 3u)

static void store_u16_le(uint8_t* dst, uint16_t value)
{
    dst[0] = value & 0xFF;
    dst[1] = (value >> 8) & 0xFF;
}

static void store_u32_le(uint8_t* dst, uint32_t value)
{
    dst[0] = value & 0xFF;
    dst[1] = (value >> 8) & 0xFF;
    dst[2] = (value >> 16) & 0xFF;
    dst[3] = (value >> 24) & 0xFF;
}

int bmp_write_rgb888(const char* filename, const uint8_t* rgb, uint16_t width, uint16_t height)
{
    uint32_t row_bytes = (uint32_t)width * 3u;
    uint32_t row_stride = (row_bytes + 3u) & ~3u;
    uint32_t pixel_bytes = row_stride * height;
    uint32_t file_size = BMP_HEADER_SIZE + pixel_bytes;
    uint8_t header[BMP_HEADER_SIZE] = {0};
    uint8_t row[BMP_MAX_ROW_BYTES + 3u] = {0};

    if (filename == NULL || rgb == NULL || width == 0 || height == 0 || row_bytes > BMP_MAX_ROW_BYTES) {
        return 0;
    }

    FILE* f = fopen(filename, "wb");
    if (f == NULL) {
        return 0;
    }

    header[0] = 'B';
    header[1] = 'M';
    store_u32_le(&header[2], file_size);
    store_u32_le(&header[10], BMP_HEADER_SIZE);
    store_u32_le(&header[14], BMP_INFO_HEADER_SIZE);
    store_u32_le(&header[18], width);
    store_u32_le(&header[22], height);
    store_u16_le(&header[26], 1);
    store_u16_le(&header[28], 24);
    store_u32_le(&header[34], pixel_bytes);
    store_u32_le(&header[38], 2835);
    store_u32_le(&header[42], 2835);

    if (fwrite(header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        return 0;
    }

    for (int src_row = height - 1; src_row >= 0; src_row--) {
        const uint8_t* src = rgb + ((size_t)src_row * width * 3u);

        for (uint16_t col = 0; col < width; col++) {
            row[col * 3u + 0u] = src[col * 3u + 2u];
            row[col * 3u + 1u] = src[col * 3u + 1u];
            row[col * 3u + 2u] = src[col * 3u + 0u];
        }
        for (uint32_t pad = row_bytes; pad < row_stride; pad++) {
            row[pad] = 0;
        }

        if (fwrite(row, 1, row_stride, f) != row_stride) {
            fclose(f);
            return 0;
        }
    }

    fclose(f);
    return 1;
}
