#include <stdio.h>
#include <string.h>
#include <xcore/hwtimer.h>

#include "xuh_msc.h"

#define XUH_WORD_ALIGNED __attribute__((aligned(4)))

#if XUH_MSC_TRACE
#define TRACE(...) printf(__VA_ARGS__)
#else
#define TRACE(...) ((void)0)
#endif

enum {
    REQ_GET_MAX_LUN = 0xfe,
    REQTYPE_CLASS_INTERFACE_IN = 0xa1,

    CBW_BYTES = 31,
    CSW_BYTES = 13,
    CBW_SIGNATURE = 0x43425355,
    CSW_SIGNATURE = 0x53425355,

    SCSI_TEST_UNIT_READY = 0x00,
    SCSI_REQUEST_SENSE = 0x03,
    SCSI_INQUIRY = 0x12,
    SCSI_READ_CAPACITY_10 = 0x25,
    SCSI_READ_10 = 0x28,

    /* One READ(10) covers at most this many blocks. */
    MAX_READ_BLOCKS = 64
};

/* The open device, for the FatFs disk functions in xuh_msc_fatfs.c. */
XUH_Msc *xuh_msc_open_device;

static void wait_ticks(unsigned ticks)
{
    hwtimer_t timer = hwtimer_alloc();
    hwtimer_delay(timer, ticks);
    hwtimer_free(timer);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

/* One Bulk-Only Transport command: command block, optional data-in stage and
 * status. Returns the number of data bytes received, or -1.
 * `data` must be word aligned and hold `length` rounded up to a whole packet:
 * full packets are received straight into consecutive positions of it. */
static int transport(XUH_Msc *msc, unsigned length, const uint8_t command[],
                     unsigned commandLength, uint8_t data[])
{
    uint8_t cbw[32] XUH_WORD_ALIGNED;
    uint8_t csw[XUH_MSC_BLOCK_BYTES] XUH_WORD_ALIGNED;
    uint32_t tag = msc->tag++;
    int received = 0;

    if (commandLength == 0 || commandLength > 16 ||
        length > MAX_READ_BLOCKS * XUH_MSC_BLOCK_BYTES)
        return -1;

    memset(cbw, 0, sizeof(cbw));
    write_le32(cbw, CBW_SIGNATURE);
    write_le32(cbw + 4, tag);
    write_le32(cbw + 8, length);
    cbw[12] = XUH_DIR_IN;
    cbw[13] = 0; /* Logical unit */
    cbw[14] = (uint8_t)commandLength;
    memcpy(cbw + 15, command, commandLength);

    TRACE("MSC tag=%u cmd=0x%02x: sending CBW\n", (unsigned)tag, command[0]);
    XUH_OutTransfer(msc->bulkOut, cbw, CBW_BYTES);

    if (length) {
        /* The data stage is one packet per IN transfer. A short packet, or
         * the expected length, ends the stage. */
        while ((unsigned)received < length) {
            int packet = XUH_InTransfer(msc->bulkIn, data + received);
            if (packet < 0 || packet > XUH_MSC_BLOCK_BYTES) { received = -1; break; }
            received += packet;
            if (packet < XUH_MSC_BLOCK_BYTES) break;
        }
        TRACE("MSC tag=%u: data bytes=%d\n", (unsigned)tag, received);
    }

    int cswLength = XUH_InTransfer(msc->bulkIn, csw);
    TRACE("MSC tag=%u: CSW bytes=%d\n", (unsigned)tag, cswLength);
    if (cswLength != CSW_BYTES || read_le32(csw) != CSW_SIGNATURE ||
        read_le32(csw + 4) != tag) {
        TRACE("MSC tag=%u: invalid CSW: bytes=%d signature=0x%08x tag=%u\n", (unsigned)tag,
              cswLength, (unsigned)read_le32(csw), (unsigned)read_le32(csw + 4));
        return -1;
    }
    uint32_t residue = read_le32(csw + 8);
    if (csw[12] != 0 || residue > length || received < 0 || (unsigned)received > length) {
        TRACE("MSC tag=%u: status=%u residue=%u data=%d expected=%u\n", (unsigned)tag,
              csw[12], (unsigned)residue, received, length);
        return -1;
    }
    /* Allocation-length commands such as REQUEST SENSE may return short data. */
    return received;
}

static int test_unit_ready(XUH_Msc *msc)
{
    const uint8_t command[6] = {SCSI_TEST_UNIT_READY};
    return transport(msc, 0, command, sizeof(command), NULL);
}

static int inquiry(XUH_Msc *msc)
{
    const uint8_t command[6] = {SCSI_INQUIRY, 0, 0, 0, 36, 0};
    uint8_t data[XUH_MSC_BLOCK_BYTES] XUH_WORD_ALIGNED;
    return transport(msc, 36, command, sizeof(command), data);
}

static int request_sense(XUH_Msc *msc)
{
    const uint8_t command[6] = {SCSI_REQUEST_SENSE, 0, 0, 0, 18, 0};
    uint8_t data[XUH_MSC_BLOCK_BYTES] XUH_WORD_ALIGNED;
    return transport(msc, 18, command, sizeof(command), data);
}

static int read_capacity(XUH_Msc *msc, uint32_t *lastBlock, uint32_t *blockBytes)
{
    const uint8_t command[10] = {SCSI_READ_CAPACITY_10};
    uint8_t data[XUH_MSC_BLOCK_BYTES] XUH_WORD_ALIGNED;

    if (transport(msc, 8, command, sizeof(command), data) != 8) return -1;
    *lastBlock = read_be32(data);
    *blockBytes = read_be32(data + 4);
    return 0;
}

/* READ(10) of 1..MAX_READ_BLOCKS consecutive blocks as one command, rather
 * than a command, data and status stage for every block. */
static int read_blocks(XUH_Msc *msc, uint32_t lba, unsigned count, uint8_t *buffer)
{
    uint8_t command[10] = {SCSI_READ_10};
    unsigned length = count * XUH_MSC_BLOCK_BYTES;

    command[2] = (uint8_t)(lba >> 24);
    command[3] = (uint8_t)(lba >> 16);
    command[4] = (uint8_t)(lba >> 8);
    command[5] = (uint8_t)lba;
    command[7] = (uint8_t)(count >> 8);
    command[8] = (uint8_t)count;
    return transport(msc, length, command, sizeof(command), buffer) == (int)length ? 0 : -1;
}

/* Bring the unit up. Only READ CAPACITY has to succeed: a unit reports its
 * power-on unit attention condition through the commands before it. */
static XUH_Status initialise_media(XUH_Msc *msc)
{
    uint32_t lastBlock, blockBytes;

    wait_ticks(10000);
    test_unit_ready(msc);
    wait_ticks(10000);
    inquiry(msc);
    request_sense(msc);
    wait_ticks(40000);

    if (read_capacity(msc, &lastBlock, &blockBytes)) return XUH_ERR_IO;
    if (blockBytes != XUH_MSC_BLOCK_BYTES) return XUH_ERR_UNSUPPORTED;
    msc->blockCount = lastBlock + 1;
    msc->blockSize = blockBytes;
    return XUH_OK;
}

XUH_Status XUH_Msc_Open(XUH_Msc *msc, XUH_Host *host, const XUH_Device *device)
{
    const XUH_EndpointInfo *in = NULL, *out = NULL;
    XUH_Status status;
    uint8_t maxLun;

    memset(msc, 0, sizeof(*msc));
    xuh_msc_open_device = NULL;
    int index = XUH_Device_FindInterface(device, XUH_MSC_CLASS, XUH_MSC_SUBCLASS_SCSI,
                                         XUH_MSC_PROTOCOL_BULK_ONLY);
    if (index < 0) return XUH_ERR_UNSUPPORTED;

    const XUH_InterfaceInfo *interface = &device->interfaces[index];
    for (unsigned i = 0; i < interface->endpointCount; i++) {
        const XUH_EndpointInfo *endpoint = &interface->endpoints[i];
        if ((endpoint->attributes & XUH_EP_TYPE_MASK) != XUH_EP_TYPE_BULK) continue;
        if (endpoint->address & XUH_DIR_IN) {
            if (!in) in = endpoint;
        } else if (!out) {
            out = endpoint;
        }
    }
    /* Whole blocks are received a packet at a time, so only the high-speed
     * bulk packet size is supported. */
    if (!in || !out || in->maxPacketSize != XUH_MSC_BLOCK_BYTES ||
        out->maxPacketSize != XUH_MSC_BLOCK_BYTES)
        return XUH_ERR_UNSUPPORTED;

    msc->host = host;
    msc->interfaceNumber = interface->number;
    msc->tag = 1;
    status = XUH_Host_ClaimEndpoint(host, out->address, &msc->bulkOut);
    if (status != XUH_OK) return status;
    status = XUH_Host_ClaimEndpoint(host, in->address, &msc->bulkIn);
    if (status != XUH_OK) return status;

    XUH_Host_ReportActivity(host, XUH_ACTIVITY_MSC_OPEN);
    /* A device with one logical unit need not support this request. */
    if (XUH_Host_ControlIn(host, REQTYPE_CLASS_INTERFACE_IN, REQ_GET_MAX_LUN, 0,
                           interface->number, &maxLun, 1, NULL) == XUH_OK)
        msc->maxLun = maxLun;
    status = initialise_media(msc);
    if (status == XUH_OK) xuh_msc_open_device = msc;
    XUH_Host_ReportActivity(host, status == XUH_OK ? XUH_ACTIVITY_IDLE : XUH_ACTIVITY_ERROR);
    return status;
}

XUH_Status XUH_Msc_Read(XUH_Msc *msc, uint32_t lba, uint32_t count, uint8_t *buffer)
{
    if (!count || lba >= msc->blockCount || count > msc->blockCount - lba)
        return XUH_ERR_PROTOCOL;

    while (count) {
        unsigned blocks = count < MAX_READ_BLOCKS ? (unsigned)count : MAX_READ_BLOCKS;
        XUH_Host_ReportActivity(msc->host, XUH_ACTIVITY_MSC_READ);
        int failed = read_blocks(msc, lba, blocks, buffer);
        XUH_Host_ReportActivity(msc->host, failed ? XUH_ACTIVITY_ERROR : XUH_ACTIVITY_IDLE);
        if (failed) return XUH_ERR_IO;
        lba += blocks;
        buffer += blocks * XUH_MSC_BLOCK_BYTES;
        count -= blocks;
    }
    return XUH_OK;
}
