
##########################
lib_xuh: USB host library
##########################

************
Introduction
************

``lib_xuh`` is a USB host library for XMOS XS2 and XS3 devices. It provides three
layers:

* the USB host transport: the host controller task and single-packet transfers
  (``xuh.h``);
* the host core, which waits for a device, enumerates it and describes its
  interfaces and endpoints to the application (``xuh_host.h``);
* a read-only mass storage class driver (``xuh_msc.h``), with optional FatFs
  disk functions.

The library is experimental. Its limits are listed in the "Known issues" section
of ``README.rst``.

*****
Usage
*****

``lib_xuh`` is used with `XCommon CMake <https://www.xmos.com/file/xcommon-cmake-documentation/?version=latest>`_.
Include it in the application's ``APP_DEPENDENT_MODULES``:

.. code-block:: cmake

    set(APP_DEPENDENT_MODULES "lib_xuh")

Tasks
=====

The application runs the host controller, ``XUH_Manager()``, on its own logical
core on the USB tile, and a host task of its own on the same tile. They are
connected by an array of channels in each direction. Slot 0 of each array is the
default control endpoint; the others are for class drivers.

.. code-block:: xc

    #define NUM_EPS 3

    int main()
    {
        chan c_out[NUM_EPS], c_in[NUM_EPS];
        par {
            on tile[1]: XUH_Manager(c_out, NUM_EPS, c_in, NUM_EPS);
            on tile[1]: UsbHost(c_out, NUM_EPS, c_in, NUM_EPS);
        }
        return 0;
    }

The USB tile is set with ``USB_TILE`` in the application's ``xud_conf.h``. The
host core and class driver functions are C functions. Call them from the one
host task: an endpoint handle is only valid on the tile of ``XUH_Manager()``.

Enumerating a device
====================

The host task owns an ``XUH_Host`` and decides what to do with the device that
is found:

.. code-block:: c

    void UsbHost(chanend c_out[], unsigned n_out, chanend c_in[], unsigned n_in)
    {
        XUH_Host host;
        XUH_Device device;
        XUH_Msc msc;

        XUH_Host_Init(&host, c_out[0], c_in[0], &c_out[1], n_out - 1, &c_in[1], n_in - 1);
        if (XUH_Host_WaitForDevice(&host, &device) == XUH_OK) {
            if (XUH_Msc_Open(&msc, &host, &device) == XUH_OK) {
                /* Mass storage: FatFs drive 0. f_mount(), f_open() ... */
            }
        }
    }

``XUH_Host_WaitForDevice()`` blocks until a device is attached. It then reads
the device and configuration descriptors, gives the device an address, selects
its first configuration and returns its identifiers and every interface and
endpoint in an ``XUH_Device``. It does not interpret the device class.

One device is enumerated, after reset: a second call returns
``XUH_ERR_UNSUPPORTED``.

Every call returns an ``XUH_Status``. The codes for timeouts, stalls and
disconnection are reserved until the transport can report them; until then a
transfer to a device that stops responding blocks.

An application that handles a device class itself can find an interface with
``XUH_Device_FindInterface()``, bind channel slots to its endpoints with
``XUH_Host_ClaimEndpoint()`` and use the transfers in ``xuh.h``, and
``XUH_Host_ControlIn()`` and ``XUH_Host_ControlNoData()`` for control requests.

Mass storage
============

``XUH_Msc_Open()`` returns ``XUH_ERR_UNSUPPORTED`` for a device that is not mass
storage, so it is also the test for one. On success ``blockCount`` and
``blockSize`` of the ``XUH_Msc`` give the capacity, and ``XUH_Msc_Read()`` reads
blocks.

The driver is read only, uses logical unit 0 and requires 512-byte blocks.

FatFs
=====

To use FatFs, set ``XUH_MSC_FATFS`` to 1 in an application ``xuh_conf.h`` and add
``lib_fatfs`` to the application's dependencies:

.. code-block:: c

    #define XUH_MSC_FATFS 1

``lib_xuh`` then provides the FatFs disk functions, and ``get_fattime()``, for
the device most recently opened with ``XUH_Msc_Open()``, which is physical drive
0. ``lib_xuh`` does not depend on, or call, FatFs: the application mounts and
uses the volume.

.. code-block:: c

    static FATFS fs;

    if (XUH_Msc_Open(&msc, &host, &device) == XUH_OK) {
        f_mount(&fs, "", 1);
        /* f_open(), f_read() ... */
    }

Activity callback
=================

The library reports what it is doing on the bus to a callback registered with
``XUH_Host_SetActivityCallback()``, for example to drive an activity LED. Each
activity (``XUH_ACTIVITY_ENUMERATE``, ``XUH_ACTIVITY_MSC_OPEN``,
``XUH_ACTIVITY_MSC_READ``) is followed by ``XUH_ACTIVITY_IDLE`` or
``XUH_ACTIVITY_ERROR`` when it ends.

.. code-block:: c

    XUH_ACTIVITY_CALLBACK static void usb_activity(XUH_Activity activity)
    {
        /* ... */
    }

    XUH_Host_SetActivityCallback(&host, usb_activity);

The callback runs in the host task and must not call back into the library.
``XUH_ACTIVITY_CALLBACK`` lets the tools account for its stack.

Examples
========

``examples/app_xuh_msc`` is a complete, minimal application of the calls above:
it prints the description of the attached device, mounts a mass storage volume,
lists the root directory and prints the first ``.TXT`` file in it.

``examples/app_xuh_player`` is a video player that reads MJPEG or MPEG-1 files
from mass storage; see its own guide in ``examples/app_xuh_player/doc``.

Configuration
=============

Options and their defaults are in ``xuh_config.h``. Override them in an optional
application ``xuh_conf.h``.

.. list-table::
   :header-rows: 1
   :widths: 40 10 50

   * - Option
     - Default
     - Purpose
   * - ``XUH_MAX_INTERFACES``
     - 4
     - Interface descriptors recorded for a device
   * - ``XUH_MAX_ENDPOINTS_PER_INTERFACE``
     - 4
     - Endpoint descriptors recorded for an interface
   * - ``XUH_MAX_DATA_EPS``
     - 3
     - Non-control channel slots in each direction
   * - ``XUH_CONFIG_DESC_MAX``
     - 512
     - Largest configuration descriptor, in bytes
   * - ``XUH_DEVICE_ADDRESS``
     - 3
     - Address given to the device
   * - ``XUH_MSC_FATFS``
     - 0
     - Provide the FatFs disk functions
   * - ``XUH_MSC_TRACE``
     - 0
     - Print each mass storage command
