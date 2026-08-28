cmake_minimum_required(VERSION 3.21)
include($ENV{XMOS_CMAKE_PATH}/xcommon.cmake)
project(app_xuh_msc)

set(APP_HW_TARGET xk-audio-216-mc.xn)

set(APP_INCLUDES src)

set(APP_COMPILER_FLAGS -O3 -g -DDEBUG_PRINT_ENABLE=1 -report -mno-dual-issue)

include(${CMAKE_CURRENT_LIST_DIR}/../deps.cmake)
list(APPEND APP_DEPENDENT_MODULES "lib_jpeg")

set(XMOS_SANDBOX_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../)

XMOS_REGISTER_APP()
