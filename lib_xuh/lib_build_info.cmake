set(LIB_NAME lib_xuh)

set(LIB_VERSION 0.1.0)

set(LIB_INCLUDES api src)

set(LIB_DEPENDENT_MODULES "lib_xud(8375c51efc1d0c8c403e624eff0197880f7b8795)")

set(LIB_COMPILER_FLAGS  -O3
                        -Wall
                        -Wextra
                        -Wsign-compare
                        -Wconversion)

XMOS_REGISTER_MODULE()
