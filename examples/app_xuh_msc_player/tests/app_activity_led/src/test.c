#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <xs1.h>
#include <xcore/hwtimer.h>
#include <xcore/port.h>
#include "activity_led.h"

#ifndef APP_XUH_MSC_PLAYER_LED_ACTIVE_LOW
#define APP_XUH_MSC_PLAYER_LED_ACTIVE_LOW 0
#endif

static void sample(hwtimer_t timer, unsigned on[2])
{
    unsigned start = hwtimer_get_time(timer);
    on[0] = on[1] = 0;
    for (unsigned i = 0; i < 400; ++i) {
        hwtimer_wait_until(timer, start + i * 1000); /* Four PWM periods. */
        unsigned value = port_peek(XS1_PORT_32A);
        assert((value & ~3u) == 0);
        value ^= APP_XUH_MSC_PLAYER_LED_ACTIVE_LOW ? 3u : 0u;
        if (value & 1) ++on[0];
        if (value & 2) ++on[1];
    }
}

void test_activity_led(void)
{
    hwtimer_t timer = hwtimer_alloc();
    assert(timer);
    unsigned on[2];
    hwtimer_delay(timer, 200000); /* Allow the concurrent task to enable its port. */
    sample(timer, on);
    assert(on[0] < 25 && on[1] == 0);

    activity_led_usb(ACTIVITY_LED_BUSY);
    hwtimer_delay(timer, 2500000);
    sample(timer, on);
    assert(on[0] > 360 && on[1] == 0);

    activity_led_decode(ACTIVITY_LED_BUSY);
    hwtimer_delay(timer, 2500000);
    sample(timer, on);
    assert(on[0] > 360 && on[1] > 80 && on[1] < 120);

    activity_led_decode(ACTIVITY_LED_FRAME);
    activity_led_decode(ACTIVITY_LED_BUSY);
    hwtimer_delay(timer, 600000);
    sample(timer, on);
    assert(on[0] == 0 && on[1] > 360);

    activity_led_usb(ACTIVITY_LED_ERROR);
    hwtimer_delay(timer, 600000);
    sample(timer, on);
    assert(on[0] == 0 && on[1] > 360);
    activity_led_usb(0);
    activity_led_decode(0);
    sample(timer, on);
    assert(on[0] == 0 && on[1] > 360);
    hwtimer_free(timer);
    puts("PASS: tile-1 LED task, PWM pins, USB/decode mailboxes and latched error override");
    exit(0); /* The production PWM task intentionally runs indefinitely. */
}
