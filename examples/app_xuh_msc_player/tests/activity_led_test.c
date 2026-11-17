#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "activity_led.h"

static void test_mailbox(void)
{
    unsigned word = activity_led_next_word(0, ACTIVITY_LED_BUSY);
    assert(word == 5);
    word = activity_led_next_word(word, 0);
    assert(word == 8); /* A short operation remains visible after it ends. */
    word = activity_led_next_word(word, ACTIVITY_LED_ERROR);
    assert(word & ACTIVITY_LED_ERROR);
    word = activity_led_next_word(word, ACTIVITY_LED_BUSY);
    assert((word & 3) == (ACTIVITY_LED_ERROR | ACTIVITY_LED_BUSY));
    assert(activity_led_next_word(0x00fffffc, 0) == 0);
    assert(activity_led_next_word(UINT_MAX, ACTIVITY_LED_FRAME) == ACTIVITY_LED_ERROR);
    word = activity_led_next_word(0, ACTIVITY_LED_FRAME);
    assert((word & ACTIVITY_LED_FRAME_MASK) == 0x01000000u);
    word = activity_led_next_word(word, ACTIVITY_LED_BUSY);
    assert((word & ACTIVITY_LED_FRAME_MASK) == 0x01000000u);
}

static void test_effects(void)
{
    activity_led_effect_t effect = {0};
    unsigned words[2] = {0}, duty[2], idle[600];
    unsigned low = 255, high = 0;
    for (unsigned i = 0; i < 1200; ++i) {
        activity_led_effect_step(&effect, words, duty);
        assert(duty[0] > 0 && duty[0] < 16 && duty[1] == 0);
        if (i < 600) idle[i] = duty[0];
        else assert(duty[0] == idle[i - 600]);
        if (duty[0] < low) low = duty[0];
        if (duty[0] > high) high = duty[0];
    }
    assert(high > low); /* Breathing, not merely a dim steady LED. */

    /* Many reads and decode completions between samples still light both dies. */
    words[0] = activity_led_next_word(words[0], ACTIVITY_LED_BUSY);
    words[0] = activity_led_next_word(words[0], 0);
    words[1] = activity_led_next_word(words[1], ACTIVITY_LED_BUSY);
    words[1] = activity_led_next_word(words[1], 0);
    activity_led_effect_step(&effect, words, duty);
    assert(duty[0] >= 64 && duty[1] == duty[0] / 4);
    unsigned previous = duty[1];
    for (unsigned i = 0; i < 32; ++i) {
        activity_led_effect_step(&effect, words, duty);
        assert(duty[1] <= previous);
        previous = duty[1];
    }
    assert(duty[1] == 0 && effect.level[0] == 0 && effect.level[1] == 0);

    /* An operation lasting longer than a sample holds its colour brightly. */
    for (unsigned channel = 0; channel < 2; ++channel) {
        memset(&effect, 0, sizeof(effect));
        words[0] = words[1] = 0;
        words[channel] = activity_led_next_word(0, ACTIVITY_LED_BUSY);
        for (unsigned i = 0; i < 100; ++i)
            activity_led_effect_step(&effect, words, duty);
        assert(duty[channel] == (channel ? 63 : 255));
        if (!channel) assert(duty[1] == 0);
        else assert(duty[0] < 16);
        words[channel] = activity_led_next_word(words[channel], 0);
        activity_led_effect_step(&effect, words, duty);
        for (unsigned i = 0; i < 32; ++i)
            activity_led_effect_step(&effect, words, duty);
        assert(effect.level[channel] == 0);
    }

    /* Either producer can latch the distinctive fault pattern until reset. */
    for (unsigned channel = 0; channel < 2; ++channel) {
        memset(&effect, 0, sizeof(effect));
        words[0] = words[1] = 0;
        words[channel] = activity_led_next_word(0, ACTIVITY_LED_ERROR);
        for (unsigned i = 0; i < 400; ++i) {
            unsigned phase = i % 200;
            unsigned on = phase < 16 || (phase >= 32 && phase < 48);
            activity_led_effect_step(&effect, words, duty);
            assert(duty[0] == 0 && duty[1] == (on ? 255 : 0));
            words[0] = words[1] = 0; /* Even clearing inputs cannot clear a fault. */
        }
    }
}

static void test_frame_accents(void)
{
    activity_led_effect_t effect = {0};
    unsigned words[2] = {ACTIVITY_LED_BUSY, ACTIVITY_LED_BUSY}, duty[2];
    for (unsigned i = 0; i < 10; ++i)
        activity_led_effect_step(&effect, words, duty);
    assert(duty[0] == 255 && duty[1] == 63);
    /* A completion must survive an immediate return to busy. */
    words[1] = activity_led_next_word(words[1], ACTIVITY_LED_FRAME);
    words[1] = activity_led_next_word(words[1], ACTIVITY_LED_BUSY);
    for (unsigned i = 0; i < 50; ++i) {
        /* Simulate high frame rates: accents must still have a visible gap. */
        words[1] = activity_led_next_word(words[1], ACTIVITY_LED_FRAME | ACTIVITY_LED_BUSY);
        activity_led_effect_step(&effect, words, duty);
        if (i < 8) assert(duty[0] == 0 && duty[1] == 255);
        if (i >= 20) assert(duty[0] == 255 && duty[1] == 63);
    }
    activity_led_effect_step(&effect, words, duty);
    assert(duty[0] == 255 && duty[1] == 63); /* No frame: no autonomous accent. */
    words[1] = activity_led_next_word(words[1], ACTIVITY_LED_FRAME);
    activity_led_effect_step(&effect, words, duty);
    assert(duty[0] == 0 && duty[1] == 255);
    words[0] = activity_led_next_word(words[0], ACTIVITY_LED_ERROR);
    activity_led_effect_step(&effect, words, duty);
    assert(effect.failed && duty[0] == 0 && duty[1] == 255);
}

static void test_coalesced_updates(void)
{
    /* The LED task steps with the latest words it has taken, so several
     * updates may arrive, or none, between two steps. */
    activity_led_effect_t effect = {0};
    unsigned decode = ACTIVITY_LED_BUSY, words[2] = {0, ACTIVITY_LED_BUSY}, duty[2];
    for (unsigned i = 0; i < 60; ++i)
        activity_led_effect_step(&effect, words, duty);
    assert(duty[1] == 63 && !effect.frame_cooldown);
    /* Two frames, each replaced before a step: one accent. */
    decode = activity_led_next_word(decode, ACTIVITY_LED_FRAME);
    decode = activity_led_next_word(decode, ACTIVITY_LED_BUSY);
    decode = activity_led_next_word(decode, ACTIVITY_LED_FRAME);
    words[1] = activity_led_next_word(decode, ACTIVITY_LED_BUSY);
    activity_led_effect_step(&effect, words, duty);
    assert(duty[0] == 0 && duty[1] == 255);
    /* No further update: no second accent. */
    for (unsigned i = 0; i < 100; ++i)
        activity_led_effect_step(&effect, words, duty);
    assert(!effect.frame_pulse && duty[1] == 63);
}

static void test_pwm(void)
{
    /* Exhaust all 65536 colour mixes: sorted deadlines, bounded masks and
     * exactly the requested on-time for each LED, including equal/zero duty. */
    for (unsigned green = 0; green < 256; ++green) {
        for (unsigned red = 0; red < 256; ++red) {
            unsigned duty[2] = {green, red}, on[2] = {0};
            activity_led_edge_t edges[3];
            unsigned count = activity_led_pwm_edges(duty, edges);
            assert(count >= 1 && count <= 3 && edges[0].ticks == 0);
            for (unsigned i = 0; i < count; ++i) {
                unsigned next = i + 1 < count ? edges[i + 1].ticks : ACTIVITY_LED_PERIOD_TICKS;
                assert(edges[i].ticks < next && edges[i].value < 4);
                for (unsigned bit = 0; bit < 2; ++bit)
                    if (edges[i].value & (1u << bit)) on[bit] += next - edges[i].ticks;
            }
            assert(edges[count - 1].value == 0);
            assert(on[0] == green * ACTIVITY_LED_PERIOD_TICKS / 256);
            assert(on[1] == red * ACTIVITY_LED_PERIOD_TICKS / 256);
        }
    }
}

int main(void)
{
    test_mailbox();
    test_effects();
    test_frame_accents();
    test_coalesced_updates();
    test_pwm();
    /* Disabled hooks must also be usable without target headers/runtime. */
    activity_led_usb(ACTIVITY_LED_BUSY);
    activity_led_decode(ACTIVITY_LED_ERROR);
    puts("PASS: LED breathing, activity/decay, frame accents, coalesced updates, latched error, generation wrap and all PWM mixes");
    return 0;
}
