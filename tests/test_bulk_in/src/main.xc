#include <assert.h>
#include <platform.h>
#include <print.h>
#include <stdlib.h>
#include "xuh.h"

static void check_bulk_in(chanend c_out[1], chanend c_in[2])
{
    /* Initialise every peer in the order the manager publishes its handles. */
    XUH_InitEp(c_out[0]);
    XUH_InitEp(c_in[0]);
    XUH_Ep ep = XUH_InitEp(c_in[1]);
    assert(XUH_SetEndpointAddress(ep, 0x84) == 0);
    XUH_SetDeviceAddress(5);

    union { unsigned words[6]; unsigned char bytes[24]; } storage;
    for (unsigned i = 0; i < sizeof(storage.bytes); ++i) storage.bytes[i] = 0xa7;
    unsigned char expected[7] = {0x00, 0x11, 0x7f, 0x80, 0xa5, 0xfe, 0xff};
    int length = XUH_InTransfer(ep, storage.bytes + 4);
    assert(length == sizeof(expected));
    for (unsigned i = 0; i < sizeof(expected); ++i)
        assert(storage.bytes[4 + i] == expected[i]);
    /* Allow space for packet/CRC staging, but not writes outside that storage. */
    for (unsigned i = 0; i < 4; ++i) assert(storage.bytes[i] == 0xa7);
    for (unsigned i = 16; i < sizeof(storage.bytes); ++i) assert(storage.bytes[i] == 0xa7);
    printstr("PASS: bulk IN slot=1 endpoint=4 address=5 bytes=7 guards intact\n");
    _Exit(0);
}

int main(void)
{
    chan c_out[1], c_in[2];
    par {
        on tile[0]: XUH_Manager(c_out, 1, c_in, 2);
        on tile[0]: check_bulk_in(c_out, c_in);
    }
    return 0;
}
