#ifndef TEST_DAC_FIXTURE_H_
#define TEST_DAC_FIXTURE_H_

#include "audio_dac_queue.h"

enum { FIXTURE_ARM = 1, FIXTURE_SNAPSHOT = 2,
       FIXTURE_FRAMES = 3 * 1152 };
/* The consumer replies only between callbacks, giving a coherent observation.
 * Capture the requested prefix of the played stream, including zero samples. */
enum { FIXTURE_CALLBACKS = DAC_STATUS_WORDS, FIXTURE_CAPTURED, FIXTURE_HASH,
       FIXTURE_NONZERO, FIXTURE_AUDIO_MS, FIXTURE_AUDIO_VALID, FIXTURE_WORDS };

static inline unsigned test_pcm_hash(unsigned hash, const int32_t pair[2])
{
    return ((hash * 33) ^ (unsigned)pair[0]) * 33 ^ (unsigned)pair[1];
}

#endif
