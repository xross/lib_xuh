#ifndef FRAME_TIMING_H_
#define FRAME_TIMING_H_
#include <stdint.h>

#ifndef APP_XUH_MSC_PLAYER_FRAME_TIMING
#define APP_XUH_MSC_PLAYER_FRAME_TIMING 0
#endif

/* Optional development instrumentation: 1-ms upper-bound buckets, the final
 * bucket holding everything above 126 ms. No frame log or dynamic storage.
 * Zero initialise per play; individual intervals must fit a 32-bit timer delta. */
enum { FRAME_TIMING_BINS = 128 };
typedef struct {
    unsigned bins[FRAME_TIMING_BINS];
    unsigned count, min_us, max_us, over_40ms;
} frame_timing_t;

static inline void frame_timing_add(frame_timing_t *timing, unsigned us)
{
    unsigned bucket = us / 1000 + (us % 1000 != 0);
    if (bucket >= FRAME_TIMING_BINS) bucket = FRAME_TIMING_BINS - 1;
    ++timing->bins[bucket];
    if (!timing->count || us < timing->min_us) timing->min_us = us;
    if (us > timing->max_us) timing->max_us = us;
    timing->over_40ms += us > 40000;
    ++timing->count;
}

/* Nearest-rank percentile upper bound. Overflow-bin percentiles use the exact
 * observed maximum, not a fictitious 127-ms upper bound. percent is 1..100. */
static inline unsigned frame_timing_percentile(const frame_timing_t *timing, unsigned percent)
{
    uint64_t rank = ((uint64_t)timing->count * percent + 99) / 100;
    unsigned cumulative = 0;
    if (!timing->count) return 0;
    for (unsigned i = 0; i < FRAME_TIMING_BINS - 1; ++i) {
        cumulative += timing->bins[i];
        if (cumulative >= rank) {
            unsigned upper = i * 1000;
            return upper < timing->max_us ? upper : timing->max_us;
        }
    }
    return timing->max_us;
}
#endif
