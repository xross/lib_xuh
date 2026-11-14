#pragma once

#include <stdint.h>

int bmp_write_rgb888(const char* filename, const uint8_t* rgb, uint16_t width, uint16_t height);
