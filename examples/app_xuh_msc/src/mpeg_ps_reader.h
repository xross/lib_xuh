#ifndef APP_MPEG_PS_READER_H_
#define APP_MPEG_PS_READER_H_

#ifdef __xcore__
#define PS_CALLBACK(name) __attribute__((fptrgroup("mpeg_ps_" #name)))
#else
#define PS_CALLBACK(name)
#endif
#define PS_READ_CALLBACK PS_CALLBACK(read)
#define PS_AUDIO_CALLBACK PS_CALLBACK(audio)
typedef int (*ps_read_fn)(void *, unsigned char *, unsigned, unsigned *);
typedef int (*ps_audio_fn)(void *, const unsigned char *, unsigned);

typedef struct {
    unsigned char cache[4096] __attribute__((aligned(4)));
    unsigned position, length, remaining, stream, ended, error, started;
    unsigned video_bytes, audio_bytes;
    PS_READ_CALLBACK ps_read_fn read;
    void *context;
} mpeg_ps_reader_t;

/* Sequential MPEG-1 PS only: first video (E0) and MP2 audio (C0). Other PES
 * streams are skipped. Packet payloads stream through one cache, with no
 * packet-sized allocations. Audio callbacks consume their span before return.
 * read callback: 0 on success (zero bytes = EOF), nonzero on I/O failure.
 * read_video: 1 = video bytes, 0 = clean EOF, -1 = read/format/truncation error. */
void mpeg_ps_init(mpeg_ps_reader_t *reader, PS_READ_CALLBACK ps_read_fn read, void *context);
int mpeg_ps_read_video(mpeg_ps_reader_t *reader, unsigned char *output,
                       unsigned capacity, unsigned *bytes,
                       PS_AUDIO_CALLBACK ps_audio_fn audio, void *audio_context);
#endif
