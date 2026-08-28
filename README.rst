
:orphan:

#######################
USB Host (XUH) Library
#######################

:maintainer: xross
:version: 0.1.0
:scope: xmoslabs
:description: Low-level USB host transport for XMOS devices
:category: General purpose
:keywords:
:devices: xcore-200, xcore.ai

*******
Summary
*******

.. warning::

    Use at your own risk. We do not accept responsibility for damage to hardware
    while using this code. This project is not officially endorsed by XMOS.

This library provides a low-level USB host interface for XMOS XS2 and XS3 based devices.
It is intended for applications that need direct access to USB host endpoints and
transfer control.

The ``lib_xuh`` module provides the USB host transport layer. Applications or
higher-level class drivers are responsible for device class handling and for
assigning endpoint usage.

The USB mass storage example demonstrates mounting storage media with FatFs and
reading files from a USB mass storage device. It builds for the XK-EVK-XU316-AIV
target by default and can also be built for the original XS2 target by setting
``APP_HW_TARGET=xk-audio-216-mc.xn`` when configuring with CMake. The original
XS2 board enables USB switch GPIO control in the app; the XU316 target assumes
VBUS is hardwired.

ST7789V display
===============

The XU316 mass storage example displays the decoded JPEG on an ST7789V 240 by
320 LCD. RGB888 image data is streamed from tile 1 to the tile 0 LCD task and
converted into a tile 0 RGB565 framebuffer. Images smaller than the display are
centred with black borders. Larger images are centre-cropped to 240 by 320. The
complete framebuffer is assembled before the SPI transaction starts, and chip
select remains asserted from the address-window commands through all pixel data
for each update.

Set ``APP_XUH_MSC_ENABLE_LCD=OFF`` when configuring with CMake to disable the
display. Set ``APP_XUH_MSC_LCD_TEST_MODE=ON`` to replace JPEG rendering with a
32 by 32 moving-box test. Each animation step updates only the 33 by 32 region
covering the old and new box positions.

The LCD is connected to tile 0 using the following ports and device pins:

.. list-table:: ST7789V connections
   :header-rows: 1
   :widths: 15 15 20 15

   * - LCD signal
     - Tile
     - xcore port
     - Device pin
   * - SCLK
     - 0
     - ``XS1_PORT_1L``
     - ``X0D35``
   * - MOSI
     - 0
     - ``XS1_PORT_1M``
     - ``X0D36``
   * - CS
     - 0
     - ``XS1_PORT_1N``
     - ``X0D37``
   * - DC
     - 0
     - ``XS1_PORT_1O``
     - ``X0D38``
   * - RST
     - 0
     - ``XS1_PORT_1P``
     - ``X0D39``

MISO is not used. The display uses SPI mode 0 at 30 MHz. Connect the LCD and
XU316 grounds together and consult the XK-EVK-XU316-AIV carrier schematic for
the physical connector positions of the listed device pins.

TODO
=====

* Port to later version of fatfs
* Add assignable EP addresses (i.e. XUH has a bunch of EP resource and the higher-level can assign an address
* Doesn't deal with NYET (in reply to OUT transfer)
* Perform more than one tranfer per micro-frame
* Detect device disconnects
* Deal with FS (LS?) devices
* Share code with lib_xud (or merge with lib_xud)
    * lib_xud becomes "XMOS USB Driver" instead of "XMOS USB_Device"

********
Features
********

* Low-level USB host support for XMOS XS2 and XS3 based devices
* Endpoint-level USB transfer interface
* Example USB mass storage application

************
Known issues
************

* Too many to list.

****************
Development repo
****************

* `lib_xuh <https://www.github.com/xross/lib_xuh>`_ (https://www.github.com/xross/lib_xuh)

**************
Required tools
**************

* XMOS XTC Tools: 15.3.1

*********************************
Required libraries (dependencies)
*********************************

* None

*************************
Related application notes
*************************

* None

*******
Support
*******

This package is supported by XMOS Ltd. Issues can be raised against the software using GitHub `issues <https://github.com/xross/lib_xuh/issues>`_.
