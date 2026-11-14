lib_xuh change log
==================

UNRELEASED
----------

  * ADDED: ``app_xuh_msc_bench`` example: raw block and FatFs file read rates of
    a mass storage device
  * CHANGED: The player's ``mpeg1_audio`` configuration plays
    ``VIDEO_320x180.MPG`` by default
  * REMOVED: The player's ``TEST.TXT`` read and its JPEG decode benchmark, with
    the single-image LCD path and BMP writer only they used, and the
    ``APP_XUH_MSC_PLAYER_ENABLE_LCD`` option: the player always has a display
  * ADDED: Stereo audio level meter in the top-right of the player's display,
    in the ``mpeg1_audio`` configuration
  * CHANGED: The player FPS gauge uses the MPEG sequence-header rate for its
    target tick; measured playback throughput remains separate
  * ADDED: Centred smaller MPEG pictures in the storage example, with a fixed
    screen-position FPS gauge and configurable long input filenames
  * ADDED: Host core (``xuh_host.h``): ``XUH_Host_WaitForDevice()`` enumerates
    the attached device and reports its interfaces and endpoints;
    ``XUH_Host_ControlIn()`` and ``XUH_Host_ControlNoData()`` control transfers,
    with a multiple-packet data stage; ``XUH_Host_ClaimEndpoint()``;
    an activity callback, ``XUH_Host_SetActivityCallback()``.
  * ADDED: Mass storage class driver (``xuh_msc.h``), moved from the
    ``app_xuh_msc_player`` example, and optional FatFs disk functions selected with
    ``XUH_MSC_FATFS``.
  * ADDED: ``xuh_config.h`` defaults, overridden by an optional application
    ``xuh_conf.h``.
  * CHANGED: The ``app_xuh_msc_player`` example uses the host core and mass storage
    driver; its ``USBHost()`` takes all of the transport's channels.
  * CHANGED: The video player example is renamed from ``app_xuh_msc`` to
    ``app_xuh_msc_player``, and its ``APP_XUH_MSC_*`` options to
    ``APP_XUH_MSC_PLAYER_*``.
  * ADDED: Basic ``app_xuh_msc`` example: lists the root directory of a mass
    storage device and prints a text file.
  * FIXED: SET_CONFIGURATION uses the configuration value of the device rather
    than 1; GET_MAX_LUN names the mass storage interface rather than 0.
  * REMOVED: ``XUH_ControlTransfer_In()``, ``XUH_SetAddress()``,
    ``XUH_GetDescriptor()`` and ``XUH_GetStringDescriptor()``, which had no
    public declaration; the unused ``USBH_*`` duplicates; ``usbh.h``,
    ``usbh_descriptors.h`` and ``usbh_class_msc.h``.
