#include <platform.h>
#include "activity_led.h"

void test_activity_led(chanend c_sample);
void test_activity_led_sample(chanend c_sample);

int main(void)
{
    chan c_led_usb, c_led_decode, c_sample;
    par {
        /* As in the player: writers on tile 0, LEDs on tile 1. */
        on tile[0]:
        {
            activity_led_usb_init(c_led_usb);
            activity_led_decode_init(c_led_decode);
            test_activity_led(c_sample);
        }
        on tile[1]: activity_led_task(c_led_usb, c_led_decode);
        on tile[1]: test_activity_led_sample(c_sample);
    }
    return 0;
}
