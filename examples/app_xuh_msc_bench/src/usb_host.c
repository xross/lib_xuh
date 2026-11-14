/* USB mass storage read benchmark. Measures the read rate of the attached
 * device two ways: raw blocks through the class driver, and a file through
 * FatFs, each with several transfer sizes. Nothing is written to the device. */
#include <stdio.h>
#include <string.h>
#include <xcore/hwtimer.h>

#include "ff.h"
#include "xuh_host.h"
#include "xuh_msc.h"
#include "usb_host.h"

enum { MAX_BLOCKS = 64,                         /* Largest transfer: 32 KiB */
       TEST_BYTES = 4 * 1024 * 1024,            /* Read by each measurement */
       TICKS_PER_MS = 100000 };                 /* 100 MHz reference clock */

/* Whole blocks are received straight into this, so it must be word aligned. */
static BYTE buffer[MAX_BLOCKS * XUH_MSC_BLOCK_BYTES] __attribute__((aligned(4)));
static FATFS fs;
static FIL file;

/* Reference time wraps every 42.9 seconds: sum the time of each call. */
static void report(const char *what, unsigned unit_bytes, unsigned long long bytes,
                   unsigned long long ticks)
{
    unsigned milliseconds = (unsigned)(ticks / TICKS_PER_MS);
    unsigned rate = milliseconds ? (unsigned)(bytes * 1000 / 1024 / milliseconds) : 0;
    printf("  %s %5u bytes at a time: %7u KiB in %5u ms = %5u KiB/s\n",
           what, unit_bytes, (unsigned)(bytes / 1024), milliseconds, rate);
}

/* Consecutive blocks from the start of the device, `blocks` per command. */
static void raw_read(XUH_Msc *msc, unsigned blocks)
{
    unsigned total = TEST_BYTES / XUH_MSC_BLOCK_BYTES;
    unsigned long long ticks = 0;
    unsigned done = 0;

    if (total > msc->blockCount) total = msc->blockCount;
    total -= total % blocks;
    for (; done < total; done += blocks) {
        unsigned start = get_reference_time();
        XUH_Status status = XUH_Msc_Read(msc, done, blocks, buffer);
        ticks += get_reference_time() - start;
        if (status != XUH_OK) {
            printf("  Raw read failed at block %u, status %d\n", done, status);
            return;
        }
    }
    report("Raw ", blocks * XUH_MSC_BLOCK_BYTES, (unsigned long long)done * XUH_MSC_BLOCK_BYTES, ticks);
}

/* The largest file in the root directory, so that the test is long enough. */
static FRESULT largest_file(char name[], unsigned size, FSIZE_t *bytes)
{
    DIR directory;
    FILINFO info;
    FRESULT result = f_opendir(&directory, "/");

    name[0] = 0;
    *bytes = 0;
    if (result != FR_OK) return result;
    while ((result = f_readdir(&directory, &info)) == FR_OK && info.fname[0]) {
        if (!(info.fattrib & AM_DIR) && info.fsize > *bytes && strlen(info.fname) < size) {
            strcpy(name, info.fname);
            *bytes = info.fsize;
        }
    }
    f_closedir(&directory);
    return result;
}

/* The first TEST_BYTES of a file, `unit` bytes per f_read(). */
static void file_read(const char *name, unsigned unit)
{
    unsigned long long ticks = 0, bytes = 0;
    UINT count = unit;
    FRESULT result = f_open(&file, name, FA_READ);

    while (result == FR_OK && count == unit && bytes < TEST_BYTES) {
        unsigned start = get_reference_time();
        result = f_read(&file, buffer, unit, &count);
        ticks += get_reference_time() - start;
        bytes += count;
    }
    f_close(&file);
    if (result != FR_OK)
        printf("  File read failed, FatFs error %d\n", result);
    else
        report("File", unit, bytes, ticks);
}

static void benchmark(XUH_Msc *msc)
{
    static const unsigned raw_blocks[] = {1, 8, MAX_BLOCKS};
    static const unsigned file_units[] = {512, 4096, sizeof(buffer)};
    char name[13]; /* 8.3 name */
    FSIZE_t bytes;

    printf("Raw block reads (XUH_Msc_Read):\n");
    for (unsigned i = 0; i < sizeof(raw_blocks) / sizeof(raw_blocks[0]); i++)
        raw_read(msc, raw_blocks[i]);

    FRESULT result = f_mount(&fs, "", 1);
    if (result == FR_OK) result = largest_file(name, sizeof(name), &bytes);
    if (result != FR_OK) {
        printf("FatFs error %d\n", result);
    } else if (!name[0]) {
        printf("No file in the root directory\n");
    } else {
        printf("File reads (FatFs f_read) of %s, %lu bytes:\n", name, (unsigned long)bytes);
        for (unsigned i = 0; i < sizeof(file_units) / sizeof(file_units[0]); i++)
            file_read(name, file_units[i]);
    }
}

void UsbHost(chanend c_out[], unsigned n_out, chanend c_in[], unsigned n_in)
{
    static XUH_Host host;
    static XUH_Device device;
    static XUH_Msc msc;
    XUH_Status status;

    XUH_Host_Init(&host, c_out[0], c_in[0], &c_out[1], n_out - 1, &c_in[1], n_in - 1);

    printf("Waiting for a USB device\n");
    status = XUH_Host_WaitForDevice(&host, &device);
    if (status == XUH_OK) status = XUH_Msc_Open(&msc, &host, &device);
    if (status == XUH_OK) {
        printf("Mass storage %04x:%04x, %u blocks of %u bytes\n", device.vendorId,
               device.productId, (unsigned)msc.blockCount, (unsigned)msc.blockSize);
        benchmark(&msc);
    } else if (status == XUH_ERR_UNSUPPORTED) {
        printf("Not a mass storage device\n");
    } else {
        printf("Failed, status %d\n", status);
    }
    printf("Done\n");

    while (1);
}
