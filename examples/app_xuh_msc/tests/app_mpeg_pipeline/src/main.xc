#include <platform.h>
#include "mpeg_pipeline.h"
void test_loader(chanend c_load);
void test_renderer(chanend c_render);

int main(void)
{
    chan c_load, c_image, c_render;
    par {
        on tile[1]: test_loader(c_load);
        on tile[1]: mpeg_pipeline_decode(c_load, c_image);
        on tile[0]: mpeg_pipeline_receive(c_image, c_render);
        on tile[0]: test_renderer(c_render);
    }
    return 0;
}
