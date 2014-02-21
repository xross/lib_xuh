# XMOS USB Host (XUH) Library

## Description

This USB Host (XUH) xmod provides a low-level interface to USB. 
Currently private and intended to be distributed in library form (see xlib_xud)
until permission is granted to open-source by XMOS.

# Disclaimer

Use at your own risk. We do not accept responsibility of any damage to hardware
while using this code.

This is not officially endorsed by XMOS.

# Building 

Uses the XMOS build-system.

## Todo

* Add assignable EP addresses (i.e. XUH has a bunch of EP resource and the higher-level can assign an address
* Doesn't deal with NYET (in reply to OUT transfer)
* Only performs on tranfer per micro-frame
* Detect device disconnects
* Deal with FS (LS?) devices

## Support

Please use the issue tracker

## Required software (dependencies)
