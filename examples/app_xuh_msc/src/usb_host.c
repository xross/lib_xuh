/* Basic USB mass storage example: enumerate the attached device, mount its FAT
 * volume, list the root directory and print the first text file in it. */
#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "xuh_host.h"
#include "xuh_msc.h"
#include "usb_host.h"

static FATFS fs;
static FIL file;
/* FatFs reads whole sectors straight into this, so it must be word aligned
 * and a multiple of the sector size. */
static BYTE buffer[512] __attribute__((aligned(4)));

static void print_device(const XUH_Device *device)
{
    printf("USB device %04x:%04x, class 0x%02x, %u interface(s)\n",
           device->vendorId, device->productId, device->deviceClass,
           device->interfaceCount);
    for (unsigned i = 0; i < device->interfaceCount; i++) {
        const XUH_InterfaceInfo *interface = &device->interfaces[i];
        printf("  Interface %u: class 0x%02x subclass 0x%02x protocol 0x%02x\n",
               interface->number, interface->classCode, interface->subClass,
               interface->protocol);
        for (unsigned e = 0; e < interface->endpointCount; e++)
            printf("    Endpoint 0x%02x: attributes 0x%02x, max packet %u\n",
                   interface->endpoints[e].address, interface->endpoints[e].attributes,
                   interface->endpoints[e].maxPacketSize);
    }
}

static int is_text_file(const FILINFO *info)
{
    const char *extension = strrchr(info->fname, '.');
    return !(info->fattrib & AM_DIR) && extension && !strcmp(extension, ".TXT");
}

/* List the root directory. Copies the name of the first text file to
 * `text_file`, which is left empty if there is none. */
static FRESULT list_root(char text_file[], unsigned size)
{
    DIR directory;
    FILINFO info;
    FRESULT result = f_opendir(&directory, "/");

    text_file[0] = 0;
    if (result != FR_OK) return result;
    printf("Root directory:\n");
    while ((result = f_readdir(&directory, &info)) == FR_OK && info.fname[0]) {
        if (info.fattrib & AM_DIR)
            printf("  %-12s  <DIR>\n", info.fname);
        else
            printf("  %-12s  %lu bytes\n", info.fname, (unsigned long)info.fsize);
        if (!text_file[0] && is_text_file(&info) && strlen(info.fname) < size)
            strcpy(text_file, info.fname);
    }
    f_closedir(&directory);
    return result;
}

static FRESULT print_file(const char *name)
{
    UINT count;
    FRESULT result = f_open(&file, name, FA_READ);

    if (result != FR_OK) return result;
    printf("%s:\n", name);
    while ((result = f_read(&file, buffer, sizeof(buffer), &count)) == FR_OK && count)
        printf("%.*s", (int)count, (const char *)buffer);
    printf("\n");
    f_close(&file);
    return result;
}

static void use_volume(void)
{
    char text_file[13]; /* 8.3 name */
    FRESULT result = f_mount(&fs, "", 1);

    if (result == FR_OK) result = list_root(text_file, sizeof(text_file));
    if (result == FR_OK) {
        if (text_file[0])
            result = print_file(text_file);
        else
            printf("No .TXT file in the root directory\n");
    }
    if (result != FR_OK) printf("FatFs error %d\n", result);
}

void UsbHost(chanend c_out[], unsigned n_out, chanend c_in[], unsigned n_in)
{
    static XUH_Host host;
    static XUH_Device device;
    static XUH_Msc msc;
    XUH_Status status;

    /* Slot 0 in each direction is the control endpoint; the rest are for
     * class drivers. */
    XUH_Host_Init(&host, c_out[0], c_in[0], &c_out[1], n_out - 1, &c_in[1], n_in - 1);

    printf("Waiting for a USB device\n");
    status = XUH_Host_WaitForDevice(&host, &device);
    if (status != XUH_OK) {
        printf("Enumeration failed, status %d\n", status);
    } else {
        print_device(&device);

        status = XUH_Msc_Open(&msc, &host, &device);
        if (status == XUH_OK) {
            /* A mass storage device: it is now FatFs drive 0. */
            printf("Mass storage: %u blocks of %u bytes\n",
                   (unsigned)msc.blockCount, (unsigned)msc.blockSize);
            use_volume();
        } else if (status == XUH_ERR_UNSUPPORTED) {
            printf("Not a mass storage device\n");
        } else {
            printf("Mass storage open failed, status %d\n", status);
        }
    }
    printf("Done\n");

    while (1);
}
