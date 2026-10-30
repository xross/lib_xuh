
#include <xs1.h>
#include <print.h>
#include <xcore/chanend.h>
#include "gpioDefines.h"

#include "usb_std_requests.h"
#include "usb_defs.h"
#include "xuh.h"
#include "stdio.h"
#include <string.h>

#if APP_XUH_MSC_ENABLE_JPEG
#include "bmp_writer.h"
#include "jpeg.h"
#endif

#include "ff.h"

#include "timing.h"

unsigned int get_time(void);
#include "diskio.h"

#include "msc_support.h"

#define MSC_WORD_ALIGNED __attribute__((aligned(4)))

XUH_Ep g_ep_out2;
XUH_Ep g_ep_in1;
XUH_Ep g_ep_in0;
XUH_Ep g_ep_out0;

#if APP_XUH_MSC_ENABLE_JPEG
#define JPEG_INPUT_FILE       "TEST0.JPG"
#define BMP_OUTPUT_FILE       "decoded.bmp"
#define JPEG_INPUT_MAX_BYTES  (40 * 1024)
#define JPEG_INPUT_WIDTH      400
#define JPEG_INPUT_HEIGHT     400
#define JPEG_DECODE_SCALE     1
#define JPEG_OUTPUT_WIDTH     (JPEG_INPUT_WIDTH >> JPEG_DECODE_SCALE)
#define JPEG_OUTPUT_HEIGHT    (JPEG_INPUT_HEIGHT >> JPEG_DECODE_SCALE)
#endif

#define LCD_WIDTH             240
#define LCD_HEIGHT            320

void USBH_MSC_Init()
{

    // DO nothing
}

void delay(unsigned x);
void ComposeSetupBuffer(USB_SetupPacket_t *sp, unsigned char buffer[8]);

/* XUH MSC SCSI FUNCTIONS */
s_ms_cbw ms_cbw = {
    0x43425355, 0x12345678,
    0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,
};
//
unsigned g_tag = 0x1;
static int g_msc_trace;

static unsigned read_le32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

int ms_transport(XUH_Ep ep_out, XUH_Ep ep_in,
    unsigned length, int dir, int lun, unsigned cblen, char cb[], // Command stage
    unsigned char data[], // Data Stage
    int *dataResidue, int *status)
{
    unsigned char cbw_com[64] MSC_WORD_ALIGNED;
    unsigned char buffer[512] MSC_WORD_ALIGNED = {0};
    unsigned tag = g_tag;
    int received = 0;

    *dataResidue = 0;
    *status = -1;
    if (cblen == 0 || cblen > 16 || length > 512 || (!dir && length))
        return -1;

    /* Signature */
    cbw_com[0] = 0x55;
    cbw_com[1] = 0x53;
    cbw_com[2] = 0x42;
    cbw_com[3] = 0x43;

    /* Tag */
    cbw_com[7] = ((g_tag & 0xff000000) >> 24);
    cbw_com[6] = ((g_tag & 0xff0000) >> 16);
    cbw_com[5] = ((g_tag & 0xff00) >> 8);
    cbw_com[4] = ((g_tag & 0xff) >> 0);

    g_tag++;

    /* Data transfer length */
    cbw_com[11] = ((length & 0xff000000) >> 24);
    cbw_com[10] = ((length & 0xff0000) >> 16);
    cbw_com[9] = ((length & 0xff00) >> 8);
    cbw_com[8] = ((length & 0xff) >> 0);

    /* Flags */
    cbw_com[12] = dir<<7;  /* 1: data in from device, 0, data to the device */

    /* Logical Unit Number (LUN) */
    cbw_com[13] = lun;

    /* Command Block Length */
    cbw_com[14] = cblen;

    /* Fill in the acutal command */
    for(int i = 0; i < cblen; i++)
    {
        cbw_com[15+i] = cb[i];
    }

    for(int i = cblen+15; i < 31; i++)
    {
        cbw_com[i] = 0;
    }

	/* Send to device */
    if (g_msc_trace)
        printf("MSC tag=%u cmd=0x%02x: sending CBW\n", tag, (unsigned char)cb[0]);
    XUH_OutTransfer(ep_out, cbw_com, 31);

    if(dir && length)
    {
        if (g_msc_trace)
            printf("MSC tag=%u: waiting for data\n", tag);
        received = XUH_InTransfer(ep_in, data);
        if (g_msc_trace)
            printf("MSC tag=%u: data bytes=%d\n", tag, received);
    }

    if (g_msc_trace)
        printf("MSC tag=%u: waiting for CSW\n", tag);
    int csw_length = XUH_InTransfer(ep_in, buffer);
    if (g_msc_trace)
        printf("MSC tag=%u: CSW bytes=%d\n", tag, csw_length);
    if (csw_length != 13 || read_le32(buffer) != 0x53425355 ||
        read_le32(buffer + 4) != tag || buffer[12] > 2) {
        printf("MSC tag=%u: invalid CSW: bytes=%d signature=0x%08x tag=%u status=%u\n",
               tag, csw_length, read_le32(buffer), read_le32(buffer + 4), buffer[12]);
        return -1;
    }
    *dataResidue = (int)read_le32(buffer + 8);
    *status = buffer[12];
    if (*status || *dataResidue < 0 || (unsigned)*dataResidue > length ||
        received < 0 || (unsigned)received > length) {
        printf("MSC tag=%u: status=%d residue=%d data=%d expected=%u\n",
               tag, *status, *dataResidue, received, length);
        return -1;
    }
    /* Allocation-length commands such as REQUEST SENSE may return short data. */
    return received;
}


int ms_readcapacity(XUH_Ep ep_out, XUH_Ep ep_in, int lun, int lba, int *lbaddr, int *lblen)
{
    char cb[10] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    int residue, status;
    unsigned char data[512] MSC_WORD_ALIGNED;
    int retval;

    cb[2] = (lba >> 24) & 0xFF;
    cb[3] = (lba >> 16) & 0xFF;
    cb[4] = (lba >> 8) & 0xFF;
    cb[5] = (lba >> 0) & 0xFF;

    retval = ms_transport(ep_out, ep_in, 8, 1, lun, 10, cb, data, &residue, &status);
    if (retval != 8) return -1;

    *lbaddr = ((unsigned)data[0] << 24) | ((unsigned)data[1] << 16) | ((unsigned)data[2] << 8) | data[3];
    *lblen = ((unsigned)data[4] << 24) | ((unsigned)data[5] << 16) | ((unsigned)data[6] << 8) | data[7];

    return status;
}

int ms_requestsense(XUH_Ep ep_out, XUH_Ep ep_in, int lun, int desc, int *responsecode)
{
    char cb[6] = {0x03, 0, 0, 0, 18, 0};
    unsigned char data[512] MSC_WORD_ALIGNED;
    int residue, status;
    int retval;

    cb[1] = desc & 1;

    retval = ms_transport(ep_out, ep_in, 18, 1, lun, 6, cb, data, &residue, &status);
    if (retval < 1) return -1;

    *responsecode = data[0] & 0x7f;

    return status;

}

int ms_testunitready(XUH_Ep ep_out, XUH_Ep ep_in, int lun)
{
    char cb[6] = {0,0,0,0,0,0};
    unsigned char data[512] MSC_WORD_ALIGNED;
    int residue, status;

    int retval;

    retval = ms_transport(ep_out, ep_in, 0, 0, lun, 6, cb, data, &residue, &status);
    return retval < 0 ? retval : status;
}

int ms_inquiry(XUH_Ep ep_out, XUH_Ep ep_in, int lun, int *deviceType, int *removable, int *dataformat)
{
  char cb[6] = {0x12, 0x0, 0x0, 0x0, 36, 0x0};
  int residue, status;
  unsigned char data[512] MSC_WORD_ALIGNED;
  int retval;

  retval = ms_transport(ep_out, ep_in, 36, 1, lun, 6, cb, data, &residue, &status);
  if (retval < 5) return -1;

  *deviceType = data[0] & 0x1f;
  *removable = (data[1] >> 7) & 1;
  *dataformat = data[3] & 0xF;

  return status;
}

int ms_read(XUH_Ep ep_out, XUH_Ep ep_in, int lun, unsigned lba, unsigned char buff[])
{

    char cb[10] = {0x28, 0, 0, 0, 0, 0, 0, 0, 1, 0};
    int residue, status;
    int retval;

    cb[2] = (lba >> 24) & 0xFF;
    cb[3] = (lba >> 16) & 0xFF;
    cb[4] = (lba >> 8) & 0xFF;
    cb[5] = (lba >> 0) & 0xFF;

    retval = ms_transport(ep_out, ep_in, 512, 1, lun, 10, cb, buff, &residue, &status);

    return retval == 512 && residue == 0 ? status : -1;
}

unsigned g_capacity;
unsigned g_blocksize;

int XUH_MSDSCSIMediaInitialise()
{
    g_msc_trace = 1;
    /* This function performs the followin SCSI commands:
        * READ CAPACITY 10
        * REQUEST SENSE
     */
    int lbaddr;
    int lnlen;

    int responsecode;
    int devicetype, removable, dataformat;

    unsigned char buffer[64];
    unsigned char spBuffer[8];
    USB_BmRequestType_t bmRequestType;
    USB_SetupPacket_t sp;
    int status;

    delay(10000);

    //printstr("INIT");
    status = ms_testunitready(g_ep_out2, g_ep_in1, 0);
    delay(10000);
    ms_inquiry(g_ep_out2, g_ep_in1, 0, &devicetype, & dataformat, &removable);

    ms_requestsense(g_ep_out2, g_ep_in1, 0, 0, &responsecode);
    delay(40000);

    status = ms_readcapacity(g_ep_out2, g_ep_in1, 0, 0, &lbaddr,&lnlen);
    if (status || lnlen != 512) {
        printf("MSC: capacity read failed or unsupported sector size\n");
        return 1;
    }

    g_capacity = lbaddr;
    g_blocksize = lnlen;

    g_msc_trace = 0;
    return 0;
}

int XUH_MSDSCSISectorRead(unsigned sector, unsigned char buff[])
{
    /* Issues READ 10 command block */
    return ms_read(g_ep_out2, g_ep_in1, 0, sector, buff);
}


/* FATFS FUNCTIONS */

DSTATUS disk_initialize (
        BYTE ifNum                                /* Physical drive nmuber (0..) */
)
{
    if (ifNum != 0) return STA_NOINIT;
    if(XUH_MSDSCSIMediaInitialise())
    {
        return STA_NOINIT;
    }
    return 0;
}


//}

DRESULT disk_read (
        BYTE ifNum,              /* Physical drive nmuber (0..) */
        BYTE *buff,            /* Data buffer to store read data */
        LBA_t sector,           /* Sector address (LBA) */
        UINT count              /* Number of sectors to read (1..255) */
)
{
    UINT nSector;

    if (g_msc_trace)
        printf("disk_read: drive=%u sector=%lu count=%u\n",
               ifNum, (unsigned long)sector, count);

   // printstr("read ifNum:");
    //printint(ifNum); printstr(" sector: "), printint(sector); printstr(" count: "); printintln(count);
    if(ifNum != 0 || count == 0)
    {
        printstr("ifNum out of Range");
        return RES_PARERR;
    }
    for(nSector = 0; nSector < count; nSector++)
    {
        if (XUH_MSDSCSISectorRead(sector, buff))
        {
            return RES_ERROR;
        }
        sector++;
        buff += 512;
    }
    return RES_OK;
 }


//DRESULT disk_write (BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
DRESULT disk_write(BYTE IfNum, const BYTE *buff, LBA_t sector, UINT count)
{
    /* This example has no SCSI WRITE implementation. Never report success. */
    return IfNum == 0 ? RES_WRPRT : RES_PARERR;
}

DWORD get_fattime(void)
{
    printstr("time");

  return ((DWORD)(2010 - 1980) << 25)  /* Fixed to Jan. 1, 2010 */
          | ((DWORD)1 << 21)
          | ((DWORD)1 << 16)
          | ((DWORD)0 << 11)
          | ((DWORD)0 << 5)
          | ((DWORD)0 >> 1);
}

DSTATUS disk_status(BYTE IfNum)
{
        return IfNum == 0 ? 0 : STA_NOINIT;
    //printstr("status");
   // return STA_NOINIT;
}

DRESULT disk_ioctl (BYTE IfNum, BYTE ctrl, void* Buff)
{
    if (IfNum != 0) return RES_PARERR;

    switch(ctrl)
    {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            *(LBA_t*)Buff = (LBA_t)g_capacity + 1;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD*)Buff = 1;
            return RES_OK;
        case GET_SECTOR_SIZE:

            *(WORD*)Buff = 512;
            return RES_OK;

        default:
            return RES_PARERR;
    }
}

void die(FRESULT rc ) /* Stop with dying message */
{
  printf("\nFailed with rc=%u.\n", rc);
  for(;;);
}
void mountdie(FRESULT rc ) /* Stop with dying message */
{
  printf("\nmountFailed with rc=%u.\n", rc);
  for(;;);
}
void opendie(FRESULT rc ) /* Stop with dying message */
{
  printf("\nopenFailed with rc=%u.\n", rc);
  for(;;);
}

FATFS Fatfs;
FIL   Fil;
//BYTE  fatfsbuffer[1024];
BYTE Buff[512*60];      /* File read buffer (40 SD card blocks to let multiblock operations (if file not fragmented) */
#if APP_XUH_MSC_ENABLE_JPEG
static BYTE RgbBuff[JPEG_DECODE_OUTPUT_SIZE(JPEG_OUTPUT_WIDTH, JPEG_OUTPUT_HEIGHT)];
static BYTE DecodeWork[JPEG_DECODE_WORK_SIZE] WORD_ALIGNED;
#endif

int XUH_ControlTransfer_In(XUH_Ep ep_out, XUH_Ep ep_in, USB_SetupPacket_t sp, unsigned char buffer[]);

#if APP_XUH_MSC_ENABLE_JPEG
static int read_jpeg_from_msc(const char* path, BYTE* dst, UINT dst_size, UINT* bytes_read)
{
    FRESULT rc;

    *bytes_read = 0;

    rc = f_open(&Fil, path, FA_READ);
    if (rc)
    {
        printf("failed rc=%u\n", rc);
        return 1;
    }

    rc = f_read(&Fil, dst, dst_size, bytes_read);

    if (rc) {
        printf("read failed rc=%u\n", rc);
        f_close(&Fil);
        return 1;
    }

    rc = f_close(&Fil);
    if (rc) {
        printf("close failed rc=%u\n", rc);
        return 1;
    }

    if (*bytes_read == 0 || *bytes_read == dst_size) {
        printf("invalid size %u bytes\n", *bytes_read);
        return 1;
    }

    return 0;
}

#if APP_XUH_MSC_ENABLE_LCD && !APP_XUH_MSC_LCD_TEST_MODE
static void send_rgb888_to_lcd(chanend c_lcd_image, const BYTE *rgb,
                               unsigned width, unsigned height,
                               unsigned channels)
{
    unsigned crop_width = width < LCD_WIDTH ? width : LCD_WIDTH;
    unsigned crop_height = height < LCD_HEIGHT ? height : LCD_HEIGHT;
    unsigned crop_x = (width - crop_width) / 2;
    unsigned crop_y = (height - crop_height) / 2;

    chanend_out_word(c_lcd_image, crop_width);
    chanend_out_word(c_lcd_image, crop_height);

    for (unsigned y = 0; y < crop_height; ++y) {
        const BYTE *row = rgb + ((crop_y + y) * width + crop_x) * channels;

        for (unsigned x = 0; x < crop_width; ++x) {
            chanend_out_byte(c_lcd_image, row[x * channels]);
            chanend_out_byte(c_lcd_image, row[x * channels + 1]);
            chanend_out_byte(c_lcd_image, row[x * channels + 2]);
        }
    }
    /* Release the cross-tile route, then wait for rendering to finish. */
    chanend_out_control_token(c_lcd_image, XS1_CT_END);
    chanend_check_control_token(c_lcd_image, XS1_CT_END);
}
#endif
#endif



void MassStorage(XUH_Ep ep_out0, XUH_Ep ep_in0, XUH_Ep ep_out2, XUH_Ep ep_in1
#if APP_XUH_MSC_ENABLE_LCD
                 , chanend c_lcd_image
#endif
                 )
{
    FRESULT rc;
    DIR dir;
    FILINFO fno;
    unsigned char buffer[64];
  UINT bw, br, i;
    FRESULT rest;
    int x;

    g_ep_out2 = ep_out2;
    g_ep_in1 = ep_in1;
    g_ep_out0 = ep_out0;
    g_ep_in0 = ep_in0;

    USB_BmRequestType_t bmRequestType;
    USB_SetupPacket_t sp;

    /* GetMaxLun */
    bmRequestType.Recipient = USB_BM_REQTYPE_RECIP_INTER;
    bmRequestType.Type = USB_BM_REQTYPE_TYPE_CLASS;
    bmRequestType.Direction = USB_BM_REQTYPE_DIRECTION_D2H;

    sp.bmRequestType = bmRequestType;
    sp.bRequest = 254;  // GET_MAX_LUN
    sp.wValue = 0;

    sp.wIndex = 0;//interface 0
    sp.wLength = 1;

    XUH_ControlTransfer_In(ep_out0, ep_in0, sp, buffer);

    rc = f_mount(&Fatfs, "", 0);             /* Register volume work area (never fails) for SD host interface #0 */
    if(rc)
        mountdie(rc);

    {
        FATFS *fs;
        DWORD fre_clust, fre_sect, tot_sect;

        /* Get volume information and free clusters of drive 0 */
        rc = f_getfree("0:", &fre_clust, &fs);
        if(rc) die(rc);

        /* Get total sectors and free sectors */
        tot_sect = (fs->n_fatent - 2) * fs->csize;
        fre_sect = fre_clust * fs->csize;

        /* Print free space in unit of KB (assuming 512 bytes/sector) */
        //printf("%lu KB total drive space.\n"
        //        "%lu KB available.\n",
        //        fre_sect / 2, tot_sect / 2);
    }

#if APP_XUH_MSC_ENABLE_JPEG
    const char * const jpeg_paths[] = {"TEST0.JPG", "TEST1.JPG"};
    /* The reference timer runs at 100 MHz. Unsigned subtraction handles wrap. */
    const unsigned ticks_per_second = 100000000;
    const unsigned benchmark_ticks = 5 * ticks_per_second;
    unsigned frames = 0;
    unsigned elapsed;
    g_msc_trace = 0;
    printf("Benchmarking full JPEG read/decode/render for 5 seconds...\n");
    unsigned start = get_time();
    do {
        unsigned image = frames % (sizeof(jpeg_paths) / sizeof(jpeg_paths[0]));
        const char *path = jpeg_paths[image];
        if (read_jpeg_from_msc(path, Buff, sizeof(Buff), &br)) {
            printf("Failed to read %s\n", path);
            return;
        }

        jpeg_decode_info_t info = {0};
        memset(RgbBuff, 0, sizeof(RgbBuff));
        memset(DecodeWork, 0, sizeof(DecodeWork));

        jpeg_decode_result_t result = jpeg_decode(
            Buff,
            br,
            RgbBuff,
            sizeof(RgbBuff),
            DecodeWork,
            sizeof(DecodeWork),
            JPEG_DECODE_SCALE,
            &info);

        if (result != JPEG_DECODE_OK) {
            printf("%s: decode failed, result=%d\n", path, result);
            return;
        }

#if APP_XUH_MSC_ENABLE_LCD && !APP_XUH_MSC_LCD_TEST_MODE
        send_rgb888_to_lcd(c_lcd_image, RgbBuff, info.output_width,
                           info.output_height, info.channels);
#endif
        ++frames;
        elapsed = get_time() - start;
    } while (elapsed < benchmark_ticks);

    unsigned elapsed_ms = elapsed / (ticks_per_second / 1000);
    unsigned fps_hundredths = (unsigned)(((unsigned long long)frames *
                                         ticks_per_second * 100) / elapsed);
#if APP_XUH_MSC_ENABLE_LCD && !APP_XUH_MSC_LCD_TEST_MODE
    printf("Rendered %u frames in %u.%03u seconds: %u.%02u FPS\n",
#else
    printf("Decoded %u frames in %u.%03u seconds (no LCD rendering): %u.%02u FPS\n",
#endif
           frames, elapsed_ms / 1000, elapsed_ms % 1000,
           fps_hundredths / 100, fps_hundredths % 100);

#else
    /* Trace only the text file, keeping JPEG read timing unchanged. */
    g_msc_trace = 1;
    printf("Opening TEST.TXT\n");
    rc = f_open(&Fil, "TEST.TXT", FA_READ);
    printf("TEST.TXT f_open: rc=%u\n", rc);
    if (rc) return;

    printf("Reading TEST.TXT\n");
    rc = f_read(&Fil, Buff, sizeof(Buff), &br);
    printf("TEST.TXT f_read: rc=%u bytes=%u\n", rc, br);
    FRESULT read_rc = rc;

    printf("Closing TEST.TXT\n");
    rc = f_close(&Fil);
    printf("TEST.TXT f_close: rc=%u\n", rc);
    g_msc_trace = 0;
    if (read_rc || rc) return;

    for (UINT i = 0; i < br; i++)
        printchar(Buff[i]);
    printf("\nTEST.TXT complete\n");
#endif

}
