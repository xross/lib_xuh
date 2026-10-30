JPEG display pipeline
=====================

The JPEG/LCD configuration cycles through ``TEST0.JPG``, ``TEST1.JPG``,
``TEST2.JPG`` and ``TEST3.JPG`` from USB storage. Each frame is reopened, read,
closed, decoded and displayed at full scale. The default image size is
200 x 200; file selection is independent of the two pipeline buffer slots.
The loader is the only task that accesses FatFs and the USB endpoints.

Task placement
--------------

* Tile 1: USB manager, file loader, JPEG decoder and pixel transmitter.
* Tile 0: pixel receiver/RGB565 converter, LCD renderer and SPI master.

The compressed input and RGB888 output each have two buffers on tile 1. Tile 0
has two full-screen RGB565 buffers. Buffer pointers never cross tiles; pixel
bytes do. The receiver converts the next image while SPI renders the previous
one. The decoder can process another image while the previous RGB buffer is
being transmitted, and the loader can fill the alternate input buffer during
decoding.

Ownership protocol
------------------

Ready messages carry a slot index, monotonically increasing frame sequence and
stage-specific metadata. The consumer returns a completion message before the
producer can publish another buffer. A producer may fill the alternate slot
while the previous slot is owned by the consumer. It cannot reuse that previous
slot until completion has been received. This bounded handoff supplies
backpressure without polling or dropping frames.

Every raw channel message ends with ``CT_END`` to release its route. C handoffs
include compiler memory barriers so shared-buffer accesses cannot be reordered
across ownership transfers. The LCD task uses an unsafe pointer only while it
owns a tile-local framebuffer; completion is sent after the SPI transaction.

Benchmark
---------

The loader produces frames for approximately five seconds, then sends a stop
message. Each stage finishes its pending work before forwarding stop. The final
result travels back only after the last LCD transaction has completed. Loaded
and displayed frame counts must match, and sequence/slot acknowledgements are
checked throughout.

The report includes total FPS with fill/drain, startup-to-first-display latency,
steady-state FPS after the first completed frame, and average stage times.
The first image is explicitly acknowledged after rendering to measure startup;
later image acknowledgements permit receive/render overlap. All timestamps used
for end-to-end rates are taken on tile 1, avoiding cross-tile timer offsets.
Transmit/wait and receive/convert timings include channel backpressure, so stage
averages overlap and must not be added together. Normal per-frame logging is
disabled.

Enable ``PIPELINE_VERIFY=1`` as a compiler define for development integrity
checks. These compare complete decoded images and LCD framebuffers against the
first instance of each of the four images, check RGB buffers before
and after transmission, and check LCD buffers before and after rendering.
This verification assumes the four source images do not change during a run and
adds work, so leave it disabled for performance measurements.

The default double buffers require the large memory model. The XU316 build fits
within each tile's 512 KiB SRAM; inspect the linker memory/stack report when
changing image sizes. RGB buffers currently hold 200 x 200 x 3 bytes, and each
compressed frame is limited to less than 30 KiB.

Earlier measurements on the XU316 board with two 400 x 400 source images decoded at
half scale to 200 x 200 RGB, and a full 240 x 320 LCD update at 30 MHz SPI:

* Serial: 3.58 FPS.
* Pipelined, including fill/drain: 27 completed frames in 5.437 s, 4.96 FPS.
* Pipelined steady state: 4.99 FPS.
* Average pipeline stage times: load 18.2 ms, decode 199.2 ms,
  transmit/wait 9.1 ms, receive/convert 7.6 ms, LCD SPI 42.1 ms.

With four resized 200 x 200 JPEGs decoded at full scale, the hardware benchmark
completed 78 frames in 5.121 s: 15.22 FPS overall and 15.39 FPS steady state.
Average load time was 11.8 ms and decode time was 64.9 ms. The LCD SPI transfer
remained 42.1 ms per frame.

Decode remains the limiting stage. The earlier integrity-enabled benchmark completed
without mismatches; missing-file and decode-error cases also drained pending
valid frames without deadlock. Integrity checks are disabled in the timing build.

For future MJPEG input, the loader-to-decoder message already describes a frame
by its compressed-buffer slot, sequence number and byte count. A sequential
frame reader can replace the alternating-file loader without changing the
pixel receiver or LCD renderer.
