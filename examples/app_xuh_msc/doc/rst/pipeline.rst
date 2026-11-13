Motion JPEG display pipeline
============================

Build configurations
--------------------

The app provides named ``jpeg`` and ``mpeg1`` configurations on XU316 with the
LCD enabled. Each executable compiles only its own decoder. From the repository
root, activate XTC Tools 15.3.1 and configure a fresh build directory::

    source /Applications/XMOS_XTC_15.3.1/SetEnv.sh
    export PATH="/usr/bin:$PATH"
    cmake -G "Unix Makefiles" -S examples/app_xuh_msc -B build-video
    xmake -C build-video jpeg -j 8
    xmake -C build-video mpeg1 -j 8

Omit the config name to build both. Executables are build-local, not written over
the old single-config app image::

    xrun --xscope --xscope-io-only build-video/bin/jpeg/app_xuh_msc_jpeg.xe
    xrun --xscope --xscope-io-only build-video/bin/mpeg1/app_xuh_msc_mpeg1.xe

Both programs run indefinitely while playback is successful. Stop xrun with
Ctrl-C after collecting completed-play reports. Always use xSCOPE, not JTAG
``--io`` logging, which can halt the timing-sensitive USB task.

The existing JPEG/LCD options still apply to ``jpeg``. On the original XS2 target
or with LCD disabled/test mode enabled, only ``jpeg`` is configured; its existing
non-video modes are preserved. The first MPEG-1 integration is XU316/LCD-only.
XCommon fetches missing dependencies but does not update existing sibling repos.
Use the pinned JPEG/MPEG revisions from ``examples/deps.cmake`` and initialise
their submodules with ``git submodule update --init --recursive``.

MJPEG playback
--------------

Place ``VIDEO.MJP`` at the root of a FAT-formatted USB memory stick. The default
JPEG/LCD configuration loops the entire raw MJPEG file at maximum speed.
After every play it drains pending frames, reports the play number, completed-frame
FPS and stage timings, resets per-play counters and reopens the file.
Workers stay alive and USB, FatFs and the LCD are initialised only once.
File/decode errors stop playback after pending work drains.
It does not pace playback, skip frames or play audio. Raw MJPEG
has no presentation timestamps. AVI and other containers are not supported.

Input and display
-----------------

Use baseline JPEG frames at 320 x 240 with a maximum compressed frame size of
30 KiB. Smaller images are centred; larger decoded images must fit the output
buffer or decoding fails. The decoder uses full scale. The ST7789V is configured
for 320 x 240 landscape addressing (MADCTL 0x60); view the display rotated 90
degrees from its portrait orientation. Verify rotation and colour order on the
physical panel.

Example conversion and preview on a Mac with FFmpeg installed::

    ffmpeg -i source.mp4 -t 30 -an \
      -vf "fps=10,scale=320:240,setsar=1" \
      -c:v mjpeg -pix_fmt yuvj420p -q:v 5 -f mjpeg VIDEO.MJP
    ffplay -f mjpeg -framerate 10 VIDEO.MJP

This stretches the complete picture to the LCD dimensions without cropping its
width. ``makemjpg.sh`` provides the source-specific conversion with a three-second
intro and removal of embedded letterboxing; its input media are not included.

The buffered reader opens the file once and performs sequential 4096-byte reads.
It follows JPEG segment lengths (so embedded marker-like bytes in metadata do
not terminate a frame), entropy byte stuffing and restart markers. It handles
markers crossing disk-read boundaries and rejects malformed, truncated and
oversized frames. Read/decode errors stop production and drain prior valid work.

Task placement and ownership
----------------------------

* Tile 1: USB manager, file loader, JPEG decoder and pixel transmitter.
* Tile 0: pixel receiver/RGB565 converter, LCD renderer and SPI master.

Compressed input has two 30 KiB slots. The decoder uses one 230400-byte RGB888
buffer, waiting for the transmitter to release it before the next decode. This
keeps full-screen playback within SRAM; unlike the earlier 200 x 200 benchmark,
decode and RGB transmission do not overlap. File loading and LCD output can
still overlap decoding. Tile 0 has two 153600-byte RGB565 framebuffers, allowing
receive/conversion to overlap LCD rendering.

Ready messages carry a slot, monotonically increasing sequence and frame metadata.
Acknowledgements explicitly return ownership, and provide bounded backpressure
without polling or dropping frames. Pointers stay on their owning tile; pixel
bytes cross tiles. Every channel message ends with CT_END, and C ownership
handoffs include compiler memory barriers. Stop messages propagate only after
pending work is complete. A frame is counted after its LCD transaction finishes.
The end-of-play marker resets each worker's sequence and metrics rather than
terminating the worker. The next play starts only after the completed statistics
have returned to the loader.

The large memory model is required. On XU316 the build uses approximately
321 KiB on tile 0 and 345 KiB on tile 1, including code, data and stacks.

Measurements and verification
------------------------------

The first 320 x 240 test clip completed 450 frames in 50.040 s (8.99 FPS), with
loaded and displayed counts matching and zero sequence errors. Average stage
times were load 13.2 ms, decode 95.3 ms, transmit/wait 13.1 ms, receive/convert
13.1 ms and LCD SPI 41.9 ms. These overlapping stage times must not be summed.
A clip encoded at 15 FPS therefore plays slower than real time with this build.

Elapsed time accumulates unsigned reference-timer deltas after each frame into
a 64-bit total, supporting clips longer than the 32-bit timer's wrap period.
Each individual frame operation must complete within one timer period (42.9 s).
Stage totals accumulate microseconds, rather than raw timer ticks. No routine
per-frame logging occurs. The report is printed after EOF and pipeline drain.

Development builds may enable PIPELINE_VERIFY=1 to hash each RGB buffer before
and after transmission and each LCD buffer before and after rendering. Video
frames are not compared against previous frames, since their contents change.
Leave this disabled for performance measurements.

The native reader test checks split markers, metadata containing EOI bytes,
stuffed entropy bytes, restart markers, every truncation point, buffer limits
and disk errors. From the lib_xuh root, with a native sanitizer-capable compiler::

    cc -std=c99 -g -fsanitize=address,undefined \
      -Iexamples/app_xuh_msc/src -I../lib_fatfs/lib_fatfs/api \
      examples/app_xuh_msc/tests/mjpeg_reader_test.c \
      examples/app_xuh_msc/src/mjpeg_reader.c -o /tmp/mjpeg-reader-test
    /tmp/mjpeg-reader-test

MPEG-1 playback
---------------

Place ``VIDEO.M1V`` at the root of the FAT-formatted USB stick and run the
``mpeg1`` executable. Input must be fixed-resolution, progressive 320 x 240
MPEG-1 YUV420 **elementary video**, not an MPG/AVI container. The default PL_MPEG
backend supports I, P and B pictures. The encoded frame rate is ignored for this
initial implementation: every frame is decoded and sent to the LCD as fast as
possible, with no pacing, intentional skipping or audio.

The USB manager remains a separate task on tile 1. The FatFs task services
decoder requests using one 4096-byte tile-local input chunk. The decoder copies
each feed into its 32 KiB input buffer in the 384 KiB static arena; the chunk can
then be reused. Neither MPEG file reads nor display backpressure run inside the
USB manager. This first implementation does not prefetch filesystem reads during
decoder execution.

The decoder/transmitter is one additional tile-1 task. It sends each picture as
two-row YUV420 strips to tile 0, using the library's plane strides. It waits for
staging acknowledgement before another decode can invalidate those views. Tile 0
converts a 960-byte strip into big-endian RGB565 in one of the existing two
153600-byte LCD framebuffers; it does not allocate a full RGB888 picture. The
scalar conversion follows PL_MPEG's limited-range BT.601 coefficients. Reception
of the next frame and SPI rendering of the previous frame overlap. Raw channel
transactions close with CT_END and ownership handoffs include memory barriers.

At EOF the decoder drains delayed reference pictures, closes and checks that
arena allocations were released. The receiver drains the last LCD transaction
before completed statistics are returned. The next play reopens ``VIDEO.M1V``
with fresh counters; all pipeline workers remain alive. Read/decode errors stop
playback after draining earlier valid frames.

xSCOPE reports decoded/displayed counts, errors, decoder status, compressed
bytes read, arena peak/live-after-close, startup latency and completed-frame FPS.
Stage means cover load, feed, decode, transmit/wait, receive/convert and LCD SPI.
These overlap and must not be summed. Elapsed reference-timer deltas accumulate
into a 64-bit total; each operation must complete within one 32-bit timer period.

The first integrated XTC 15.3.1 build reports 321844 bytes on tile 0 and
467812 bytes on tile 1, leaving 56476 bytes on tile 1. The tile-1 figure includes
a provisional 16 KiB decoder-task stack reservation because upstream callback
paths prevent complete automatic sizing. Linker constraints pass with caveats;
this reservation is not a proven production stack bound.

Preparing a video-only file
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

From the sibling ``lib_mpeg`` repository::

    bash tools/makempeg1.sh \
      ../lib_xuh/examples/app_xuh_msc/data/crysis/source.mp4 \
      ../lib_xuh/examples/app_xuh_msc/data/crysis/crysis_meme.jpg \
      ../lib_xuh/examples/app_xuh_msc/data/VIDEO.M1V

This uses a three-second intro followed by the source from seven seconds onward,
stretches the complete active width to 320 x 240 and removes source-specific
letterboxing. The reference file has 3025 frames at 25 FPS. Copy it to the USB
stick yourself; the application does not use the host-side ``data/`` directory.

Colour and pipeline tests
~~~~~~~~~~~~~~~~~~~~~~~~~

The native strip-conversion test compares 163840 pixels against pristine
PL_MPEG's RGB888 conversion followed by RGB565 packing, including clamping and
chroma extremes. From the repository root::

    mkdir -p build-video
    cc -std=c99 -O1 -g -fsanitize=address,undefined -fno-sanitize=shift -fwrapv \
      -Iexamples/app_xuh_msc/src -I../lib_mpeg/third_party/pl_mpeg \
      examples/app_xuh_msc/tests/yuv_rgb565_test.c \
      examples/app_xuh_msc/src/yuv_rgb565.c -o build-video/yuv_rgb565_test
    build-video/yuv_rgb565_test

The finite simulator fixture substitutes host-file input and a checksum renderer
for USB and physical SPI. It exercises the production MPEG decoder/receiver
tasks, cross-tile strip transfer, native-reference RGB565 hashes, repeated EOF
drain/restart and open/read/empty-file error recovery. It does not validate USB,
the physical display or end-to-end hardware throughput. With XTC active::

    python3 -m venv .venv
    source .venv/bin/activate
    cmake -S ../lib_mpeg -B build-mpeg-reference \
      -DMPEG_BACKEND=pl_mpeg -DMPEG_SANITIZE=OFF -DCMAKE_BUILD_TYPE=Release
    cmake --build build-mpeg-reference -j 8
    cmake -G "Unix Makefiles" \
      -S examples/app_xuh_msc/tests/app_mpeg_pipeline -B build-mpeg-pipeline
    xmake -C build-mpeg-pipeline -j 8
    python examples/app_xuh_msc/tests/run_mpeg_pipeline.py \
      --bench build-mpeg-reference/mpeg_bench \
      --xe build-mpeg-pipeline/bin/test_mpeg_pipeline.xe \
      --work build-mpeg-pipeline/fixtures

The runner requires normal termination and all seven completed test cases.
Its cycle and 240-second host timeout bounds are failures, not functional passes.
The default smoke fixture is one full-screen I picture per successful play.
For I/P/B pipeline coverage set ``MPEG_PIPELINE_TEST_FRAMES=3`` when configuring
the test and pass ``--frames 3`` to the runner. The initial three-picture repeat
fixture exceeded the host timeout; that run is not a pass. Independent library
reference tests already exercise I/P/B decoding and final-picture drain.
