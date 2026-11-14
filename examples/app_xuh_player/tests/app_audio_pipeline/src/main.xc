#include <platform.h>
#include "audio_pipeline.h"
void test_audio_pipeline(chanend channel);

int main(void)
{
    chan audio;
    par {
        on tile[0]: mp2_audio_task(audio);
        on tile[1]: test_audio_pipeline(audio);
    }
    return 0;
}
