
#include <xs1.h>
#include <print.h>
#include <xcore/chanend.h>
#include "gpioDefines.h"

#include "usb_std_requests.h"
#include "usb_defs.h"
#include "xuh.h"
#include "stdio.h"

#if APP_XUH_MSC_ENABLE_JPEG
#include "bmp_writer.h"
#include "jpeg.h"
#endif

#include "ff.h"

#include "timing.h"

unsigned int get_time(void);
#include "diskio.h"

#include "msc_support.h"

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

int ms_transport(XUH_Ep ep_out, XUH_Ep ep_in,
    unsigned length, int dir, int lun, unsigned cblen, char cb[], // Command stage
    char data[], // Data Stage
    int *dataResidue, int *status)
{
    unsigned char cbw_com[64];
    unsigned char buffer[64];

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
    XUH_OutTransfer(ep_out, cbw_com, 31);

    if(dir)
    {
        XUH_InTransfer(ep_in, data);

    }
    //if(dir)
    //{
    // Status
        XUH_InTransfer(ep_in, buffer);

    //}
    status = buffer[12];
}


int ms_readcapacity(XUH_Ep ep_out, XUH_Ep ep_in, int lun, int lba, int *lbaddr, int *lblen)
{
    char cb[10] = {0x25, 0x0, 0x0, 0x0, 0x0, 0x0 ,0,0,0,0,0};
    int residue, status;
    unsigned char data[64];
    int retval;

    cb[2] = (lba >> 24) & 0xFF;
    cb[3] = (lba >> 16) & 0xFF;
    cb[4] = (lba >> 8) & 0xFF;
    cb[5] = (lba >> 0) & 0xFF;

    retval = ms_transport(ep_out, ep_in, 8, 1, lun, 10, cb, data, residue, status);



    *lbaddr = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    *lblen = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];

    return status;
}

int ms_requestsense(XUH_Ep ep_out, XUH_Ep ep_in, int lun, int desc, int *responsecode)
{
    char cb[6] = {0x03, 0, 0, 0, 36, 0};
    unsigned data[5];
    int residue, status;
    int retval;

    cb[1] = desc & 1;

    retval = ms_transport(ep_out, ep_in, 18, 1, lun, 6, cb, (unsigned) data, residue, status);

    responsecode = (data[0] & 0x7f) * (data[0] >> 7);

    return status;

}

int ms_testunitready(XUH_Ep ep_out, XUH_Ep ep_in, int lun)
{
    char cb[6] = {0,0,0,0,0,0};
    unsigned data[5];
    int residue, status;

    int retval;

    retval = ms_transport(ep_out, ep_in, 0, 0, lun, 6, cb, data, residue, status);
    return status;
}

int ms_inquiry(XUH_Ep ep_out, XUH_Ep ep_in, int lun, int *deviceType, int *removable, int *dataformat)
{
  char cb[6] = {0x12, 0x0, 0x0, 0x0, 36, 0x0};
  int residue, status;
  char data[36];
  int retval;

  retval = ms_transport(ep_out, ep_in, 36, 1, lun, 6, cb, data, residue, status);

  deviceType = data[0];
  removable = (data[1] >> 7) & 1;
  dataformat = data[3] & 0xF;

  return status;
}

int ms_read(XUH_Ep ep_out, XUH_Ep ep_in, int lun, int lba, unsigned char buff[])
{

    char cb[9] = {0x28, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x0};
    int residue, status;
    int retval;

    cb[2] = (lba >> 24) & 0xFF;
    cb[3] = (lba >> 16) & 0xFF;
    cb[4] = (lba >> 8) & 0xFF;
    cb[5] = (lba >> 0) & 0xFF;

    retval = ms_transport(ep_out, ep_in, 512, 1, lun, 9, cb, buff, residue, status);

    return status;
}

unsigned g_capacity;
unsigned g_blocksize;

int XUH_MSDSCSIMediaInitialise()
{
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

    ms_readcapacity(g_ep_out2, g_ep_in1, 0, 0, &lbaddr,&lnlen);

    g_capacity = lbaddr;
    g_blocksize = lnlen;

    return 0;
}

int XUH_MSDSCSISectorRead(unsigned sector, unsigned char buff[])
{
    /* Issues READ 10 command block */
    ms_read(g_ep_out2, g_ep_in1, 0, sector, buff);
    return 0;
}


/* FATFS FUNCTIONS */

DSTATUS disk_initialize (
        BYTE ifNum                                /* Physical drive nmuber (0..) */
)
{
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
        DWORD sector,           /* Sector address (LBA) */
        UINT count              /* Number of sectors to read (1..255) */
)
{
    int nSector;

   // printstr("read ifNum:");
    //printint(ifNum); printstr(" sector: "), printint(sector); printstr(" count: "); printintln(count);
    if(ifNum != 0)
    {
        printstr("ifNum out of Range");
        return RES_PARERR;
    }
    for(nSector = 0; nSector < count; nSector++)
    {
        if (XUH_MSDSCSISectorRead(sector, buff))
        {
            return RES_NOTRDY;
        }
        sector++;
        buff += 512;
    }
    return RES_OK;
 }


//DRESULT disk_write (BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
DRESULT disk_write(BYTE IfNum, BYTE *buff ,DWORD sector, UINT count)
{
    printstr("write");
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
        return 0;
    //printstr("status");
   // return STA_NOINIT;
}

DRESULT disk_ioctl (BYTE IfNum, BYTE ctrl, void* Buff)
{

    switch(ctrl)
    {
        case GET_SECTOR_SIZE:

            *(DWORD*)Buff = 512;
            printint(512);
            return RES_OK;

        default:
            printstr("ioctl: ");
            printintln(IfNum);
            printintln(ctrl);
            break;
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

    printf("\nOpening %s...", path);

    rc = f_open(&Fil, path, FA_READ);
    if (rc)
    {
        printf("failed rc=%u\n", rc);
        return 1;
    }

    rc = f_read(&Fil, dst, dst_size, bytes_read);

    printintln(dst_size);
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

    printf("%u bytes read.\n", *bytes_read);
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
    rc = f_open(&Fil, "TEST0.JPG", FA_READ);
    if (rc)
    {
        printf("failed rc=%u\n", rc);
    }

    rc = f_read(&Fil, Buff, sizeof(Buff), &br);

    if (rc) {
        printf("read failed rc=%u\n", rc);
        f_close(&Fil);
    }

    rc = f_close(&Fil);
    if (rc) {
        printf("close failed rc=%u\n", rc);
        return;
    }

    if (br == 0 || br == sizeof(Buff)) {
        printf("invalid size %u bytes\n", br);
        return;
    }

    printf("%u bytes read.\n", br);

   jpeg_decode_info_t info;

    memset(RgbBuff, 0, sizeof(RgbBuff));
    memset(DecodeWork, 0, sizeof(DecodeWork));

    printf("Decoding JPEG...\n");
    jpeg_decode_result_t result = jpeg_decode(
        Buff,
        br,
        RgbBuff,
        sizeof(RgbBuff),
        DecodeWork,
        sizeof(DecodeWork),
        JPEG_DECODE_SCALE,
        &info);

    printf("Decode result = %d\n", result);
    printf("Image = %u x %u\n", info.width, info.height);
    printf("Output = %u x %u x %u\n", info.output_width, info.output_height, info.channels);
    printf("Output bytes = %lu\n", (unsigned long)info.output_size);

    if (result != JPEG_DECODE_OK) {
        return;
    }

#if APP_XUH_MSC_ENABLE_LCD && !APP_XUH_MSC_LCD_TEST_MODE
    send_rgb888_to_lcd(c_lcd_image, RgbBuff, info.output_width,
                       info.output_height, info.channels);
    printf("Displayed JPEG\n");
#endif

#else
    rc = f_open(&Fil, "TEST.TXT", FA_READ);
    if(rc) die(rc);

    rc = f_read(&Fil, Buff, sizeof(Buff), &br);
    if(rc) die(rc);
    printf("%d bytes read. Read rate: %dKBytes/Sec\n", br, (br*100000));

    rc = f_close(&Fil);
    if(rc) die(rc);

    for (int i = 0; i < br; i++)
        printchar(Buff[i]);
#endif

}
