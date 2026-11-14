lib_xuh change log
==================

UNRELEASED
----------

  * ADDED: Host core (``xuh_host.h``): ``XUH_Host_WaitForDevice()`` enumerates
    the attached device and reports its interfaces and endpoints;
    ``XUH_Host_ControlIn()`` and ``XUH_Host_ControlNoData()`` control transfers,
    with a multiple-packet data stage; ``XUH_Host_ClaimEndpoint()``;
    an activity callback, ``XUH_Host_SetActivityCallback()``.
  * ADDED: Mass storage class driver (``xuh_msc.h``), moved from the
    ``app_xuh_player`` example, and optional FatFs disk functions selected with
    ``XUH_MSC_FATFS``.
  * ADDED: ``xuh_config.h`` defaults, overridden by an optional application
    ``xuh_conf.h``.
  * CHANGED: The ``app_xuh_player`` example uses the host core and mass storage
    driver; its ``USBHost()`` takes all of the transport's channels.
  * CHANGED: The video player example is renamed from ``app_xuh_msc`` to
    ``app_xuh_player``, and its ``APP_XUH_MSC_*`` options to
    ``APP_XUH_PLAYER_*``.
  * FIXED: SET_CONFIGURATION uses the configuration value of the device rather
    than 1; GET_MAX_LUN names the mass storage interface rather than 0.
  * REMOVED: ``XUH_ControlTransfer_In()``, ``XUH_SetAddress()``,
    ``XUH_GetDescriptor()`` and ``XUH_GetStringDescriptor()``, which had no
    public declaration; the unused ``USBH_*`` duplicates; ``usbh.h``,
    ``usbh_descriptors.h`` and ``usbh_class_msc.h``.
