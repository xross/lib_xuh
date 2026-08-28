
:orphan:

#######################
USB Host (XUH) Library
#######################

:Maintainer: xross
:version: 0.1.0
:Scope: xmoslabs
:Description: Low-level USB host library for XMOS XS2 based devices
:description: JPEG image encoding and decoding
:category: General purpose
:keywords:
:devices: xcore-200

*******
Summary
*******

.. warning::
    Use at your own risk. We do not accept responsibility for damage to hardware
  while using this code. This project is not officially endorsed by XMOS.

This library provides a low-level USB host interface for XMOS XS2 based devices.
It is intended for applications that need direct access to USB host endpoints and
transfer control.

The ``lib_xuh`` module provides the USB host transport layer. Applications or
higher-level class drivers are responsible for device class handling and for
assigning endpoint usage.

The USB mass storage example demonstrates mounting storage media with FatFs and
reading files from a USB mass storage device

TODO
=====

* Port to later version of fatfs
* Port to xs3
* Add assignable EP addresses (i.e. XUH has a bunch of EP resource and the higher-level can assign an address
* Doesn't deal with NYET (in reply to OUT transfer)
* Perform more than one tranfer per micro-frame
* Detect device disconnects
* Deal with FS (LS?) devices

********
Features
********

* Low-level USB host support for XMOS XS2 based devices
* Endpoint-level USB transfer interface
* Example USB mass storage application

************
Known issues
************

* Too many to list.

****************
Development repo
****************

* `lib_xuh <https://www.github.com/xmos-innovation/lib_xuh>`_ (https://www.github.com/xmos-innovation/lib_xuh)

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


