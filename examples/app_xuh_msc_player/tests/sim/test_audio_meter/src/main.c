/* The integer level meter against the float one, for seeded random peaks of at
 * most TEST_PEAK_BITS bits. TEST_PEAK_BITS comes from the build configuration.
 * The seed is the first command-line argument if there is one, and otherwise
 * TEST_SEED from a header the test generates before building. */
#include <stdio.h>
#include <stdlib.h>
#include "audio_meter.h"
#include "test_seed.h"

enum { CASES = 2000 };

int main(int argc, char **argv)
{
    unsigned seed = argc > 1 ? (unsigned)strtoul(argv[1], 0, 0) : TEST_SEED;
    unsigned state = seed;
    printf("seed=%u bits=%d\n", seed, TEST_PEAK_BITS);
    for (unsigned i = 0; i < CASES; ++i) {
        state = state * 1664525u + 1013904223u;
        unsigned peak = state >> (32 - TEST_PEAK_BITS);
        unsigned level = audio_meter_level_s32(peak);
        /* The integer meter uses the top 16 bits below full scale; the float
         * meter given exactly that value must agree. */
        unsigned reference = audio_meter_level((float)(peak >> 15) / 65536.0f);
        if (level != reference || level > AUDIO_METER_BAR_WIDTH) {
            printf("FAIL: peak=%u level=%u reference=%u\n", peak, level, reference);
            return 1;
        }
    }
    printf("PASS: %d peaks\n", CASES);
    return 0;
}
