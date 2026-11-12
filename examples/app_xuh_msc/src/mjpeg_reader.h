#ifndef MJPEG_READER_H_
#define MJPEG_READER_H_
#include "ff.h"

typedef struct {
    FIL file;
    BYTE buffer[4096];
    UINT position, available;
    FRESULT error;
} mjpeg_reader_t;

/* 1: complete frame, 0: clean EOF, -1: disk/format/capacity error. */
int mjpeg_read_frame(mjpeg_reader_t *reader, BYTE *frame, UINT capacity, UINT *size);
#endif
