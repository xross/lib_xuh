cmake_minimum_required(VERSION 3.21)
include($ENV{XMOS_CMAKE_PATH}/xcommon.cmake)
project(app_xuh_msc)

set(APP_HW_TARGET src/XK-EVK-XU316-AIV.xn CACHE STRING "XN target for app_xuh_msc")

set(APP_INCLUDES src)

set(APP_COMPILER_FLAGS -O3 -g -DDEBUG_PRINT_ENABLE=1 -report -mno-dual-issue -fxscope -mcmodel=large)
set(APP_XSCOPE_SRCS config.xscope)

include(${CMAKE_CURRENT_LIST_DIR}/../deps.cmake)

if(APP_HW_TARGET MATCHES "xk-audio-216")
    list(APPEND APP_COMPILER_FLAGS -DAPP_XUH_MSC_HAS_USB_SWITCH_GPIO=1)
else()
    list(APPEND APP_COMPILER_FLAGS -DAPP_XUH_MSC_HAS_USB_SWITCH_GPIO=0)
endif()

if(APP_HW_TARGET MATCHES "XK-EVK-XU316")
    set(APP_XUH_MSC_ENABLE_JPEG_DEFAULT ON)
else()
    set(APP_XUH_MSC_ENABLE_JPEG_DEFAULT OFF)
endif()

set(APP_XUH_MSC_ENABLE_JPEG ${APP_XUH_MSC_ENABLE_JPEG_DEFAULT} CACHE BOOL "Enable JPEG decode in app_xuh_msc")

if(APP_XUH_MSC_ENABLE_JPEG)
    list(APPEND APP_DEPENDENT_MODULES "${XUH_JPEG_DEPENDENCY}")
    list(APPEND APP_COMPILER_FLAGS -DAPP_XUH_MSC_ENABLE_JPEG=1)
else()
    list(APPEND APP_COMPILER_FLAGS -DAPP_XUH_MSC_ENABLE_JPEG=0)
endif()

if(APP_HW_TARGET MATCHES "XK-EVK-XU316")
    set(APP_XUH_MSC_ENABLE_LCD_DEFAULT ON)
else()
    set(APP_XUH_MSC_ENABLE_LCD_DEFAULT OFF)
endif()

set(APP_XUH_MSC_ENABLE_LCD ${APP_XUH_MSC_ENABLE_LCD_DEFAULT} CACHE BOOL "Enable the ST7789V display")
set(APP_XUH_MSC_LCD_TEST_MODE OFF CACHE BOOL "Display the moving-box LCD test instead of the decoded JPEG")

if(APP_XUH_MSC_ENABLE_LCD)
    list(APPEND APP_DEPENDENT_MODULES "${XUH_SPI_DEPENDENCY}")
    list(APPEND APP_COMPILER_FLAGS -DAPP_XUH_MSC_ENABLE_LCD=1)
else()
    list(APPEND APP_COMPILER_FLAGS -DAPP_XUH_MSC_ENABLE_LCD=0)
endif()

if(APP_XUH_MSC_LCD_TEST_MODE)
    list(APPEND APP_COMPILER_FLAGS -DAPP_XUH_MSC_LCD_TEST_MODE=1)
else()
    list(APPEND APP_COMPILER_FLAGS -DAPP_XUH_MSC_LCD_TEST_MODE=0)
endif()

set(XMOS_SANDBOX_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../)

# Per-pixel video loops: these options are appended after the application flags,
# re-enabling dual issue for these two files only.
set(APP_COMPILER_FLAGS_yuv_rgb565.c -mdual-issue)
set(APP_COMPILER_FLAGS_mpeg_pipeline.c -mdual-issue)

set(APP_COMPILER_FLAGS_jpeg ${APP_COMPILER_FLAGS} -DAPP_XUH_MSC_ENABLE_MPEG1=0)
if(APP_HW_TARGET MATCHES "XK-EVK-XU316" AND APP_XUH_MSC_ENABLE_LCD AND NOT APP_XUH_MSC_LCD_TEST_MODE)
    set(MPEG_BACKEND pl_mpeg CACHE STRING "Backend for the MPEG-1 player")
    if(NOT MPEG_BACKEND STREQUAL "pl_mpeg")
        message(FATAL_ERROR "The mpeg1 configuration requires MPEG_BACKEND=pl_mpeg")
    endif()
    list(APPEND APP_DEPENDENT_MODULES "${XUH_MPEG_DEPENDENCY}")
    set(APP_COMPILER_FLAGS_mpeg1 ${APP_COMPILER_FLAGS})
    list(FILTER APP_COMPILER_FLAGS_mpeg1 EXCLUDE REGEX "^-DAPP_XUH_MSC_ENABLE_JPEG=")
    list(APPEND APP_COMPILER_FLAGS_mpeg1 -DAPP_XUH_MSC_ENABLE_JPEG=0 -DAPP_XUH_MSC_ENABLE_MPEG1=1)
endif()

XMOS_REGISTER_APP()

# XTC 15.3.1 resolves modules for all configurations together. Keep each
# decoder's sources out of the other executable, not merely linker-discarded.
foreach(target ${APP_BUILD_TARGETS})
    get_target_property(sources ${target} SOURCES)
    if(target STREQUAL "app_xuh_msc_jpeg")
        list(FILTER sources EXCLUDE REGEX "/lib_mpeg/lib_mpeg/")
        set(configuration jpeg)
    else()
        list(FILTER sources EXCLUDE REGEX "/lib_jpeg/lib_jpeg/")
        set(configuration mpeg1)
    endif()
    set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${configuration}")
endforeach()
