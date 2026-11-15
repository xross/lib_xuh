cmake_minimum_required(VERSION 3.21)
include($ENV{XMOS_CMAKE_PATH}/xcommon.cmake)
project(test_audio_pipeline)
set(XMOS_SANDBOX_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../../../)
set(APP_HW_TARGET XK-EVK-XU316)
set(APP_INCLUDES src ../../src)
set(APP_C_SRCS src/test.c ../../src/audio_pipeline.c ../../src/decode_lead.c)
set(APP_XC_SRCS src/main.xc)
set(APP_COMPILER_FLAGS -O3 -g -report -mcmodel=large -mno-dual-issue
    -DAPP_XUH_MSC_PLAYER_ENABLE_JPEG=0 -DAPP_XUH_MSC_PLAYER_ENABLE_MPEG1=1
    -DAPP_XUH_MSC_PLAYER_LCD_TEST_MODE=0 -DAPP_XUH_MSC_PLAYER_MPEG_AUDIO=1
    -DAPP_XUH_MSC_PLAYER_AUDIO_METER=0)
# The player's mpeg_conf.h (in ../../src) selects S32; OFF overrides it.
option(TEST_AUDIO_S32 "Exercise the player's S32 PCM path (OFF tests float)" ON)
if(NOT TEST_AUDIO_S32)
    list(APPEND APP_COMPILER_FLAGS -DMPEG_AUDIO_FORMAT=0)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/../../../deps.cmake)
set(APP_DEPENDENT_MODULES "${XUH_MPEG_DEPENDENCY}")
set(MPEG_BACKEND pl_mpeg)
XMOS_REGISTER_APP()
set_target_properties(${APP_BUILD_TARGETS} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
