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

    LCD: ILI9341, 320x240 landscape, SPI 50000 kHz

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

Activity LED
------------

On the AIV target, tile 1 port 32A bit 0 (green) and bit 1 (red) drive the
bi-colour LED. The effect is enabled by default for this target only:

* Idle: dim green breathing with a three-second period.
* USB: green pulses around media initialisation and each multi-sector disk read.
* Decode: a subdued red undertone around JPEG/MPEG decode calls, excluding
  channel/LCD waits. Completed frames trigger a full-red accent: 40 ms bright,
  followed by a 60-ms crossfade back to the activity mix. Green is briefly
  suppressed so the brighter green die cannot wash out the red.
* Accents are limited to four per second, with a visible gap even at high FPS;
  each requires a newly completed frame, rather than a free-running blink.
* Overlapping activity between accents mixes into yellow/orange shades.
* A reported disk, file or video-pipeline error overrides activity with two
  80-ms red flashes every second, latched until reset. A blocked USB operation
  remains green/busy; this is not a USB timeout or a hardware watchdog.

The LED task runs on tile 1, uses one additional logical core and hardware timer,
and sleeps between at most three edges of each 1-kHz PWM period. Brightness is
updated every 5 ms, with a fast attack, approximately 160-ms fade from full
brightness and integer gamma correction. Each producer publishes a single
tile-local word; there is no LED channel handshake or lock in the playback path.
Short operations and completed frames have independent generation counters,
so starting the next decode cannot erase the last frame-completion event.
The JPEG configuration uses five tile-1 tasks with the LED; MPEG-1 uses four.

The outputs assume active-high LEDs, keep other port-32A output bits low, and
must not share ownership of that port with another task. Other enabled,
narrower ports retain their hardware pin precedence. Check board wiring before
enabling this on another target. For active-low LEDs, configure with
``-DAPP_XUH_MSC_LED_ACTIVE_LOW=ON``.

For uninstrumented benchmarks, configure with
``-DAPP_XUH_MSC_ACTIVITY_LED=OFF``. This removes the LED task and compiles out
the activity hooks; it does not claim zero overhead when the effect is enabled.
The portable effect/PWM test can be run from the repository root::

    cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
      -Iexamples/app_xuh_msc/src examples/app_xuh_msc/src/activity_led.c \
      examples/app_xuh_msc/tests/activity_led_test.c -o /tmp/activity_led_test
    /tmp/activity_led_test

This checks breathing, colour independence/mixing, fade-out, short operations,
completed-frame accents and rate limiting, latched fault timing, generation
wrap and all 65536 PWM duty combinations.
It does not validate physical LED polarity, PWM jitter or playback FPS on hardware.

A bounded two-task simulator fixture exercises the real timer, port and mailbox
code. With XTC Tools active, from the repository root::

    xcc -target=XK-EVK-XU316 -O3 -g -DAPP_XUH_MSC_ACTIVITY_LED=1 \
      -Iexamples/app_xuh_msc/src examples/app_xuh_msc/src/activity_led.c \
      examples/app_xuh_msc/tests/app_activity_led/src/main.xc \
      examples/app_xuh_msc/tests/app_activity_led/src/test.c -o /tmp/activity_led.xe
    xsim --max-cycles 100000000 /tmp/activity_led.xe

It must print ``PASS: tile-1 LED task`` and exit normally before the cycle limit;
also apply a host-process timeout when automating it. Repeat the compile with
``-DAPP_XUH_MSC_LED_ACTIVE_LOW=1`` to check inverted outputs too. The fixture
samples the simulated pins without changing port direction; it does not require
USB or an LCD and does not run on the board as part of the native test.

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

For frames after sequence zero, tile 0 acknowledges the YUV planes immediately
after conversion, overlay and optional integrity hashing, before waiting for
the previous LCD completion. Tile 1 can decode while tile 0 waits, but its next
channel send backpressures until tile 0 has published the ready RGB slot. Only
then does tile 0 receive into the other, released slot. The acknowledgement
releases YUV ownership, not RGB ownership. Sequence zero still waits for the
first LCD completion before acknowledging; EOF/stop still drains the last
published frame before returning statistics. No third framebuffer is needed.

The conversion is ``lib_mpeg``'s ``mpeg_yuv420_rows_to_rgb565()``
(``mpeg_rgb565.h``), called on tile 0 for each strip. It follows PL_MPEG's
limited-range BT.601 coefficients and is bit-exact with its scalar arithmetic.
On XS3 the default converter uses the VPU for 16 pixels in each of two rows,
including exact BT.601 arithmetic, clipping and high-byte-first RGB565 packing.
It uses a small stack input block and assembly scratch, not an RGB888 frame or
lookup tables. ``-DMPEG_RGB565_VPU=OFF`` restores the original scalar lookup-table
converter (5504 bytes of arrays, built on first use), including its two-pixel
32-bit stores. Both implementations are bit-exact with the upstream reference.
The application is built
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
The per-play stage table gives mean microseconds per displayed frame and labels
where each stage runs:

.. list-table:: MPEG-1 timing scopes
   :header-rows: 1

   * - Stage
     - Tile
     - Timed work
   * - Load
     - 1
     - Filesystem reads; overlaps decoding through read-ahead
   * - Wait for loader
     - 1
     - Decoder waiting for the next input chunk
   * - Feed
     - 1
     - Copying compressed input into the decoder
   * - Decode
     - 1
     - Producing the next decoded YUV420 picture
   * - Transmit / ack wait
     - 1 to 0 (timed on 1)
     - Sending planar YUV and waiting for the receiver's acknowledgement
   * - Send YUV / backpressure
     - 1 to 0 (timed on 1)
     - Header, strip payloads and CT_END, including blocked channel output
   * - Wait receiver ack
     - 1
     - Waiting for the final tile-0 acknowledgement after CT_END was sent
   * - Receive YUV / wait
     - 0
     - Receiving strip payload words, including waiting for incoming data
   * - YUV to RGB565
     - 0
     - Converting the received strips into the existing LCD framebuffer
   * - Wait previous LCD
     - 0
     - Waiting for the previous render before publishing the next framebuffer
   * - LCD SPI
     - 0
     - Sending the framebuffer to the physical display

MPEG reception and conversion are timed separately around each two-row strip.
Tick deltas are accumulated per frame before conversion to microseconds, to
avoid per-strip rounding loss. These scopes exclude frame headers, CT_END,
overlay work and waits for the LCD renderer or return acknowledgement; transmit
time still covers the complete handoff. Extra timer reads add a small measurement
overhead, but no image buffers or tasks are added. Reports are printed only after
playback drains. JPEG retains its combined receive/conversion timing.

``Send YUV / backpressure`` and ``Wait receiver ack`` split the existing total
``Transmit / ack wait``; they are not additional independent costs. Channel
output can block while tile 0 converts each strip, so send time is not a pure
wire-bandwidth measurement. The receiver's ``Wait previous LCD`` counter times
only the previous-frame acknowledgement receive, excluding overlay work,
publication overhead, the first-frame display wait and final drain. First-frame
display waiting is still included in the sender's acknowledgement time. As with
other stage totals, counters reset each play and are reported as per-frame means.

Elapsed reference-timer deltas accumulate into a 64-bit total; each operation
must complete within one 32-bit timer period.

Wait for loader, feed, decode and transmit/wait run one after another in the
decoder task on tile 1, so their sum is the frame period. Load now overlaps
decoding and only costs the frame period what appears as wait for loader.
Reception and conversion run on tile 0 during transmit/wait, and LCD SPI overlaps
the following frame's decode. These overlapping timings must not be added to the
tile-1 frame-period sum. The sender can block while tile 0 converts each strip,
so faster conversion can shorten transmit/wait as well.

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

The recorded stage means were decode 36858 us, transmit/wait 16718 us,
receive/convert 13407 us and LCD SPI 41908 us. Loader reads averaged 4350 us,
overlapping decoding; feed averaged 97 us. The decoder's loader wait averaged
1602 us on play 1 and 1144 us on play 2. The first decoder wait includes startup
before the loader begins playback, so use the second play for steady-state
stage accounting: wait + feed + decode + transmit totals about 54.8 ms/frame.
This is a whole-file average, not a worst-picture-time bound.
These measurements predate the separate receive/conversion counters: their
13407 us combined value cannot be split retrospectively. The table below
preserves the original combined measurements; a fresh split-timing comparison
follows under `VPU colour conversion comparison`_.

In the table, "Load/wait" is the loader's read time for the first four rows,
where the decoder waited for every read, and the decoder's wait for the loader
once reads overlap decoding. The read-ahead row's 1.6 ms is from a first play,
which includes the wait while the USB device starts up; the last row is a
second play.

.. list-table:: Mean MPEG-1 stage times per frame on hardware (ms)
   :header-rows: 1

   * - Build
     - Load/wait (tile 1)
     - Feed (tile 1)
     - Decode (tile 1)
     - Transmit/wait (tile 1 to 0)
     - Receive/convert (tile 0)
     - LCD SPI (tile 0)
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

In that historical build, decode was about two thirds of the 54.9 ms frame
period and was the limiting stage.
The decoder shares tile 1 with only the USB manager and FatFs tasks, so it
already has a full logical core. The LCD write is at the wire rate for the
30 MHz SPI clock then in use and would limit playback at about 24 FPS. These figures are for
one file on one board; other content will decode at different rates.

The first integrated XTC 15.3.1 build reports 321844 bytes on tile 0 and
467812 bytes on tile 1, leaving 56476 bytes on tile 1. The tile-1 figure includes
a provisional 16 KiB decoder-task stack reservation because upstream callback
paths prevent complete automatic sizing. Linker constraints pass with caveats;
this reservation is not a proven production stack bound.

VPU colour conversion comparison
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A matched scalar/VPU comparison on 2026-11-13 used the same XU316 at 600 MHz,
ILI9341 panel at 30 MHz SPI, XTC Tools 15.3.1 and the 3025-frame reference file.
The FPS overlay was enabled. Each build completed two consecutive plays,
decoding and displaying every frame with zero errors, arena peak 379288 bytes
and zero live allocations after close. Only ``MPEG_RGB565_VPU`` changed between
the builds. Both used the same PL_MPEG fork revision; its
decoder improvements were already present before the VPU work. Compare against
the fresh **20.20 FPS** baseline, not the historical 18.23 FPS measurement.

.. list-table:: Matched second-play stage means (ms per displayed frame)
   :header-rows: 1

   * - Stage
     - Tile
     - Scalar
     - VPU
   * - Load
     - 1
     - 4.350
     - 4.351
   * - Wait for loader
     - 1
     - 1.269
     - 1.269
   * - Feed
     - 1
     - 0.097
     - 0.097
   * - Decode
     - 1
     - 30.581
     - 30.581
   * - Transmit / acknowledgement wait
     - 1 to 0 (timed on 1)
     - 17.519
     - 13.748
   * - Receive YUV / wait
     - 0
     - 1.245
     - 1.245
   * - YUV to RGB565
     - 0
     - 12.369
     - 5.740
   * - LCD SPI
     - 0
     - 41.905
     - 41.866

Both scalar plays completed in 149.691 s at **20.20 FPS**. The VPU plays took
138.284 s and 138.283 s, both **21.87 FPS**: an **8.3%** end-to-end increase.
The integrated conversion itself is 2.15 times faster; channel receive time and
decode time are unchanged. The 6.629 ms conversion saving reduces transmit/wait
by only 3.771 ms because the receiver also waits for the LCD renderer. Timings
overlap and must not be added as independent stages.

The VPU build uses **331836 bytes on tile 0**, versus 335616 scalar: a net saving
of **3780 bytes**, despite extra code and stack scratch. Tile 1 is unchanged at
480820 bytes. The two LCD framebuffers, YUV strip and decoder arena are unchanged;
the VPU build removes the colour lookup arrays. Linker callback-analysis caveats
and the provisional decoder stack reservation still apply.

Handoff and acknowledgement timing
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A further run with the sender and renderer-wait counters split completed two
3025-frame plays in 138.278 s each, at **21.87 FPS**, with zero errors, the same
arena peak and no live allocations after close. The second-play means were:

.. list-table:: VPU handoff breakdown (ms per displayed frame)
   :header-rows: 1

   * - Stage
     - Tile
     - ms/frame
   * - Total transmit / acknowledgement wait
     - 1 to 0 (timed on 1)
     - 13.746
   * - Send YUV / backpressure (part of total)
     - 1 to 0 (timed on 1)
     - 6.945
   * - Final receiver acknowledgement (part of total)
     - 1
     - 6.801
   * - Receive YUV / wait
     - 0
     - 1.245
   * - YUV to RGB565
     - 0
     - 5.739
   * - Wait previous LCD
     - 0
     - 6.312
   * - Decode
     - 1
     - 30.580
   * - LCD SPI
     - 0
     - 41.864

The channel-send portion overlaps reception and conversion; it is not 6.945 ms
of raw link transfer followed by those stages. Most final acknowledgement
waiting corresponds to the receiver waiting for the previous LCD render. The
remaining difference also includes last-strip work, overlay/protocol overhead
and the first-frame display wait; those are not individually attributed here.
The sender's 13.746 ms total is therefore not a channel-bandwidth bottleneck.

The extra counters/reporting increase the VPU build totals by 48 bytes on tile 0
and 288 bytes on tile 1, to 331884 and 481108 bytes respectively. No image
buffers, decoder arena storage or tasks are added.

Early YUV acknowledgement
~~~~~~~~~~~~~~~~~~~~~~~~~~

Moving the non-first-frame acknowledgement after conversion/overlay/hash and
before the previous-LCD wait completed two further 3025-frame plays in 137.135 s
and 137.134 s: **22.05 FPS**, versus **21.87 FPS** above (about 0.8% faster).
Both plays decoded/displayed all frames with zero errors, arena peak 379288
bytes and zero live allocations after close. Neither decoder nor converter
arithmetic was changed for this comparison. Second-play means were:

.. list-table:: ACK-only change (ms per displayed frame)
   :header-rows: 1

   * - Stage
     - Tile
     - Original ACK
     - Early ACK
   * - Decode
     - 1
     - 30.580
     - 30.581
   * - Total transmit / acknowledgement wait
     - 1 to 0 (timed on 1)
     - 13.746
     - 13.357
   * - Send YUV / backpressure (part of total)
     - 1 to 0 (timed on 1)
     - 6.945
     - 12.868
   * - Final receiver acknowledgement (part of total)
     - 1
     - 6.801
     - 0.488
   * - Receive YUV / wait
     - 0
     - 1.245
     - 1.244
   * - YUV to RGB565
     - 0
     - 5.739
     - 5.761
   * - Wait previous LCD
     - 0
     - 6.312
     - 18.348
   * - LCD SPI
     - 0
     - 41.864
     - 41.859

Most of the removed final-ACK wait reappears as backpressure on the next send:
the decoder can start earlier but tile 0 still owns only two RGB slots. The
larger previous-LCD wait starts earlier in the overlap schedule; it is not an
extra additive frame cost or slower SPI. This is a modest measured gain, not
the full 6.312 ms/frame suggested by the original isolated wait counter.
First-display startup stayed effectively unchanged (123849 versus 123832 us).
ILI9341 linker totals became 331828 bytes on tile 0 (56 bytes less) and 481108
bytes on tile 1 (unchanged); no buffers or tasks were added. ST7789V also builds
with the same protocol; the physical-panel throughput measurement is ILI9341.

Scalar motion-compensation follow-up
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

After measuring the ACK-only build, a separate PL_MPEG change retains packed
four-pixel scalar arithmetic while reducing word-load overhead:

* Rounded averaging uses an inline function, evaluating load/extract arguments
  once instead of twice through the old macro.
* xcore destination reads use the word alignment already required by stores.

The two- and four-sample interpolation biases, and the separately rounded
bidirectional average, are unchanged. No VPU decoder kernel, lookup table,
additional frame storage or new task is introduced. ILI9341 linker totals are
331828 bytes on tile 0 and 481020 bytes on tile 1: unchanged and 88 bytes smaller
respectively than the ACK-only build. The tile-1 difference is 92 fewer code
bytes and four more constant/data bytes; its reserved stack total is unchanged.
The macroblock function's generated assembly has eight LEXTRACT sites rather
than sixteen and retains the original 18-word local stack requirement. Those
are static code observations, not measured execution counts or timings.

A preceding experiment also hoisted source alignment and carried overlapping
aligned loads across inner-loop iterations. It was bit-exact and used 48 fewer
bytes than ACK-only, but two complete plays each took 138.349 s (**21.86 FPS**).
Decode on tile 1 increased to 31.451 ms/frame, versus 30.581 ms for ACK-only;
handoff was 12.880 ms (12.391 send/backpressure plus 0.488 final ACK), tile-0
receive/conversion were 1.244/5.761 ms, previous-LCD wait was 17.427 ms and LCD
SPI stayed at 41.859 ms. All 3025 frames displayed with zero errors and unchanged
arena accounting. That more complex loop was removed rather than retaining a
memory-saving performance regression. These results do not establish that
load reuse is intrinsically slower, only that this scalar/compiler arrangement
did not help this clip. The simplified implementation above is a separate trial.

The simplified version completed two further 3025-frame plays in **135.425 s
each (22.33 FPS)**, with zero errors, arena peak 379288 bytes and zero live
allocations after close. Decoder time fell about 4.0% relative to ACK-only, and
FPS rose about 1.3%. Combined with early ACK, the gain over the instrumented VPU
baseline is about 2.1% (21.87 to 22.33 FPS), while total linker usage falls by
144 bytes across both tiles. The retained version changes only single-evaluation
averaging and aligned destination reads; the load-reuse experiment is not kept.

.. list-table:: Retained scalar fixes, second-play means (ms per displayed frame)
   :header-rows: 1

   * - Stage
     - Tile
     - ACK only
     - ACK + simple MC fixes
   * - Load
     - 1
     - 4.350
     - 4.350
   * - Wait for loader
     - 1
     - 1.269
     - 1.269
   * - Feed
     - 1
     - 0.097
     - 0.097
   * - Decode
     - 1
     - 30.581
     - 29.344
   * - Total transmit / acknowledgement wait
     - 1 to 0 (timed on 1)
     - 13.357
     - 14.029
   * - Send YUV / backpressure (part of total)
     - 1 to 0 (timed on 1)
     - 12.868
     - 13.540
   * - Final receiver acknowledgement (part of total)
     - 1
     - 0.488
     - 0.488
   * - Receive YUV / wait
     - 0
     - 1.244
     - 1.244
   * - YUV to RGB565
     - 0
     - 5.761
     - 5.761
   * - Wait previous LCD
     - 0
     - 18.348
     - 20.094
   * - LCD SPI
     - 0
     - 41.859
     - 41.859

The faster decoder reaches the next send sooner, increasing backpressure and
tile 0's earlier-starting previous-LCD wait. These overlapping waits must not
be summed or interpreted as slower channel transport. SPI is unchanged, with
an approximately 23.9 FPS full-screen ceiling at this clock rate. First-frame
display startup improved from 123849 to 123050 us. Both controller selections
still build; the measured physical display remains ILI9341 at 30 MHz SPI.

All five native sanitizer/reference tests pass, including byte-exact MPEG-1
comparison with pristine upstream. The standalone xcore macroblock test passes
2128 independent per-pixel comparisons covering both block sizes, alignments,
half-pixel modes and bidirectional rounding. The delayed-renderer hardware
pipeline fixture also passes with the modified decoder and native RGB565 hashes.

LCD SPI clock
~~~~~~~~~~~~~

The measurements above used a 30 MHz SPI clock, at which the 41.9 ms LCD write
capped playback at about 23.9 FPS. The clock is now **50 MHz** (``LCD_SPI_KHZ``
in ``src/lcd_display.xc``). ``lib_spi`` rounds a request down to the system
clock divided by a multiple of four, so with a 600 MHz system clock the steps
above 30 MHz are 37.5 MHz and 50 MHz.

Each setting completed two 3025-frame plays of the reference file on the
ILI9341 panel with zero errors, and the picture was checked by eye. Only the
clock changed between the builds.

.. list-table:: LCD SPI clock, second-play means (ms per displayed frame)
   :header-rows: 1

   * - Stage
     - Tile
     - 30 MHz
     - 37.5 MHz
     - 50 MHz
   * - Wait for loader
     - 1
     - 1.269
     - 1.266
     - 1.265
   * - Decode
     - 1
     - 29.344
     - 29.767
     - 29.767
   * - Total transmit / acknowledgement wait
     - 1 to 0 (timed on 1)
     - 14.029
     - 10.354
     - 8.534
   * - YUV to RGB565
     - 0
     - 5.761
     - 5.771
     - 5.763
   * - Wait previous LCD
     - 0
     - 20.094
     - 8.227
     - 2.734
   * - LCD SPI
     - 0
     - 41.859
     - 33.665
     - 25.471
   * - FPS
     -
     - 22.33
     - 24.08
     - 25.19

At 50 MHz the LCD write no longer limits playback: the decoder task's wait for
the loader, feed, decode and handoff add up to the whole 39.7 ms frame period.
The average is just above the file's 25 FPS. Pictures differ in decode time, so
the on-screen rate, measured over half-second windows, still falls below 25 at
times. The 30 MHz column is the earlier run recorded above; the 0.4 ms decode
difference from it has not been investigated. ST7789V has not been run at this
clock.

Sampled decoder profiling
~~~~~~~~~~~~~~~~~~~~~~~~~

XTC Tools 15.3.1 rejects ``--gprof`` together with ``--xscope``. Use a separate
``-DAPP_XUH_MSC_PROFILE_SILENT=ON`` build for PC sampling. This disables xSCOPE
and provides a ``_write`` sink that discards terminal output while retaining
printf formatting/control flow. It avoids JTAG logging pauses and blocked
xSCOPE writes without a host receiver. Normal builds retain xSCOPE output.

Keep the production optimisation flags and decoder sources unchanged. Configure
and build from the repository root::

    cmake -G "Unix Makefiles" -S examples/app_xuh_msc -B build-video-profile \
      -DAPP_XUH_MSC_PROFILE_SILENT=ON -DMPEG_RGB565_VPU=ON \
      -DAPP_XUH_MSC_LCD_CONTROLLER=ILI9341
    xmake -C build-video-profile mpeg1 -j 8

Run xgdb in a separate directory to hold the per-tile/core ``.gprof`` files.
Connect with ``connect --gprof`` (and ``--adapter-id`` if required), ``load``,
``break video_loader_report``, then ``continue``. This breakpoint is reached only
after a successful complete play, after the application's frame/error checks.
Inspect ``number`` and ``*play``, then ``quit`` to flush the profiles. Extract
the matching executable with ``xobjdump -s`` and use ``xgprof -b`` for function
samples, ``xgprof -l -b`` for source lines and ``xgprof -C -l -b`` for addresses.
Find the decoder core from its PL_MPEG symbols rather than assuming a core ID.

Use the separate normal xSCOPE run for FPS and stage timings, not the sampled
run or this silent build's memory totals. PC sample shares are statistical
hotspot indicators, including PCs where a core is blocked, not call counts or
precise subfunction timings.

The completed silent profile reached ``video_loader_report`` with 3025 frames,
zero errors and decoder status ``MPEG_END``. The decoder ran on **tile 1, core 2**
in this build. Full-width counters recorded 841376 PC samples at 1979 addresses;
all mapped into function ranges in the matching application ELF. Of these,
551242 samples were in PL_MPEG functions:

.. list-table:: Decoder hotspots, normalised to PL_MPEG function samples
   :header-rows: 1

   * - Work
     - Tile/core
     - Samples
     - Share
   * - Motion compensation (``plm_video_process_macroblock``)
     - 1 / 2
     - 213519
     - 38.7%
   * - Block parsing, dequantisation and residual writes (``plm_video_decode_block``)
     - 1 / 2
     - 160770
     - 29.2%
   * - IDCT (``plm_video_idct``)
     - 1 / 2
     - 133798
     - 24.3%
   * - Other PL_MPEG functions
     - 1 / 2
     - 43155
     - 7.8%

These are self-PC shares, not inclusive call-graph times. Inline helper loads,
stores and bit reads are included in their containing ELF function. Motion
compensation is the largest compute target in this clip, ahead of IDCT. Library
``memcpy`` had only two samples; it was not a measured hotspot.

The stock ``xgprof`` emitted a DWARF line-table warning. To avoid relying on its
line attribution or synthetic time units, the table above uses the full 32-bit
basic-block PC counters mapped directly into ELF function address/size ranges.
The source-line report omitted some sampled addresses, so it is not used as the
denominator. Reproduce the complete function accounting from the profile
directory, using the matching tile-1 application ELF::

    python /path/to/lib_xuh/examples/app_xuh_msc/tests/summarize_mpeg_profile.py \
      --elf image_n0c1_2.elf --profile 'tile[1]_core2.gprof' \
      --json decoder-summary.json

The helper's native parser/accounting tests run with::

    python examples/app_xuh_msc/tests/test_profile_summary.py

The handoff counters motivated the early YUV acknowledgement described above.
The original measurements in the handoff/profile tables predate that change;
keep them as the baseline rather than attributing LCD waiting to channel
bandwidth or decoder compute.

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

The finite pipeline fixture substitutes host-file input and a checksum renderer
for USB and physical SPI. It exercises the production MPEG decoder/receiver
tasks, cross-tile strip transfer, native-reference RGB565 hashes, repeated EOF
drain/restart and open/read/empty-file error recovery. It does not validate USB,
the physical display or end-to-end hardware throughput. The checksum renderer
waits 100 ms before reading each slot, deliberately exercising backpressure and
detecting a slot overwrite during rendering. With XTC active::

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
the test and pass ``--frames 3`` to the runner. Use ``6`` for both settings to
exercise next-frame send backpressure beyond the first delayed render and the
final drain. Add ``--adapter-id SERIAL`` to
run the same fixture on hardware instead of xsim (host files remain in the
fixture directory, not on USB). The stock simulator target routes xSCOPE over
XL0; the attached AIV board uses XL1. For a finite hardware fixture independent
of xSCOPE wiring, configure ``-DMPEG_PIPELINE_TEST_XSCOPE=OFF`` and pass
``--adapter-id SERIAL --jtag`` to the runner. JTAG pauses are acceptable only in
this substituted functional fixture, never in the real USB player benchmark.

With XTC 15.3.1 on the attached XU316, the six-picture hardware fixture passed
all seven cases and all 24 RGB565 frame hashes, including delayed rendering,
EOF drain/restart and error recovery. The corresponding simulator run exceeded
240 host seconds after frame zero with three pictures; it is not counted as a
pass. The six-picture JTAG run needed more than 100 host seconds; a bounded
180-second rerun completed all cases. Earlier hardware
attempts using the stock target's mismatched xSCOPE link also timed out; the
JTAG fixture avoids that configuration mismatch. Independent library reference
tests also exercise I/P/B decoding and final-picture drain.
