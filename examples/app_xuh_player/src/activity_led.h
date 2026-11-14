#ifndef ACTIVITY_LED_H_
#define ACTIVITY_LED_H_

#ifndef APP_XUH_PLAYER_ACTIVITY_LED
#define APP_XUH_PLAYER_ACTIVITY_LED 0
#endif

enum { ACTIVITY_LED_BUSY = 1, ACTIVITY_LED_ERROR = 2, ACTIVITY_LED_FRAME = 4 };
#define ACTIVITY_LED_FRAME_MASK 0xff000000u

#if APP_XUH_PLAYER_ACTIVITY_LED
/* Tile 1 only. USBHost owns usb(); the decoder owns decode(). Errors latch
 * until reset. FRAME marks a successfully decoded frame, not an input request.
 * These best-effort updates never wait for the LED task. */
void activity_led_usb(unsigned flags);
void activity_led_decode(unsigned flags);
void activity_led_task(void);
#else
static inline void activity_led_usb(unsigned flags) { (void)flags; }
static inline void activity_led_decode(unsigned flags) { (void)flags; }
#endif

#ifndef __XC__
/* Portable effect/PWM model, also exercised by the native unit test. */
enum { ACTIVITY_LED_PERIOD_TICKS = 100000, ACTIVITY_LED_UPDATE_MS = 5 };
typedef struct {
    unsigned previous[2], level[2], phase, error_phase, failed;
    unsigned frame_pulse, frame_cooldown;
} activity_led_effect_t;
typedef struct { unsigned ticks, value; } activity_led_edge_t;

unsigned activity_led_next_word(unsigned previous, unsigned flags);
void activity_led_effect_step(activity_led_effect_t *effect,
                              const unsigned words[2], unsigned duty[2]);
unsigned activity_led_pwm_edges(const unsigned duty[2], activity_led_edge_t edges[3]);
#endif
#endif
