#include "mjpeg_reader.h"

static int next_byte(mjpeg_reader_t *r)
{
    if (r->position == r->available) {
        r->position = 0;
        r->error = f_read(&r->file, r->buffer, sizeof(r->buffer), &r->available);
        if (r->error || !r->available) return -1;
    }
    return r->buffer[r->position++];
}

static int append_byte(mjpeg_reader_t *r, BYTE *dst, UINT cap, UINT *size)
{
    if (*size == cap) return -1;
    int byte = next_byte(r);
    if (byte < 0) return -1;
    dst[(*size)++] = byte;
    return byte;
}

int mjpeg_read_frame(mjpeg_reader_t *r, BYTE *dst, UINT cap, UINT *size)
{
    *size = 0;
    if (cap < 2) return -1;
    int first = next_byte(r);
    if (first < 0) return r->error ? -1 : 0;
    /* Raw MJPEG consists of consecutive complete JPEG bitstreams. */
    if (first != 0xff || next_byte(r) != 0xd8) return -1;
    dst[(*size)++] = 0xff;
    dst[(*size)++] = 0xd8;
    int entropy = 0;
    while (1) {
        int byte = append_byte(r, dst, cap, size);
        if (byte < 0) return -1;
        if (byte != 0xff) {
            if (entropy) continue;
            return -1;
        }
        int marker;
        do { marker = append_byte(r, dst, cap, size); } while (marker == 0xff);
        if (marker < 0) return -1;
        if (entropy && (marker == 0 || (marker >= 0xd0 && marker <= 0xd7))) continue;
        if (marker == 0xd9) return 1;
        if (marker == 0 || marker == 0xd8 || marker == 1 ||
            (marker >= 0xd0 && marker <= 0xd7)) return -1;
        int high = append_byte(r, dst, cap, size);
        int low = append_byte(r, dst, cap, size);
        if (high < 0 || low < 0) return -1;
        unsigned length = ((unsigned)high << 8) | low;
        if (length < 2 || length - 2 > cap - *size) return -1;
        for (unsigned i = 2; i < length; ++i)
            if (append_byte(r, dst, cap, size) < 0) return -1;
        entropy = marker == 0xda;
    }
}
