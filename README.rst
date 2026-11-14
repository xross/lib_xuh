
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
read-only mass storage class driver with optional FatFs disk functions.

There are two examples for XK-EVK-XU316: ``app_xuh_msc``, a basic mass storage
example that lists a directory and prints a text file, and ``app_xuh_player``, a
video player that reads from USB mass storage and renders to an SPI LCD. Both
are described in the library guide.

********
Features
********

* Low-level USB host support for XMOS XS2 and XS3 based devices
* Endpoint-level USB transfer interface
* Device enumeration, reporting the interfaces and endpoints of the device
* Read-only mass storage class driver, with optional FatFs disk functions
* Basic USB mass storage example
* Example USB mass storage video player application
* Support for mass storage devices with filesystem access (provided by FatFs)

************
Known issues
************

* USB support is experimental. Transfers can block indefinitely; disconnect
  detection, timeouts and recovery are incomplete.
* NYET responses to OUT transfers are not handled. Full-speed and low-speed
  devices are not supported.
* The transport performs at most one transaction per microframe.
* One device is enumerated, after reset. A device attached later, or
  re-attached, is not detected.
* Mass storage is read only, uses logical unit 0 and requires 512-byte blocks.
* The player example has no pacing, audio output or A/V synchronisation. See the
  library guide for its media, memory and validation limits.
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
that set ``XUH_MSC_FATFS``, as both examples do. The video player also uses
``lib_logging``, plus ``lib_jpeg``, ``lib_mpeg`` and ``lib_spi``
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
