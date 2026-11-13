#include "activity_led.h"

unsigned activity_led_next_word(unsigned previous, unsigned flags)
{
    /* Separate counters retain short operations AND completed frames, even if
     * the next decode has already started before the LED task samples them. */
    unsigned frames = (previous & ACTIVITY_LED_FRAME_MASK) +
                      ((flags & ACTIVITY_LED_FRAME) ? 0x01000000u : 0);
    return (frames & ACTIVITY_LED_FRAME_MASK) | ((previous + 4u) & 0x00fffffcu) |
           (flags & ACTIVITY_LED_BUSY) |
           ((previous | flags) & ACTIVITY_LED_ERROR);
}

static unsigned brightness(unsigned level)
{
    /* Approximate gamma 2, with 8-bit PWM and no floating point. */
    return (level * level + 255u) >> 8;
}

void activity_led_effect_step(activity_led_effect_t *effect,
                              const unsigned words[2], unsigned duty[2])
{
    unsigned triangle = effect->phase < 300 ? effect->phase : 599 - effect->phase;
    unsigned idle = 24 + triangle * 32 / 300;
    unsigned frame_complete = (words[1] ^ effect->previous[1]) & ACTIVITY_LED_FRAME_MASK;
    effect->phase = (effect->phase + 1) % 600; /* Three-second breath. */
    for (unsigned i = 0; i < 2; ++i) {
        if ((words[i] & ACTIVITY_LED_BUSY) || words[i] != effect->previous[i])
            effect->level[i] += (256 - effect->level[i]) / 2; /* Fast attack. */
        else
            effect->level[i] = effect->level[i] > 8 ? effect->level[i] - 8 : 0;
        effect->previous[i] = words[i];
        effect->failed |= words[i] & ACTIVITY_LED_ERROR;
    }
    unsigned green = effect->level[0] > idle ? effect->level[0] : idle;
    duty[0] = brightness(green);
    /* Keep decode activity as a subdued red undertone. Completed pictures get
     * an unmistakable red accent instead of disappearing into bright green. */
    duty[1] = brightness(effect->level[1]) / 4;
    if (effect->frame_cooldown) --effect->frame_cooldown;
    if (frame_complete && !effect->frame_cooldown) {
        effect->frame_pulse = 20; /* 40 ms full red, then a 60-ms crossfade. */
        effect->frame_cooldown = 50; /* At most four accents/sec, not an FPS strobe. */
    }
    if (effect->frame_pulse) {
        unsigned pulse = --effect->frame_pulse;
        unsigned red = pulse >= 12 ? 255 : pulse * 255 / 12;
        duty[0] = duty[0] * (255 - red) / 255;
        if (red > duty[1]) duty[1] = red;
    }
    if (effect->failed) {
        /* Two 80-ms red flashes, 80 ms apart, repeated once a second. */
        duty[0] = 0;
        duty[1] = effect->error_phase < 16 ||
                  (effect->error_phase >= 32 && effect->error_phase < 48) ? 255 : 0;
        effect->error_phase = (effect->error_phase + 1) % 200;
    }
}

unsigned activity_led_pwm_edges(const unsigned duty[2], activity_led_edge_t edges[3])
{
    unsigned green = duty[0] * ACTIVITY_LED_PERIOD_TICKS / 256;
    unsigned red = duty[1] * ACTIVITY_LED_PERIOD_TICKS / 256;
    unsigned count = 1;
    edges[0] = (activity_led_edge_t){0, (green ? 1u : 0u) | (red ? 2u : 0u)};
    if (green && red && green != red)
        edges[count++] = green < red ? (activity_led_edge_t){green, 2} :
                                      (activity_led_edge_t){red, 1};
    if (green || red)
        edges[count++] = (activity_led_edge_t){green > red ? green : red, 0};
    return count;
}

#if APP_XUH_MSC_ACTIVITY_LED
#ifndef __XS3A__
#error The activity LED task requires XS3 tile-local memory and reference time
#endif
#include <stdint.h>
#include <xs1.h>
#include <xcore/hwtimer.h>
#include <xcore/port.h>

#ifndef APP_XUH_MSC_LED_ACTIVE_LOW
#define APP_XUH_MSC_LED_ACTIVE_LOW 0
#endif

/* XS3 tile-local, single-writer mailboxes. Every shared access uses one aligned
 * LDW/STW with a compiler memory barrier: the reader may see an old or new word,
 * never a partial word. No payload is published, no lock/channel rendezvous is
 * needed, and cosmetic updates may coalesce. Do not use this for buffer ownership.
 * XTC 15.3.1's __atomic builtins instead call out to runtime helpers. */
static unsigned usb_word __attribute__((aligned(4)));
static unsigned decode_word __attribute__((aligned(4)));

static unsigned sample_word(const unsigned *word)
{
    unsigned value;
    __asm__ volatile("ldw %0, %1[0]" : "=r"(value) : "r"(word) : "memory");
    return value;
}

static void publish_word(unsigned *word, unsigned flags)
{
    unsigned value = activity_led_next_word(sample_word(word), flags);
    __asm__ volatile("stw %0, %1[0]" : : "r"(value), "r"(word) : "memory");
}

void activity_led_usb(unsigned flags) { publish_word(&usb_word, flags); }
void activity_led_decode(unsigned flags) { publish_word(&decode_word, flags); }

void activity_led_task(void)
{
    const port_t port = XS1_PORT_32A; /* Tile 1: bit 0 green, bit 1 red. */
    const unsigned invert = APP_XUH_MSC_LED_ACTIVE_LOW ? 3u : 0u;
    activity_led_effect_t effect = {{0, 0}, {0, 0}, 0, 0, 0, 0, 0};
    activity_led_edge_t edges[3];
    unsigned duty[2] = {0}, count = 1, frame = 0;
    hwtimer_t timer = hwtimer_alloc();
    if (!timer) __builtin_trap();
    port_enable(port);
    port_out(port, invert);
    unsigned start = get_reference_time() + ACTIVITY_LED_PERIOD_TICKS;
    for (;;) {
        if (!frame) {
            unsigned words[2] = {sample_word(&usb_word), sample_word(&decode_word)};
            activity_led_effect_step(&effect, words, duty);
            count = activity_led_pwm_edges(duty, edges);
        }
        for (unsigned i = 0; i < count; ++i) {
            hwtimer_wait_until(timer, start + edges[i].ticks);
            port_out(port, edges[i].value ^ invert);
        }
        start += ACTIVITY_LED_PERIOD_TICKS;
        frame = (frame + 1) % ACTIVITY_LED_UPDATE_MS;
        /* Keep absolute deadlines normally; skip missed periods after a debug
         * halt rather than emitting a burst of stale PWM transitions. */
        unsigned now = get_reference_time();
        if ((int32_t)(now - start) > 0) start = now + ACTIVITY_LED_PERIOD_TICKS;
    }
}
#endif
