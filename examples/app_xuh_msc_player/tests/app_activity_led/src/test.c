#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <xs1.h>
#include <xcore/channel.h>
#include <xcore/hwtimer.h>
#include <xcore/port.h>
#include "activity_led.h"

#ifndef APP_XUH_MSC_PLAYER_LED_ACTIVE_LOW
#define APP_XUH_MSC_PLAYER_LED_ACTIVE_LOW 0
#endif

/* The LED task steps once per update period, so allow one more. */
enum { STEP_TICKS = ACTIVITY_LED_UPDATE_MS * ACTIVITY_LED_PERIOD_TICKS };

/* Tile 1: on request, count the LED-on samples over four PWM periods. */
void test_activity_led_sample(chanend c_sample)
{
    hwtimer_t timer = hwtimer_alloc();
    assert(timer);
    for (;;) {
        (void)chan_in_word(c_sample);
        unsigned start = hwtimer_get_time(timer);
        uint32_t on[2] = {0, 0};
        for (unsigned i = 0; i < 400; ++i) {
            hwtimer_wait_until(timer, start + i * 1000);
            unsigned value = port_peek(XS1_PORT_32A);
            assert((value & ~3u) == 0);
            value ^= APP_XUH_MSC_PLAYER_LED_ACTIVE_LOW ? 3u : 0u;
            if (value & 1) ++on[0];
            if (value & 2) ++on[1];
        }
        chan_out_buf_word(c_sample, on, 2);
    }
}

static void sample(chanend c_sample, unsigned on[2])
{
    uint32_t counts[2];
    chan_out_word(c_sample, 0);
    chan_in_buf_word(c_sample, counts, 2);
    on[0] = counts[0];
    on[1] = counts[1];
}

/* Tile 0: publish as the USB host and decoder do. */
void test_activity_led(chanend c_sample)
{
    hwtimer_t timer = hwtimer_alloc();
    assert(timer);
    unsigned on[2];
    hwtimer_delay(timer, 200000); /* Allow the concurrent task to enable its port. */
    sample(c_sample, on);
    assert(on[0] < 25 && on[1] == 0);

    activity_led_usb(ACTIVITY_LED_BUSY);
    hwtimer_delay(timer, 2500000 + STEP_TICKS);
    sample(c_sample, on);
    assert(on[0] > 360 && on[1] == 0);

    activity_led_decode(ACTIVITY_LED_BUSY);
    hwtimer_delay(timer, 2500000 + STEP_TICKS);
    sample(c_sample, on);
    assert(on[0] > 360 && on[1] > 80 && on[1] < 120);

    /* BUSY follows before the LED task steps; the word still counts the frame. */
    activity_led_decode(ACTIVITY_LED_FRAME);
    activity_led_decode(ACTIVITY_LED_BUSY);
    hwtimer_delay(timer, 600000 + STEP_TICKS);
    sample(c_sample, on);
    assert(on[0] == 0 && on[1] > 360);

    activity_led_usb(ACTIVITY_LED_ERROR);
    unsigned error_time = hwtimer_get_time(timer);
    hwtimer_delay(timer, 600000 + STEP_TICKS);
    sample(c_sample, on);
    assert(on[0] == 0 && on[1] > 360);
    activity_led_usb(0);
    activity_led_decode(0);
    sample(c_sample, on);
    assert(on[0] == 0 && on[1] > 360);
    /* 90 to 160 ms into the latched pattern, after the frame accent: the gap
     * between the two red flashes, with green still held off. */
    hwtimer_wait_until(timer, error_time + 10000000);
    sample(c_sample, on);
    assert(on[0] == 0 && on[1] == 0);
    hwtimer_free(timer);
    puts("PASS: tile-0 updates sent to the tile-1 LED task, PWM pins and latched error override");
    exit(0); /* The production LED threads intentionally run indefinitely. */
}
