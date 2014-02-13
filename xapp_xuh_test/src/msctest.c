
#include <xs1.h>
#include <print.h>
#include "gpioDefines.h"

#include "usb.h"
#include "usb_defs.h"
#include "xuh.h"
#include "stdio.h"

#include "ff.h"

#include "diskio.h"

#include "msc_support.h"

XUH_Ep g_ep_out2;
XUH_Ep g_ep_in1;
XUH_Ep g_ep_in0;
XUH_Ep g_ep_out0;

void delay(unsigned x);
void ComposeSetupBuffer(USB_SetupPacket_t *sp, unsigned char buffer[8]);

/* XUH MSC SCSI FUNCTIONS */
s_ms_cbw ms_cbw = {
    0x43425355, 0x12345678,
    0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,
};

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

    status = ms_testunitready(g_ep_out2, g_ep_in1, 0);
    status = ms_testunitready(g_ep_out2, g_ep_in1, 0);

    ms_requestsense(g_ep_out2, g_ep_in1, 0, 0, &responsecode);
    ms_inquiry(g_ep_out2, g_ep_in1, 0, &devicetype, & dataformat, &removable);
    
    ms_readcapacity(g_ep_out2, g_ep_in1, 0, 0, &lbaddr,&lnlen);

    g_capacity = lbaddr;
    g_blocksize = lnlen;
   // printintln(lbaddr);
    //printintln(lnlen);

}

int XUH_MSDSCSISectorRead(unsigned sector, unsigned char buff[])
{
    /* Issues READ 10 command block */
    
    ms_read(g_ep_out2, g_ep_in1, 0, sector, buff);
}


/* FATFS FUNCTIONS */

DSTATUS disk_initialize (
        BYTE ifNum                                /* Physical drive nmuber (0..) */
)
{
    if(!XUH_MSDSCSIMediaInitialise())
    {
        return STA_NODISK;
    }
    return RES_OK;
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
        if (!XUH_MSDSCSISectorRead(sector, buff))
        {
            return RES_NOTRDY;
        }
        sector++;
        buff += 512;
    }
    return RES_OK;
 }


DRESULT disk_write(BYTE IfNum, BYTE *buff ,DWORD sector, UINT count)
{
    printstr("write");
}
DWORD get_fattime(void)
{

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

FATFS Fatfs;
FIL   Fil;
//BYTE  fatfsbuffer[1024];
BYTE Buff[512*50];      /* File read buffer (40 SD card blocks to let multiblock operations (if file not fragmented) */

int XUH_ControlTransfer_In(XUH_Ep ep_out, XUH_Ep ep_in, USB_SetupPacket_t sp, unsigned char buffer[]);

void MassStorage(XUH_Ep ep_out0, XUH_Ep ep_in0, XUH_Ep ep_out2, XUH_Ep ep_in1)
{
    FRESULT rc;
    DIR dir;
    FILINFO fno;
    unsigned char buffer[64];
  UINT bw, br, i;

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

    f_mount(&Fatfs, "", 0);             /* Register volume work area (never fails) for SD host interface #0 */
    {
    FATFS *fs;
    DWORD fre_clust, fre_sect, tot_sect;

    /* Get volume information and free clusters of drive 0 */
    //rc = f_getfree("0:", &fre_clust, &fs);
    //if(rc) die(rc);

    /* Get total sectors and free sectors */
    //tot_sect = (fs->n_fatent - 2) * fs->csize;
    //fre_sect = fre_clust * fs->csize;
 /* Print free space in unit of KB (assuming 512 bytes/sector) */
   // printf("%lu KB total drive space.\n"
     //      "%lu KB available.\n",
      //     fre_sect / 2, tot_sect / 2);
// printf("\nOpen root directory.\n");
  //rc = f_opendir(&dir, "");
  //if(rc) die(rc);

#if 0
  //printf("\nDirectory listing...\n");
  for(;;)
  {
    rc = f_readdir(&dir, &fno);    /* Read a directory item */

if(rc || !fno.fname[0]) break; /* Error or end of dir */
    if(fno.fattrib & AM_DIR)
      printf("   <dir>  %s\n", fno.fname);
    else
    {
      printf("%8d  %s\n", fno.fsize, fno.fname);
    }
  }
  if(rc) die(rc);

   
#endif
  }
  //printf("\nOpening an existing file: Data.bin...");
  rc = f_open(&Fil, "TEST.TXT", FA_READ);
  if(rc) die(rc);
  //printf("done.\n");

  //printf("\nReading file content...");
 // T = get_time();
  rc = f_read(&Fil, Buff, sizeof(Buff), &br);
 // T = get_time() - T;
  //if(rc) die(rc);
  printf("%d bytes read. Read rate: %dKBytes/Sec\n", br, (br*100000));

  printf("\nClosing the file...");
  rc = f_close(&Fil);
  if(rc) die(rc);
  printf("done.\n");

    for (int i = 0; i < br; i++)
    printchar(Buff[i]);



}
