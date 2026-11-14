/* USB mass storage class driver: Bulk-Only Transport with the SCSI transparent
 * command set, as used by USB flash drives. Read only.
 *
 * C API, with the same threading rules as xuh_host.h.
 *
 *     XUH_Msc msc;
 *     if (XUH_Msc_Open(&msc, &host, &device) == XUH_OK) {
 *         // msc.blockCount blocks of msc.blockSize bytes can be read with
 *         // XUH_Msc_Read(), or through FatFs.
 *     }
 *
 * FatFs: with XUH_MSC_FATFS set to 1 in the application's xuh_conf.h, and
 * lib_fatfs in the application's dependencies, lib_xuh defines the FatFs disk
 * functions (disk_initialize(), disk_status(), disk_read(), disk_write(),
 * disk_ioctl()) and get_fattime(), which returns a fixed date. Physical drive
 * 0 is then the device most recently opened with XUH_Msc_Open(), whose XUH_Msc
 * must stay valid. lib_xuh does not call FatFs: after a successful open the
 * application calls f_mount() and uses the volume. Writes fail with RES_WRPRT.
 */
#ifndef XUH_MSC_H_
#define XUH_MSC_H_

#include <stdint.h>
#include "xuh_host.h"

#define XUH_MSC_CLASS        0x08
#define XUH_MSC_SUBCLASS_SCSI 0x06
#define XUH_MSC_PROTOCOL_BULK_ONLY 0x50

/* The only supported block size, which is also the high-speed bulk packet size. */
#define XUH_MSC_BLOCK_BYTES  512

#ifndef __XC__

/* An open mass storage device. Treat all but blockCount and blockSize as
 * private. */
typedef struct {
    XUH_Host *host;
    XUH_Ep bulkOut, bulkIn;
    uint8_t interfaceNumber;
    uint8_t maxLun;
    uint32_t tag;
    uint32_t blockCount; /* Blocks on logical unit 0 */
    uint32_t blockSize;  /* Bytes in a block: XUH_MSC_BLOCK_BYTES */
} XUH_Msc;

/* Open the mass storage interface of an enumerated device: bind its bulk
 * endpoints and read the capacity of logical unit 0.
 * Returns XUH_ERR_UNSUPPORTED, without using the bus, if the device has no
 * SCSI Bulk-Only interface with a bulk endpoint in each direction; so this is
 * also the test for whether a device is mass storage. */
XUH_Status XUH_Msc_Open(XUH_Msc *msc, XUH_Host *host, const XUH_Device *device);

/* Read `count` consecutive blocks starting at block `lba`.
 * `buffer` must be word aligned and hold count * XUH_MSC_BLOCK_BYTES bytes. */
XUH_Status XUH_Msc_Read(XUH_Msc *msc, uint32_t lba, uint32_t count, uint8_t *buffer);

#endif /* __XC__ */
#endif
