#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "frame_timing.h"

int main(void)
{
    frame_timing_t timing = {0};
    assert(!frame_timing_percentile(&timing, 95));
    const unsigned samples[] = {0, 1, 999, 1000, 1001, 40000, 40001, 126000, 126001, UINT_MAX};
    for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
        frame_timing_add(&timing, samples[i]);
    assert(timing.count == 10 && timing.min_us == 0 && timing.max_us == UINT_MAX);
    assert(timing.over_40ms == 4);
    assert(timing.bins[0] == 1 && timing.bins[1] == 3 && timing.bins[2] == 1);
    assert(timing.bins[126] == 1 && timing.bins[127] == 2);
    assert(frame_timing_percentile(&timing, 50) == 2000);
    assert(frame_timing_percentile(&timing, 95) == UINT_MAX);
    timing = (frame_timing_t){0};
    for (unsigned i = 1; i <= 3025; ++i) frame_timing_add(&timing, i);
    assert(frame_timing_percentile(&timing, 50) == 2000);
    assert(frame_timing_percentile(&timing, 95) == 3000);
    assert(frame_timing_percentile(&timing, 99) == 3000);
    assert(frame_timing_percentile(&timing, 100) == 3025);
    unsigned sum = 0;
    for (unsigned i = 0; i < FRAME_TIMING_BINS; ++i) sum += timing.bins[i];
    assert(sum == timing.count);
    puts("PASS: frame timing boundaries, overflow, percentiles and full-play counts");
    return 0;
}
