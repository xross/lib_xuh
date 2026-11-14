#include <platform.h>
#include "activity_led.h"

void test_activity_led(void);

int main(void)
{
    par {
        on tile[1]: activity_led_task();
        on tile[1]: test_activity_led();
    }
    return 0;
}
