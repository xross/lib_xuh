#include <string.h>
#include <xcore/hwtimer.h>

#include "xuh_host.h"

#define XUH_WORD_ALIGNED __attribute__((aligned(4)))

enum {
    REQ_SET_ADDRESS = 5,
    REQ_GET_DESCRIPTOR = 6,
    REQ_SET_CONFIGURATION = 9,

    DESC_DEVICE = 1,
    DESC_CONFIGURATION = 2,
    DESC_INTERFACE = 4,
    DESC_ENDPOINT = 5,

    DEVICE_DESC_BYTES = 18,
    CONFIG_HEADER_BYTES = 9,
    INTERFACE_DESC_BYTES = 9,
    ENDPOINT_DESC_BYTES = 7,

    EP0_PACKET_MAX = 64,

    /* Reference clock ticks (100 MHz) */
    ATTACH_SETTLE_TICKS = 30000000, /* 300 ms */
    SET_ADDRESS_RECOVERY_TICKS = 200000 /* 2 ms */
};

static void wait_ticks(unsigned ticks)
{
    hwtimer_t timer = hwtimer_alloc();
    hwtimer_delay(timer, ticks);
    hwtimer_free(timer);
}

static unsigned read_le16(const uint8_t *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static void compose_setup(uint8_t setup[8], unsigned requestType, unsigned request,
                          unsigned value, unsigned index, unsigned length)
{
    setup[0] = (uint8_t)requestType;
    setup[1] = (uint8_t)request;
    setup[2] = (uint8_t)value;
    setup[3] = (uint8_t)(value >> 8);
    setup[4] = (uint8_t)index;
    setup[5] = (uint8_t)(index >> 8);
    setup[6] = (uint8_t)length;
    setup[7] = (uint8_t)(length >> 8);
}

void XUH_Host_Init(XUH_Host *host, chanend c_ep0_out, chanend c_ep0_in,
                   chanend c_out[], unsigned n_out, chanend c_in[], unsigned n_in)
{
    memset(host, 0, sizeof(*host));
    host->maxPacketSize0 = EP0_PACKET_MAX;
    host->ep0Out = XUH_InitEp(c_ep0_out);
    for (unsigned i = 0; i < n_out && i < XUH_MAX_DATA_EPS; i++)
        host->out[host->outCount++] = XUH_InitEp(c_out[i]);
    host->ep0In = XUH_InitEp(c_ep0_in);
    for (unsigned i = 0; i < n_in && i < XUH_MAX_DATA_EPS; i++)
        host->in[host->inCount++] = XUH_InitEp(c_in[i]);
}

void XUH_Host_SetActivityCallback(XUH_Host *host, XUH_ActivityCallback callback)
{
    host->activity = callback;
}

void XUH_Host_ReportActivity(XUH_Host *host, XUH_Activity activity)
{
    if (host->activity) host->activity(activity);
}

XUH_Status XUH_Host_ControlIn(XUH_Host *host, unsigned requestType, unsigned request,
                              unsigned value, unsigned index,
                              uint8_t *buffer, unsigned length, unsigned *received)
{
    uint8_t setup[8] XUH_WORD_ALIGNED;
    /* The transport stores whole received words, so a packet can run a few
     * bytes past its payload. Receive here and copy only the payload out. */
    uint8_t packet[EP0_PACKET_MAX + 8] XUH_WORD_ALIGNED;
    unsigned total = 0;

    if (received) *received = 0;
    compose_setup(setup, requestType, request, value, index, length);
    XUH_SetupTransfer(host->ep0Out, setup);

    /* Data stage: packets until the requested length or a short packet. */
    while (total < length) {
        int count = XUH_InTransfer(host->ep0In, packet);
        if (count < 0 || (unsigned)count > host->maxPacketSize0)
            return XUH_ERR_IO;
        unsigned copy = (unsigned)count < length - total ? (unsigned)count : length - total;
        memcpy(buffer + total, packet, copy);
        total += copy;
        if ((unsigned)count < host->maxPacketSize0) break;
    }

    /* Status stage: zero-length OUT */
    XUH_OutTransfer(host->ep0Out, setup, 0);
    if (received) *received = total;
    return XUH_OK;
}

XUH_Status XUH_Host_ControlNoData(XUH_Host *host, unsigned requestType, unsigned request,
                                  unsigned value, unsigned index)
{
    uint8_t setup[8] XUH_WORD_ALIGNED;
    uint8_t packet[EP0_PACKET_MAX + 8] XUH_WORD_ALIGNED;

    compose_setup(setup, requestType, request, value, index, 0);
    XUH_SetupTransfer(host->ep0Out, setup);
    /* Status stage: zero-length IN */
    return XUH_InTransfer(host->ep0In, packet) < 0 ? XUH_ERR_IO : XUH_OK;
}

XUH_Status XUH_Host_ClaimEndpoint(XUH_Host *host, unsigned address, XUH_Ep *ep)
{
    int in = (address & XUH_DIR_IN) != 0;
    unsigned count = in ? host->inCount : host->outCount;
    uint8_t *claimed = in ? &host->inClaimed : &host->outClaimed;

    for (unsigned i = 0; i < count; i++) {
        if (*claimed & (1u << i)) continue;
        XUH_Ep slot = in ? host->in[i] : host->out[i];
        if (XUH_SetEndpointAddress(slot, address)) return XUH_ERR_PROTOCOL;
        *claimed = (uint8_t)(*claimed | (1u << i));
        *ep = slot;
        return XUH_OK;
    }
    return XUH_ERR_NO_RESOURCE;
}

static XUH_Status get_descriptor(XUH_Host *host, unsigned type, uint8_t *buffer,
                                 unsigned length)
{
    unsigned received;
    XUH_Status status = XUH_Host_ControlIn(host, XUH_DIR_IN, REQ_GET_DESCRIPTOR,
                                           type << 8, 0, buffer, length, &received);
    if (status != XUH_OK) return status;
    return received == length && buffer[1] == type ? XUH_OK : XUH_ERR_PROTOCOL;
}

/* Record every interface and endpoint descriptor of a configuration. Class
 * specific descriptors are skipped. */
static XUH_Status parse_configuration(XUH_Device *device, const uint8_t *config,
                                      unsigned total)
{
    XUH_InterfaceInfo *interface = NULL;
    int skipping = 0; /* In an interface that was not recorded */

    for (unsigned offset = 0; offset < total; ) {
        const uint8_t *desc = config + offset;
        unsigned length = desc[0];
        if (length < 2 || length > total - offset) return XUH_ERR_PROTOCOL;

        if (desc[1] == DESC_INTERFACE) {
            if (length < INTERFACE_DESC_BYTES) return XUH_ERR_PROTOCOL;
            skipping = device->interfaceCount == XUH_MAX_INTERFACES;
            if (!skipping) {
                interface = &device->interfaces[device->interfaceCount++];
                interface->number = desc[2];
                interface->alternate = desc[3];
                interface->classCode = desc[5];
                interface->subClass = desc[6];
                interface->protocol = desc[7];
                interface->endpointCount = 0;
            }
        } else if (desc[1] == DESC_ENDPOINT) {
            if (length < ENDPOINT_DESC_BYTES) return XUH_ERR_PROTOCOL;
            if (interface && !skipping &&
                interface->endpointCount < XUH_MAX_ENDPOINTS_PER_INTERFACE) {
                XUH_EndpointInfo *endpoint = &interface->endpoints[interface->endpointCount++];
                endpoint->address = desc[2];
                endpoint->attributes = desc[3];
                endpoint->maxPacketSize = (uint16_t)read_le16(desc + 4);
                endpoint->interval = desc[6];
            }
        }
        offset += length;
    }
    return XUH_OK;
}

static XUH_Status enumerate(XUH_Host *host, XUH_Device *device)
{
    uint8_t desc[DEVICE_DESC_BYTES];
    uint8_t config[XUH_CONFIG_DESC_MAX];
    XUH_Status status;

    memset(device, 0, sizeof(*device));
    if (host->enumerated) return XUH_ERR_UNSUPPORTED;
    host->enumerated = 1;
    host->outClaimed = host->inClaimed = 0;
    host->maxPacketSize0 = EP0_PACKET_MAX;

    /* XUH_Manager() detects and resets the device; the first transfer below
     * blocks until it has. Allow the device to settle first. */
    wait_ticks(ATTACH_SETTLE_TICKS);

    /* The first eight bytes of the device descriptor give the control
     * endpoint's packet size. The device still has address 0. */
    status = get_descriptor(host, DESC_DEVICE, desc, 8);
    /* That returned, so a device is attached. */
    XUH_Host_ReportActivity(host, XUH_ACTIVITY_ENUMERATE);
    if (status != XUH_OK) return status;
    if (desc[7] != 8 && desc[7] != 16 && desc[7] != 32 && desc[7] != 64)
        return XUH_ERR_PROTOCOL;
    host->maxPacketSize0 = desc[7];

    status = XUH_Host_ControlNoData(host, 0, REQ_SET_ADDRESS, XUH_DEVICE_ADDRESS, 0);
    if (status != XUH_OK) return status;
    XUH_SetDeviceAddress(XUH_DEVICE_ADDRESS);
    wait_ticks(SET_ADDRESS_RECOVERY_TICKS);

    status = get_descriptor(host, DESC_DEVICE, desc, DEVICE_DESC_BYTES);
    if (status != XUH_OK) return status;
    device->address = XUH_DEVICE_ADDRESS;
    device->maxPacketSize0 = host->maxPacketSize0;
    device->deviceClass = desc[4];
    device->deviceSubClass = desc[5];
    device->deviceProtocol = desc[6];
    device->vendorId = (uint16_t)read_le16(desc + 8);
    device->productId = (uint16_t)read_le16(desc + 10);
    device->bcdDevice = (uint16_t)read_le16(desc + 12);

    /* The configuration descriptor header gives the length of the whole
     * configuration: every interface, endpoint and class descriptor. */
    status = get_descriptor(host, DESC_CONFIGURATION, config, CONFIG_HEADER_BYTES);
    if (status != XUH_OK) return status;
    unsigned total = read_le16(config + 2);
    if (total < CONFIG_HEADER_BYTES) return XUH_ERR_PROTOCOL;
    if (total > sizeof(config)) return XUH_ERR_NO_RESOURCE;

    status = get_descriptor(host, DESC_CONFIGURATION, config, total);
    if (status != XUH_OK) return status;
    device->configurationValue = config[5];
    status = parse_configuration(device, config, total);
    if (status != XUH_OK) return status;

    return XUH_Host_ControlNoData(host, 0, REQ_SET_CONFIGURATION,
                                  device->configurationValue, 0);
}

XUH_Status XUH_Host_WaitForDevice(XUH_Host *host, XUH_Device *device)
{
    XUH_Status status = enumerate(host, device);
    /* A second call fails without using the bus: not an activity. */
    if (status != XUH_ERR_UNSUPPORTED)
        XUH_Host_ReportActivity(host, status == XUH_OK ? XUH_ACTIVITY_IDLE
                                                       : XUH_ACTIVITY_ERROR);
    return status;
}

int XUH_Device_FindInterface(const XUH_Device *device, int classCode, int subClass,
                             int protocol)
{
    for (int i = 0; i < device->interfaceCount; i++) {
        const XUH_InterfaceInfo *interface = &device->interfaces[i];
        if (interface->classCode == classCode &&
            (subClass < 0 || interface->subClass == subClass) &&
            (protocol < 0 || interface->protocol == protocol))
            return i;
    }
    return -1;
}
