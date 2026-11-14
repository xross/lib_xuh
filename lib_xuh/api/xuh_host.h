/* USB host core: device enumeration and control transfers.
 *
 * C API. Call it from one task on the same tile as XUH_Manager(): an endpoint
 * handle is a tile-local pointer.
 *
 *     XUH_Host host;
 *     XUH_Device device;
 *     XUH_Host_Init(&host, c_out[0], c_in[0], &c_out[1], n_out - 1, &c_in[1], n_in - 1);
 *     if (XUH_Host_WaitForDevice(&host, &device) == XUH_OK) {
 *         // Inspect device.interfaces[], or offer it to a class driver such
 *         // as XUH_Msc_Open().
 *     }
 */
#ifndef XUH_HOST_H_
#define XUH_HOST_H_

#include <stdint.h>
#include "xuh.h"
#include "xuh_config.h"

/* Result of every host and class driver call.
 * The transport does not yet detect timeouts, stalls or disconnection: a
 * transfer to a device that stops responding blocks. TIMEOUT, STALL and
 * DISCONNECTED are reserved so that callers can handle them now and signatures
 * need not change when the transport reports them. */
typedef enum {
    XUH_OK = 0,
    XUH_ERR_PROTOCOL,     /* Malformed or short descriptor or reply */
    XUH_ERR_NO_RESOURCE,  /* No free channel slot, or a buffer limit exceeded */
    XUH_ERR_UNSUPPORTED,  /* Valid, but not something this library handles */
    XUH_ERR_IO,           /* A transfer or command failed */
    XUH_ERR_TIMEOUT,      /* Reserved */
    XUH_ERR_STALL,        /* Reserved */
    XUH_ERR_DISCONNECTED  /* Reserved */
} XUH_Status;

/* bmAttributes transfer type of an endpoint */
#define XUH_EP_TYPE_MASK        0x03
#define XUH_EP_TYPE_CONTROL     0x00
#define XUH_EP_TYPE_ISOCHRONOUS 0x01
#define XUH_EP_TYPE_BULK        0x02
#define XUH_EP_TYPE_INTERRUPT   0x03
/* Direction bit of bEndpointAddress and of bmRequestType */
#define XUH_DIR_IN              0x80

typedef struct {
    uint8_t address;        /* bEndpointAddress, including the direction bit */
    uint8_t attributes;     /* bmAttributes */
    uint16_t maxPacketSize; /* wMaxPacketSize */
    uint8_t interval;       /* bInterval */
} XUH_EndpointInfo;

typedef struct {
    uint8_t number;         /* bInterfaceNumber */
    uint8_t alternate;      /* bAlternateSetting */
    uint8_t classCode;      /* bInterfaceClass */
    uint8_t subClass;       /* bInterfaceSubClass */
    uint8_t protocol;       /* bInterfaceProtocol */
    uint8_t endpointCount;  /* Entries used in endpoints[] */
    XUH_EndpointInfo endpoints[XUH_MAX_ENDPOINTS_PER_INTERFACE];
} XUH_InterfaceInfo;

/* An enumerated device: addressed and in its first configuration. */
typedef struct {
    uint16_t vendorId, productId, bcdDevice;
    uint8_t deviceClass, deviceSubClass, deviceProtocol;
    uint8_t address;
    uint8_t maxPacketSize0;
    uint8_t configurationValue;
    /* Entries used in interfaces[], one for each interface descriptor in the
     * configuration (so one for each alternate setting). Interface and
     * endpoint descriptors beyond the configured limits are not recorded. */
    uint8_t interfaceCount;
    XUH_InterfaceInfo interfaces[XUH_MAX_INTERFACES];
} XUH_Device;

/* What the library is doing on the bus, as reported to the application's
 * activity callback. Every activity is followed by IDLE or ERROR when it ends. */
typedef enum {
    XUH_ACTIVITY_IDLE = 0,   /* The last activity finished */
    XUH_ACTIVITY_ERROR,      /* The last activity failed */
    XUH_ACTIVITY_ENUMERATE,  /* Enumerating a newly attached device */
    XUH_ACTIVITY_MSC_OPEN,   /* Bringing up a mass storage device */
    XUH_ACTIVITY_MSC_READ    /* Reading blocks from a mass storage device */
} XUH_Activity;

#ifndef __XC__

/* Put this before the definition of an activity callback, so that the tools
 * can account for its stack. */
#define XUH_ACTIVITY_CALLBACK __attribute__((fptrgroup("xuh_activity")))

typedef void (*XUH_ActivityCallback)(XUH_Activity activity);

/* Host context. Treat the fields as private. */
typedef struct {
    XUH_Ep ep0Out, ep0In;
    XUH_Ep out[XUH_MAX_DATA_EPS], in[XUH_MAX_DATA_EPS];
    uint8_t outCount, inCount;
    uint8_t outClaimed, inClaimed; /* One bit per slot */
    uint8_t maxPacketSize0;
    uint8_t enumerated;
    XUH_ACTIVITY_CALLBACK XUH_ActivityCallback activity;
} XUH_Host;

/* Take the channel ends that connect this task to XUH_Manager(): one OUT and
 * one IN for the default control endpoint, and up to XUH_MAX_DATA_EPS further
 * slots in each direction for class drivers to claim. Any slots beyond that
 * limit are left unused. */
void XUH_Host_Init(XUH_Host *host, chanend c_ep0_out, chanend c_ep0_in,
                   chanend c_out[], unsigned n_out, chanend c_in[], unsigned n_in);

/* Have `callback` called as the library starts and finishes each activity on
 * the bus, for example to drive an activity LED. It runs in the task that
 * called the library and must not call back into it. NULL, the default,
 * disables it. Call after XUH_Host_Init(). */
void XUH_Host_SetActivityCallback(XUH_Host *host, XUH_ActivityCallback callback);

/* Report an activity to the application's callback. For class drivers. */
void XUH_Host_ReportActivity(XUH_Host *host, XUH_Activity activity);

/* Wait for a device, then enumerate it: read its device and configuration
 * descriptors, give it an address and select its first configuration.
 * Describes the device and every recorded interface and endpoint in `device`.
 * Does not bind any data endpoint and does not interpret the device class.
 *
 * Blocks until a device is attached. The transport detects a device once,
 * after reset, so this can be called once: a later call returns
 * XUH_ERR_UNSUPPORTED until the transport can report disconnection. */
XUH_Status XUH_Host_WaitForDevice(XUH_Host *host, XUH_Device *device);

/* Index in device->interfaces[] of the first interface with this class,
 * subclass and protocol, or -1. A negative subclass or protocol matches any. */
int XUH_Device_FindInterface(const XUH_Device *device, int classCode, int subClass,
                             int protocol);

/* Bind a free channel slot to a device endpoint address (direction bit
 * included) and return its handle. Slots stay claimed until the next
 * XUH_Host_WaitForDevice(). */
XUH_Status XUH_Host_ClaimEndpoint(XUH_Host *host, unsigned address, XUH_Ep *ep);

/* Control transfer with a device-to-host data stage. Receives up to `length`
 * bytes (the wLength sent) into `buffer`, which needs no particular alignment,
 * and stores the number received in `*received` if it is not NULL. A device may
 * return fewer bytes than requested. */
XUH_Status XUH_Host_ControlIn(XUH_Host *host, unsigned requestType, unsigned request,
                              unsigned value, unsigned index,
                              uint8_t *buffer, unsigned length, unsigned *received);

/* Control transfer with no data stage. */
XUH_Status XUH_Host_ControlNoData(XUH_Host *host, unsigned requestType, unsigned request,
                                  unsigned value, unsigned index);

#endif /* __XC__ */
#endif
