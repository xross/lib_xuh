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

ILI9341 is the default LCD controller. Both controllers use the same pins and
320 x 240 landscape RGB565 output. To retain a separate ST7789V build::

    cmake -G "Unix Makefiles" -S examples/app_xuh_msc -B build-video-st7789v \
      -DAPP_XUH_MSC_LCD_CONTROLLER=ST7789V
    xmake -C build-video-st7789v -j 8

Use ``-DAPP_XUH_MSC_LCD_CONTROLLER=ILI9341`` to select the new panel explicitly.
The controller selection is independent of the ``jpeg`` / ``mpeg1`` decoder
configuration. For initial panel bring-up, add
``-DAPP_XUH_MSC_LCD_TEST_MODE=ON`` in a separate build directory and build
``jpeg`` to run the moving-box test without decoding video.

At startup the LCD task reports the build-selected controller once alongside
the other xSCOPE debug output, for example::

    LCD: ILI9341, 320x240 landscape, SPI 30000 kHz

ST7789V builds report ``ST7789V`` instead. This is the configured controller,
not a hardware-detected model; the LCD connection does not use MISO.

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
buffer or decoding fails. The decoder uses full scale. Both controllers use
320 x 240 landscape addressing: ILI9341 uses MADCTL 0x28 with inversion off;
ST7789V retains MADCTL 0x60 with inversion on. View the display rotated 90 degrees
from its portrait orientation. Verify rotation and colour order on the physical
panel; panel-specific wiring can affect these settings.

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

That build compiled the JPEG decoder single issue and at ``-Os``: XCommon CMake
applies the application's ``-mno-dual-issue`` to library sources, and ``lib_jpeg``
selected ``-Os``. With ``lib_jpeg`` requesting dual issue explicitly and building
its decoder file at ``-O3``, a 1210-frame, 320 x 240 clip measured:

.. list-table:: Mean MJPEG stage times per frame on hardware (ms)
   :header-rows: 1

   * - lib_jpeg build
     - Load
     - Decode
     - Transmit/wait
     - Receive/convert
     - LCD SPI
     - FPS
   * - Single issue, ``-Os``
     - 11.7
     - 103.9
     - 14.4
     - 14.3
     - 41.9
     - 8.45
   * - Dual issue, ``-Os``
     - 11.7
     - 91.9
     - 13.5
     - 13.1
     - 41.9
     - 9.48
   * - Dual issue, decoder at ``-O3``
     - 11.7
     - 82.6
     - 13.5
     - 13.1
     - 41.9
     - 10.40

All three played every frame with zero errors. This needs a ``lib_jpeg`` revision
that contains the flag change (xmos/lib_jpeg pull request 19); the revision
pinned in ``examples/deps.cmake`` predates it.

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

Shared loader
-------------

Both players use one storage-independent loader, ``src/video_loader.c``. A
*source* supplies units through open/read/close callbacks: one complete JPEG
frame for MJPEG, or one chunk of at most 4096 bytes for MPEG-1. The FatFs-backed
sources are in ``src/msctest.c``; the simulator test supplies stdio-backed ones.

The loader fills the alternate input slot while the decoder still owns the
current one, waits for the previous unit's acknowledgement, then publishes
``[slot, sequence, bytes]``. End of input, or an open/read/close failure, is a
stop message carrying an error word; the decoder then drains and returns one
statistics block, whose first words are common to both decoders. A decoder that
fails acknowledges with a failure flag and refuses further units until the stop
arrives, so both sides always finish a play in step. The loader also prints the
completed-frame FPS, startup latency and common stage means.

Frame-rate overlay
------------------

Both players draw the measured frame rate, for example ``17.1 FPS``, in white on
a black box at the top left of each frame (``src/fps_overlay.c``). The rate is
the number of frames staged for display over windows of about half a second,
measured on tile 0; nothing is drawn until the first window completes. Drawing
costs about 0.1 FPS. Build with ``-DAPP_XUH_MSC_FPS_OVERLAY=0`` to leave frames
untouched; the simulator pipeline test does this because it compares frame
hashes with a reference.

MPEG-1 playback
---------------

Place ``VIDEO.M1V`` at the root of the FAT-formatted USB stick and run the
``mpeg1`` executable. Input must be fixed-resolution, progressive 320 x 240
MPEG-1 YUV420 **elementary video**, not an MPG/AVI container. The default PL_MPEG
backend supports I, P and B pictures. The encoded frame rate is ignored for this
initial implementation: every frame is decoded and sent to the LCD as fast as
possible, with no pacing, intentional skipping or audio.

The USB manager remains a separate task on tile 1. The FatFs task runs the
read-ahead loader described under `Shared loader`_, with two 4096-byte
tile-local input chunks: while the decoder owns one, the next is read from the
stick. The decoder copies a chunk into its 32 KiB input buffer in the 384 KiB
static arena, in several feeds if the buffer is nearly full, and releases the
chunk when it next asks for input. Releasing it any earlier would block the
decoder behind the loader's read in progress. Neither MPEG file reads nor
display backpressure run inside the USB manager.

The decoder/transmitter is one additional tile-1 task. It sends each picture as
two-row YUV420 strips to tile 0, using the library's plane strides. It waits for
staging acknowledgement before another decode can invalidate those views. Tile 0
converts a 960-byte strip into big-endian RGB565 in one of the existing two
153600-byte LCD framebuffers; it does not allocate a full RGB888 picture.
Reception of the next frame and SPI rendering of the previous frame overlap. Raw
channel transactions close with CT_END and ownership handoffs include memory
barriers.

The conversion is ``lib_mpeg``'s ``mpeg_yuv420_rows_to_rgb565()``
(``mpeg_rgb565.h``), called on tile 0 for each strip. It follows PL_MPEG's
limited-range BT.601 coefficients and is bit-exact with its scalar arithmetic.
Luma scaling and each channel's clamp, RGB565 field placement and byte order are
tabulated (about 6 KB of tables on tile 0, built on first use), and two pixels
are written per 32-bit store. It is not VPU-vectorised. The application is built
with ``-mno-dual-issue``; ``lib_mpeg`` requests dual issue for its own sources,
and ``mpeg_pipeline.c``, which holds the per-word transfer loops, re-enables it
through a file-specific compiler flag.

At EOF the decoder drains delayed reference pictures, closes and checks that
arena allocations were released. The receiver drains the last LCD transaction
before completed statistics are returned. The next play reopens ``VIDEO.M1V``
with fresh counters; all pipeline workers remain alive. Read/decode errors stop
playback after draining earlier valid frames.

xSCOPE reports decoded/displayed counts, errors, decoder status, compressed
bytes read, arena peak/live-after-close, startup latency and completed-frame FPS.
Stage means cover load, decode, transmit/wait, receive/convert and LCD SPI, and
a second line gives the decoder's feed time and its wait for the loader.
Elapsed reference-timer deltas accumulate into a 64-bit total; each operation
must complete within one 32-bit timer period.

Wait for loader, feed, decode and transmit/wait run one after another in the
decoder task on tile 1, so their sum is the frame period. Load now overlaps
decoding and only costs the frame period what appears as wait for loader.
Receive/convert runs on tile 0 during transmit/wait, and LCD SPI overlaps the
following frame's decode; neither adds to the frame period while it is shorter
than that sum.

MPEG-1 measurements
~~~~~~~~~~~~~~~~~~~

The ``mpeg1`` build measured here used 330336 bytes (322.6 KiB) on tile 0 and 478948
bytes (467.7 KiB) on tile 1 of 512 KiB each, including code, data and stacks.
Tile 1 has 45340 bytes remaining. These totals include the provisional decoder
stack reservation and retain the linker callback-analysis caveats.

On XK-EVK-XU316 hardware the 3025-frame, 320 x 240 reference file (25 FPS, GOP
12, two B pictures, qscale 6; 13570971 bytes) played in 165.878 s, **18.23 FPS**,
on each of two complete consecutive plays on 2026-11-13. Both
plays decoded and displayed 3025 frames with zero errors, and play 3 reopened
successfully. The FPS overlay was enabled. This is below the file's encoded
rate. The decoder arena peaked at 379288 bytes and had zero live allocations
after each close.

The fresh stage means were decode 36858 us, transmit/wait 16718 us,
receive/convert 13407 us and LCD SPI 41908 us. Loader reads averaged 4350 us,
overlapping decoding; feed averaged 97 us. The decoder's loader wait averaged
1602 us on play 1 and 1144 us on play 2. The first decoder wait includes startup
before the loader begins playback, so use the second play for steady-state
stage accounting: wait + feed + decode + transmit totals about 54.8 ms/frame.
This is a whole-file average, not a worst-picture-time bound.

In the table, "Load/wait" is the loader's read time for the first four rows,
where the decoder waited for every read, and the decoder's wait for the loader
once reads overlap decoding. The read-ahead row's 1.6 ms is from a first play,
which includes the wait while the USB device starts up; the last row is a
second play.

.. list-table:: Mean MPEG-1 stage times per frame on hardware (ms)
   :header-rows: 1

   * - Build
     - Load/wait
     - Feed
     - Decode
     - Transmit/wait
     - Receive/convert
     - LCD SPI
     - FPS
   * - Initial scalar conversion
     - 4.3
     - 0.1
     - 49.4
     - 45.3
     - 45.3
     - 41.9
     - 10.08
   * - Tabulated conversion
     - 4.3
     - 0.1
     - 49.4
     - 20.5
     - 19.1
     - 41.9
     - 13.45
   * - Plus aligned word transfer and dual issue
     - 4.3
     - 0.1
     - 49.4
     - 15.7
     - 13.5
     - 41.9
     - 14.38
   * - Plus decoder compiled dual issue
     - 4.3
     - 0.1
     - 40.6
     - 16.2
     - 13.5
     - 41.9
     - 16.32
   * - Plus read-ahead loader and FPS overlay
     - 1.6
     - 0.1
     - 40.6
     - 16.6
     - 13.4
     - 41.9
     - 17.09
   * - Plus PL_MPEG fork decode optimisations
     - 1.1
     - 0.1
     - 36.9
     - 16.7
     - 13.4
     - 41.9
     - 18.23

The decoder had been compiled single issue: XCommon CMake applies the
application's ``-mno-dual-issue`` to library sources as well, so ``lib_mpeg`` now
requests dual issue explicitly. Without the overlay the read-ahead row measured
17.16 FPS. The last row uses the ``lib_mpeg`` PL_MPEG fork, whose output is
identical to upstream PL_MPEG.

Decode is now about two thirds of the 54.9 ms frame period and is the limiting
stage.
The decoder shares tile 1 with only the USB manager and FatFs tasks, so it
already has a full logical core. The LCD write is at the wire rate for the
30 MHz SPI clock and would limit playback at about 24 FPS. These figures are for
one file on one board; other content will decode at different rates.

The first integrated XTC 15.3.1 build reports 321844 bytes on tile 0 and
467812 bytes on tile 1, leaving 56476 bytes on tile 1. The tile-1 figure includes
a provisional 16 KiB decoder-task stack reservation because upstream callback
paths prevent complete automatic sizing. Linker constraints pass with caveats;
this reservation is not a proven production stack bound.

Preparing a video-only file
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``makempeg1.sh``, beside ``makemjpg.sh`` in the application directory, builds the
demo movie. From ``examples/app_xuh_msc``::

    bash makempeg1.sh data/crysis/source.mp4 data/crysis/crysis_meme.jpg \
      data/VIDEO.M1V

This uses a three-second intro followed by the source from seven seconds onward,
stretches the complete active width to 320 x 240 and removes source-specific
letterboxing. The reference file has 3025 frames at 25 FPS. Copy it to the USB
stick yourself; the application does not use the host-side ``data/`` directory.

Colour and pipeline tests
~~~~~~~~~~~~~~~~~~~~~~~~~

The colour conversion is tested in ``lib_mpeg`` (``tests/rgb565_test.c``, run by
its ``ctest`` suite): all 16777216 Y/Cb/Cr combinations and a padded,
odd-height frame are compared with unmodified upstream PL_MPEG's RGB888
conversion truncated to RGB565.

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
The seven plays took about 15 minutes of host time when last run; pass a larger
``--timeout`` (for example 2400) where the default is not enough.
The default smoke fixture is one full-screen I picture per successful play.
For I/P/B pipeline coverage set ``MPEG_PIPELINE_TEST_FRAMES=3`` when configuring
the test and pass ``--frames 3`` to the runner. The initial three-picture repeat
fixture exceeded the host timeout; that run is not a pass. Independent library
reference tests already exercise I/P/B decoding and final-picture drain.
