################################
USB Host (XUH) Library for xcore
################################

***********
Description
***********

This USB Host (XUH) library provides a low-level interface to USB.

**********
Disclaimer
**********

Use at your own risk. We do not accept responsibility of any damage to hardware
while using this code.

This is not officially endorsed by XMOS.

********
Building
********

Uses the XMOS make based xcommon build-system.

****
Todo
****

* Port to later version of fatfs
* Port to xs2/xs3 and modern tools
* Port to xcommon-cmake build-system
* Add assignable EP addresses (i.e. XUH has a bunch of EP resource and the higher-level can assign an address
* Doesn't deal with NYET (in reply to OUT transfer)
* Only performs on tranfer per micro-frame
* Detect device disconnects
* Deal with FS (LS?) devices

*******
Support
*******

Please use the issue tracker

********************************
Required software (dependencies)
********************************

* The example app needs git@github.com:xross/lib_fatfs.git

