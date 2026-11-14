#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mpeg_ps_reader.h"

static const unsigned char fixture[] = {
    0,0,1,0xba, 0x21,0,1,0,1,0,0,1,
    0,0,1,0xe0, 0,5, 0x0f, 1,2,3,4,
    0,0,1,0xc0, 0,5, 0x0f, 11,12,13,14,
    0,0,1,0xe0, 0,3, 0x0f, 5,6,
    0,0,1,0xb9
};
static unsigned char input[sizeof(fixture)], video[32], audio[32];
static unsigned position, size, fragment, audio_size, fail_read, fail_audio;

static int read_data(void *context, unsigned char *data, unsigned capacity, unsigned *bytes)
{
    (void)context;
    *bytes = size - position;
    if (*bytes > fragment) *bytes = fragment;
    if (*bytes > capacity) *bytes = capacity;
    memcpy(data, input + position, *bytes);
    position += *bytes;
    return fail_read ? -1 : 0;
}
static int audio_data(void *context, const unsigned char *data, unsigned bytes)
{
    (void)context;
    assert(audio_size + bytes <= sizeof(audio));
    memcpy(audio + audio_size, data, bytes);
    audio_size += bytes;
    return fail_audio ? -1 : 0;
}
static int play(unsigned capacity)
{
    mpeg_ps_reader_t reader;
    mpeg_ps_init(&reader, read_data, NULL);
    unsigned total = 0, bytes;
    position = audio_size = 0;
    int result;
    while ((result = mpeg_ps_read_video(&reader, video + total, capacity, &bytes, audio_data, NULL)) > 0) {
        total += bytes;
        assert(total <= sizeof(video));
    }
    if (result == 0) {
        assert(total == 6 && audio_size == 4);
        for (unsigned i = 0; i < total; ++i) assert(video[i] == i + 1);
        for (unsigned i = 0; i < audio_size; ++i) assert(audio[i] == i + 11);
    }
    return result;
}
int main(void)
{
    for (fragment = 1; fragment <= sizeof(fixture); ++fragment) {
        for (unsigned capacity = 1; capacity <= 8; ++capacity) {
            memcpy(input, fixture, sizeof(input)); size = sizeof(input);
            assert(play(capacity) == 0);
        }
    }
    fragment = 7;
    for (unsigned cut = 13; cut < 23; ++cut) {
        memcpy(input, fixture, sizeof(input)); size = cut;
        assert(play(4) == -1);
    }
    memcpy(input, fixture, sizeof(input)); size = sizeof(input); input[4] = 0x40;
    assert(play(4) == -1); /* Do not misparse an MPEG-2 pack header. */
    memcpy(input, fixture, sizeof(input)); input[18] = 0xff;
    assert(play(4) == -1); /* Stuffing cannot underflow a PES payload length. */
    memcpy(input, fixture, sizeof(input)); fail_read = 1;
    assert(play(4) == -1);
    fail_read = 0; fail_audio = 1;
    assert(play(4) == -1);
    puts("PASS: PS cache/PES boundaries, replay, truncation, malformed headers and I/O/sink errors");
    return 0;
}
