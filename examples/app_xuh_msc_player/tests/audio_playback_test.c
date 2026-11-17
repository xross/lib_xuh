#include <assert.h>
#include <stdio.h>
#include "audio_playback.h"

static void continuous(void)
{
    unsigned rates[] = {32000, 44100, 48000};
    for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
        audio_playback_t p = {0};
        unsigned epoch = 0xffff0000u;
        assert(!audio_playback_frame(&p, epoch, 1152, rates[r]));
        for (unsigned i = 1; i <= 6000; ++i) {
            unsigned due = audio_playback_due(&p);
            unsigned expected = epoch + (unsigned)(((uint64_t)i * 1152 * AUDIO_REFERENCE_HZ +
                                                     rates[r] - 1) / rates[r]);
            assert(due == expected);
            audio_playback_advance(&p, due);
            assert(p.played == i * 1152);
            assert(!audio_playback_frame(&p, due, 1152, rates[r]));
        }
        assert(!p.underflows && !p.held && !p.dropped);
    }
}

static void starvation(void)
{
    audio_playback_t p = {0};
    /* No output clock before the first complete frame. */
    audio_playback_advance(&p, 1234);
    assert(!p.started);
    unsigned epoch = 0xffff0000u;
    assert(!audio_playback_frame(&p, epoch, 1152, 48000));
    /* 66 ms from startup: frame 2 is entirely late, frame 3 has a
     * 864-sample obsolete prefix and 288 usable samples (6 ms). */
    unsigned now = epoch + 6600000u;
    assert(audio_playback_frame(&p, now, 1152, 48000) == 1152);
    assert(p.underflows == 1 && p.gap && p.held == 1152);
    assert(audio_playback_frame(&p, now, 1152, 48000) == 864);
    assert(p.underflows == 1 && !p.gap && p.held == 2016 && p.dropped == 2016);
    assert(audio_playback_due(&p) == epoch + 7200000u);
    assert(!audio_playback_frame(&p, epoch + 7200000u, 1152, 48000));
    /* A new, independent 5-ms gap counts once more, not per missing block. */
    assert(audio_playback_frame(&p, epoch + 10100000u, 1152, 48000) == 240);
    assert(p.underflows == 2 && p.held == 2256 && p.dropped == p.held);
    /* A provisional tail with no subsequent media is normal EOF. Clock
     * advance alone never invents another underflow or held samples. */
    unsigned held = p.held;
    now = epoch + 10100000u;
    for (unsigned i = 0; i < 5000; ++i) {
        now += 1000000u;
        audio_playback_advance(&p, now);
    }
    assert(p.started && p.held == held && p.underflows == 2);
}

static void long_gap(void)
{
    audio_playback_t p = {0};
    unsigned now = 0xff000000u;
    assert(!audio_playback_frame(&p, now, 1152, 48000));
    /* More than one hardware-timer wrap, advanced by the starvation timer. */
    for (unsigned i = 0; i < 5000; ++i) {
        now += 1000000u;
        audio_playback_advance(&p, now);
    }
    assert(p.played == 2400000 && p.started);
    unsigned skipped = 0;
    while (p.decoded <= p.played) skipped += audio_playback_frame(&p, now, 1152, 48000);
    assert(p.underflows == 1 && skipped == 2400000 - 1152);
    assert(p.held == skipped && p.dropped == skipped && !p.gap);
}

int main(void)
{
    continuous();
    starvation();
    long_gap();
    puts("PASS: continuous audio clock, fractional rates, late prefixes, gap episodes, EOF and timer wrap");
    return 0;
}
