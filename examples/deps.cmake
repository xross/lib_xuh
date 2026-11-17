# Common dependencies for examples. Use explicit owners for non-XMOS modules.
set(APP_DEPENDENT_MODULES   "lib_logging(3.3.2)"
                            "lib_xuh"
                            "https://github.com/xross/lib_fatfs(7793c9c5a7df6ed184f785568bc7a9a86b234d0b)"
)
set(XUH_JPEG_DEPENDENCY "git@github.com:xmos/lib_jpeg(b515df327d287e3444e8d3cc01642d65d90691ae)")
set(XUH_SPI_DEPENDENCY "lib_spi(4.0.0)")
set(XUH_MPEG_DEPENDENCY "git@github.com:xross/lib_mpeg(196d6930a10724b150d65d6e983f424ce01658a9)")
