#!/usr/bin/env python3
"""Bounded pipeline test with native-decoder RGB565 hashes; no USB/LCD model."""
import argparse
from pathlib import Path
import re
import subprocess


def run(command, cwd=None, timeout=60):
    result = subprocess.run([str(item) for item in command], cwd=cwd, timeout=timeout,
                            capture_output=True, text=True)
    log = result.stdout + result.stderr
    if result.returncode:
        raise RuntimeError(log)
    return log


def reference(bench, work, name, width, height, frames, rate=25):
    video = work / f"{name}.M1V"
    run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
         f"testsrc2=size={width}x{height}:rate={rate}", "-frames:v", frames, "-an", "-c:v", "mpeg1video",
         "-threads:v", 1, "-g", 12, "-bf", 2, "-q:v", 6, "-f", "mpeg1video", video])
    # Explicit sequence termination gives the final slice decoder lookahead.
    # Bare FFmpeg elementary output can end immediately after a short B slice;
    # the current codec may then leave its final macroblocks unwritten.
    with video.open("ab") as stream:
        stream.write(b"\x00\x00\x01\xb7")
    pixels = work / f"{name}.yuv"
    log = run([bench.resolve(), video, pixels])
    if f"Result: frames={frames} " not in log or "status=OK" not in log:
        raise RuntimeError("Native reference did not decode the fixture\n" + log)
    data = pixels.read_bytes()
    y_size, c_size = width * height, width * height // 4
    frame_size = y_size + 2 * c_size
    if len(data) != frames * frame_size:
        raise RuntimeError("Incorrect native picture size")
    hashes = []
    for n in range(frames):
        frame = data[n * frame_size:(n + 1) * frame_size]
        output = bytearray(320 * 240 * 2)
        x_offset, y_offset = (320 - width) // 2, (240 - height) // 2
        for y in range(height):
            for x in range(width):
                index = (y // 2) * (width // 2) + x // 2
                u = frame[y_size + index] - 128
                v = frame[y_size + c_size + index] - 128
                luma = ((frame[y * width + x] - 16) * 76309) >> 16
                r = min(255, max(0, luma + ((v * 104597) >> 16)))
                g = min(255, max(0, luma - ((u * 25674 + v * 53278) >> 16)))
                b = min(255, max(0, luma + ((u * 132201) >> 16)))
                pixel = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3)
                offset = ((y_offset + y) * 320 + x_offset + x) * 2
                output[offset:offset + 2] = bytes((pixel >> 8, pixel & 255))
        value = 2166136261
        for byte in output:
            value = ((value ^ byte) * 16777619) & 0xffffffff
        hashes.append(f"{value:08x}")
    return hashes


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bench", type=Path, required=True)
    parser.add_argument("--xe", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--adapter-id", help="Run on this XTAG instead of the simulator")
    parser.add_argument("--jtag", action="store_true", help="Hardware fixture built with MPEG_PIPELINE_TEST_XSCOPE=OFF")
    parser.add_argument("--frames", type=int, choices=(1, 3, 6), default=1,
                         help="Must match the harness MPEG_PIPELINE_TEST_FRAMES setting")
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=240)
    parser.add_argument("--rate", type=int, choices=(24, 25, 30, 50, 60), default=25)
    args = parser.parse_args()
    if args.jtag and not args.adapter_id:
        parser.error("--jtag requires --adapter-id")
    if not (0 < args.width <= 320 and 0 < args.height <= 240 and
            args.width % 4 == 0 and args.height % 2 == 0):
        parser.error("picture must fit 320x240, with width divisible by four and even height")
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    hashes = reference(args.bench, work, "VIDEO", args.width, args.height, args.frames, args.rate)
    alternate_size = (320, 180) if (args.width, args.height) == (320, 240) else (320, 240)
    alternate = reference(args.bench, work, "ALT", *alternate_size, args.frames)
    (work / "HASHES.TXT").write_text("\n".join(hashes) + "\n")
    (work / "ALTHASHES.TXT").write_text("\n".join(alternate) + "\n")
    # Host-file syscalls still need JTAG servicing; --xscope-io-only would
    # leave fopen/fread blocked even though terminal writes use xSCOPE.
    if args.adapter_id:
        import Pyxrun
        from hardware_output import HardwareOutput

        # Full xSCOPE (the default), not I/O only: the fixture is read with host-file
        # system calls. run() prints the output and keeps it in the log.
        captured = HardwareOutput()
        if not Pyxrun.run(args.xe.resolve(), captured, adapter_id=args.adapter_id, timeout=args.timeout,
                          attach="io" if args.jtag else "xscope", cwd=work, log_path=work / "xrun.log"):
            raise SystemExit("FAIL: pipeline did not run to completion")
        log = captured.text
    else:
        command = ["xsim", "--max-cycles", "1500000000", "--xscope", "-offline pipeline.xmt", args.xe.resolve()]
        log_path = work / "xsim.log"
        try:
            log = run(command, cwd=work, timeout=args.timeout)
        except subprocess.TimeoutExpired as error:
            partial = (error.stdout or b"") + (error.stderr or b"")
            if isinstance(partial, bytes):
                partial = partial.decode(errors="replace")
            log_path.write_text(partial)
            raise SystemExit(f"FAIL: pipeline exceeded {args.timeout} host seconds; no completion established") from error
        log_path.write_text(log)
        print(log, end="")
    marker = "PASS: MPEG pipeline repeat, EOF drain, open/read/empty errors and recovery"
    if marker not in log or log.count("PASS: pipeline play=") != 7:
        raise SystemExit("FAIL: incomplete pipeline test")
    if log.count(f"source rate={args.rate}/1 FPS") != (4 if args.rate == 25 else 3):
        raise SystemExit("FAIL: missing source frame-rate metadata")
    for stage in ("Receive YUV / wait", "YUV -> RGB565"):
        if not re.search(rf"^\s*{re.escape(stage)}\s+0\s+[1-9][0-9]*\s*$", log, re.MULTILINE):
            raise SystemExit(f"FAIL: missing tile-0 timing for {stage}")
    for stage, tile in (("Send YUV / backpressure", "1 -> 0"), ("Wait receiver ack", "1")):
        if not re.search(rf"^\s*{re.escape(stage)}\s+{re.escape(tile)}\s+[1-9][0-9]*\s*$",
                         log, re.MULTILINE):
            raise SystemExit(f"FAIL: missing sender timing for {stage}")
    for values, count in ((hashes, 3), (alternate, 1)):
        for number, value in enumerate(values):
            if log.count(f"RGB565 frame={number} hash={value}") != count:
                raise SystemExit("FAIL: missing or mismatched centred RGB565 frame")


if __name__ == "__main__":
    main()
