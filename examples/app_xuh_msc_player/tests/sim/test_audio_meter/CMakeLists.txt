cmake_minimum_required(VERSION 3.21)
include($ENV{XMOS_CMAKE_PATH}/xcommon.cmake)
project(test_audio_meter)
set(APP_HW_TARGET XK-EVK-XU316)
set(XMOS_SANDBOX_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../../../../)
set(APP_DEPENDENT_MODULES "")
set(APP_INCLUDES src ../../../src)
set(APP_C_SRCS src/main.c ../../../src/audio_meter.c)

# One build configuration for each value in params.json, which the pytest test
# also reads, so the two cannot disagree. A value fixed in advance is a compiler
# definition. The seed is chosen when the test runs, so it cannot be. It reaches
# the application in either of two ways:
# - The test writes it to test_seed.h and rebuilds the configuration's target.
#   Each configuration has its own header, outside the source tree, so that test
#   cases run in parallel do not share a file.
# - The test passes it as a command-line argument, with no rebuild. This needs
#   -fcmdline-buffer-bytes, which reserves the memory the arguments are loaded
#   into; without it the application sees no arguments. The buffer must also hold
#   the executable's path, which the simulator passes in full as argv[0]: when it
#   is too small the application traps at start-up, with or without arguments.
file(READ ${CMAKE_CURRENT_LIST_DIR}/../params.json params)
string(JSON count LENGTH "${params}" peak_bits)
math(EXPR last "${count} - 1")
foreach(index RANGE ${last})
    string(JSON bits GET "${params}" peak_bits ${index})
    set(generated ${CMAKE_BINARY_DIR}/generated/bits${bits})
    if(NOT EXISTS ${generated}/test_seed.h)
        file(WRITE ${generated}/test_seed.h "#define TEST_SEED (1u)\n")
    endif()
    set(APP_COMPILER_FLAGS_bits${bits} -O2 -g -report -I${generated} -DTEST_PEAK_BITS=${bits}
        -fcmdline-buffer-bytes=1024
        -DAPP_XUH_MSC_PLAYER_ENABLE_JPEG=0 -DAPP_XUH_MSC_PLAYER_ENABLE_MPEG1=1
        -DAPP_XUH_MSC_PLAYER_MPEG_AUDIO=1)
endforeach()
XMOS_REGISTER_APP()
