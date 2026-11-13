# Common dependencies for examples. Use explicit owners for non-XMOS modules.
set(APP_DEPENDENT_MODULES   "lib_logging(3.3.2)"
                            "lib_xuh"
                            "https://github.com/xross/lib_fatfs(cc154623a90b710caa2a80c8955600f9e00b61dc)"
)
set(XUH_JPEG_DEPENDENCY "git@github.com:xmos/lib_jpeg(44fcc11c8a8d64f0cdf65dfeb584cd20f3476a1c)")
set(XUH_SPI_DEPENDENCY "lib_spi(4.0.0)")
