#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mjpeg_reader.h"

static BYTE stream[16000], frame[16000];
static unsigned length, offset;
static int fail_read;

FRESULT f_read(FIL *file, void *dst, UINT size, UINT *read)
{
    (void)file;
    *read = 0;
    if (fail_read) return FR_DISK_ERR;
    *read = size < length - offset ? size : length - offset;
    memcpy(dst, stream + offset, *read);
    offset += *read;
    return FR_OK;
}

static void byte(unsigned value) { stream[length++] = value; }

static void make_frame(unsigned segment_size)
{
    byte(0xff); byte(0xd8);
    byte(0xff); byte(0xe0); byte(segment_size >> 8); byte(segment_size);
    /* EOI inside a length-delimited segment must not terminate the frame. */
    for (unsigned i = 2; i < segment_size; ++i) byte(i & 1 ? 0xd9 : 0xff);
    byte(0xff); byte(0xda); byte(0); byte(2);
    byte(0x12); byte(0xff); byte(0); byte(0xd9); /* stuffed entropy byte */
    byte(0xff); byte(0xd0); /* restart marker */
    byte(0xff); byte(0xd9);
}

int main(void)
{
    mjpeg_reader_t reader = {0};
    UINT size;
    /* First frame ends at byte 4095: second SOI straddles the refill boundary. */
    make_frame(4079);
    unsigned first_length = length;
    assert(first_length == 4095);
    make_frame(19);
    unsigned second_length = length - first_length;
    assert(mjpeg_read_frame(&reader, frame, sizeof(frame), &size) == 1);
    assert(size == first_length && !memcmp(frame, stream, size));
    assert(mjpeg_read_frame(&reader, frame, sizeof(frame), &size) == 1);
    assert(size == second_length && !memcmp(frame, stream + first_length, size));
    assert(mjpeg_read_frame(&reader, frame, sizeof(frame), &size) == 0);

    for (unsigned cut = 1; cut < first_length; ++cut) {
        memset(&reader, 0, sizeof(reader)); offset = 0; length = cut;
        assert(mjpeg_read_frame(&reader, frame, sizeof(frame), &size) == -1);
    }
    length = first_length;
    for (unsigned cap = 0; cap < first_length; ++cap) {
        memset(&reader, 0, sizeof(reader)); offset = 0;
        memset(frame, 0xa5, sizeof(frame));
        assert(mjpeg_read_frame(&reader, frame, cap, &size) == -1);
        assert(size <= cap && frame[cap] == 0xa5);
    }
    memset(&reader, 0, sizeof(reader)); offset = 0;
    assert(mjpeg_read_frame(&reader, frame, first_length, &size) == 1);
    memset(&reader, 0, sizeof(reader)); offset = 0; fail_read = 1;
    assert(mjpeg_read_frame(&reader, frame, sizeof(frame), &size) == -1);
    assert(reader.error == FR_DISK_ERR);
    fail_read = 0;
    memset(&reader, 0, sizeof(reader)); offset = 0;
    stream[0] = 0;
    assert(mjpeg_read_frame(&reader, frame, sizeof(frame), &size) == -1);
    puts("PASS: MJPEG boundaries, segment markers, stuffing, restarts, truncation, capacity, disk errors");
}
