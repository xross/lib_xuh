#include "xuh_config.h"

#if XUH_MSC_FATFS

#include "ff.h"
#include "diskio.h"
#include "xuh_msc.h"

/* FatFs disk functions for physical drive 0: the device most recently opened
 * with XUH_Msc_Open(), which sets this. */
extern XUH_Msc *xuh_msc_open_device;

DSTATUS disk_status(BYTE drive)
{
    return drive == 0 && xuh_msc_open_device ? 0 : STA_NOINIT;
}

/* XUH_Msc_Open() has already brought the unit up. */
DSTATUS disk_initialize(BYTE drive)
{
    return disk_status(drive);
}

DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count)
{
    if (drive != 0 || count == 0) return RES_PARERR;
    if (!xuh_msc_open_device) return RES_NOTRDY;
    return XUH_Msc_Read(xuh_msc_open_device, (uint32_t)sector, count, buffer) == XUH_OK
               ? RES_OK : RES_ERROR;
}

/* There is no SCSI WRITE implementation. Never report success. */
DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count)
{
    (void)buffer;
    (void)sector;
    (void)count;
    return drive == 0 ? RES_WRPRT : RES_PARERR;
}

DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer)
{
    if (drive != 0) return RES_PARERR;
    if (!xuh_msc_open_device) return RES_NOTRDY;

    switch (command) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buffer = (LBA_t)xuh_msc_open_device->blockCount;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buffer = (WORD)xuh_msc_open_device->blockSize;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buffer = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

/* Fixed to 1 January 2010. */
DWORD get_fattime(void)
{
    return ((DWORD)(2010 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}

#endif /* XUH_MSC_FATFS */
