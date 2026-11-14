cmake_minimum_required(VERSION 3.21)
include($ENV{XMOS_CMAKE_PATH}/xcommon.cmake)
project(app_xuh_msc)

set(APP_HW_TARGET src/XK-EVK-XU316-AIV.xn)
set(APP_INCLUDES src)

include(${CMAKE_CURRENT_LIST_DIR}/../deps.cmake)
set(XMOS_SANDBOX_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../)

set(APP_COMPILER_FLAGS -O3 -g -report -mno-dual-issue -fxscope)

XMOS_REGISTER_APP()
