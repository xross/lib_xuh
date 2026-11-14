
:orphan:

#######################
USB Host (XUH) Library
#######################

:maintainer: xross
:version: 0.1.0
:scope: Experimental
:description: USB host transport, enumeration and mass storage for XMOS devices
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

The ``lib_xuh`` module provides the USB host transport, a host core that
enumerates the attached device and describes it to the application, and a
read-only mass storage class driver with optional FatFs disk functions. Usage
is described in `the library guide <doc/rst/lib_xuh.rst>`_.

The USB mass storage example demonstrates mounting storage media with FatFs and
reading files from a USB mass storage device. The example supports XK-EVK-XU316
targets, defaults to XK-EVK-XU316-AIV and assumes VBUS is hardwired. The library's
XS2 transport support is separate from the XU316-only example application.

The XU316 mass storage app provides ``jpeg`` (``VIDEO.MJP``), ``mpeg1``
(``VIDEO.M1V`` elementary video) and ``mpeg1_audio`` (``VIDEO.MPG`` program stream)
build configurations. The program-stream player decodes and discards MP2 audio;
it does not provide audio output or A/V synchronisation. Playback runs at maximum
speed and reports completed-play metrics through xSCOPE. Build commands, pipeline
details and limitations are in
`the display pipeline guide <examples/app_xuh_msc/doc/rst/pipeline.rst>`_.

ILI9341 / ST7789V display
=========================

The XU316 mass storage example supports ILI9341 and ST7789V 240 by 320 LCDs,
used in 320 by 240 landscape orientation. ILI9341 is the default. Select ST7789V
with ``-DAPP_XUH_MSC_LCD_CONTROLLER=ST7789V`` when configuring CMake, or ILI9341 with
``-DAPP_XUH_MSC_LCD_CONTROLLER=ILI9341``. The controller choice applies to all
three video build configurations.

JPEG RGB888 pixels or MPEG YUV420 strips cross from tile 1 to tile 0 and are
converted into tile-0 RGB565 framebuffers. Smaller JPEG images are centred with
black borders; MPEG playback requires 320 by 240. The complete framebuffer is
assembled before the SPI transaction starts, and chip select remains asserted
from the address-window commands through all pixel data for each update.

Set ``APP_XUH_MSC_ENABLE_LCD=OFF`` when configuring with CMake to disable the
display. Set ``APP_XUH_MSC_LCD_TEST_MODE=ON`` to replace JPEG rendering with a
32 by 32 moving-box test. Each animation step updates only the 33 by 32 region
covering the old and new box positions.

The LCD is connected to tile 0 using the following ports and device pins:

.. list-table:: ILI9341 / ST7789V connections (unchanged between controllers)
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

MISO is not used. The display uses SPI mode 0 at 50 MHz. Connect the LCD and
XU316 grounds together and consult the XK-EVK-XU316-AIV carrier schematic for
the physical connector positions of the listed device pins.

********
Features
********

* Low-level USB host support for XMOS XS2 and XS3 based devices
* Endpoint-level USB transfer interface
* Device enumeration, reporting the interfaces and endpoints of the device
* Read-only mass storage class driver, with optional FatFs disk functions
* Example USB mass storage video player application
* Support for mass storage devices with filesystem access (provided by FatFs)

************
Known issues
************

* USB support is experimental. Transfers can block indefinitely; disconnect
  detection, timeouts and recovery are incomplete.
* NYET responses to OUT transfers are not handled. Full-speed and low-speed
  devices are not supported by the example.
* The transport performs at most one transaction per microframe.
* One device is enumerated, after reset. A device attached later, or
  re-attached, is not detected.
* Mass storage is read only, uses logical unit 0 and requires 512-byte blocks.
* The player has no pacing, audio output or A/V synchronisation. See the pipeline
  guide for media, memory and validation limits.
* No support for USB hubs or OTG. Mass storage is the only class driver; other
  classes can use the host core and transport directly.
* Support for high-speed devices only

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

The library depends on ``lib_xud``. ``lib_fatfs`` is needed only by applications
that set ``XUH_MSC_FATFS``. The mass-storage example also uses
``lib_logging`` and ``lib_fatfs``, plus ``lib_jpeg``, ``lib_mpeg`` and ``lib_spi``
for its selected playback configuration. Versions are pinned in
``lib_xuh/lib_build_info.cmake`` and ``examples/deps.cmake``.

*************************
Related application notes
*************************

* None

*******
Support
*******

This is an experimental community project, not an officially supported XMOS
package. Report issues through GitHub
`issues <https://github.com/xross/lib_xuh/issues>`_.
