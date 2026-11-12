Motion JPEG display pipeline
============================

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
----------------------------

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
