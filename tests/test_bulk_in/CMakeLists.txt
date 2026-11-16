cmake_minimum_required(VERSION 3.21)
project(test_bulk_in)
set(XMOS_SANDBOX_DIR ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(APP_HW_TARGET test_xs3_600.xn)
set(APP_DEPENDENT_MODULES lib_xuh)
# The host's assembly entry points use the single-issue calling convention.
set(APP_COMPILER_FLAGS -O3 -g -mno-dual-issue -DUSB_TILE=tile[0] -DXUD_CORE_CLOCK=600
    -DXUD_SIM_XSIM=1 -DXUH_SIM_CONNECT_DEBOUNCE_US=100)
set(APP_PCA_ENABLE ON)
XMOS_REGISTER_APP()
set_target_properties(${APP_BUILD_TARGETS} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
