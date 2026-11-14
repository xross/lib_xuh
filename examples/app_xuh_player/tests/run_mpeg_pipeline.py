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
    args = parser.parse_args()
    if args.jtag and not args.adapter_id:
        parser.error("--jtag requires --adapter-id")
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    video = work / "VIDEO.M1V"
    run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
         "testsrc2=size=320x240:rate=25", "-frames:v", args.frames, "-an", "-c:v", "mpeg1video",
         "-threads:v", 1, "-g", 12, "-bf", 2, "-q:v", 6, "-f", "mpeg1video", video])
    pixels = work / "native.yuv"
    log = run([args.bench.resolve(), video, pixels])
    if f"Result: frames={args.frames} " not in log or "status=OK" not in log:
        raise RuntimeError("Native reference did not decode the fixture\n" + log)
    data = pixels.read_bytes()
    width, height = 320, 240
    y_size, c_size = width * height, width * height // 4
    frame_size = y_size + 2 * c_size
    if len(data) != args.frames * frame_size:
        raise RuntimeError("Incorrect native picture size")
    hashes = []
    for n in range(args.frames):
        frame = data[n * frame_size:(n + 1) * frame_size]
        output = bytearray()
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
                output.extend((pixel >> 8, pixel & 255))
        value = 2166136261
        for byte in output:
            value = ((value ^ byte) * 16777619) & 0xffffffff
        hashes.append(f"{value:08x}")
    (work / "HASHES.TXT").write_text("\n".join(hashes) + "\n")
    # Host-file syscalls still need JTAG servicing; --xscope-io-only would
    # leave fopen/fread blocked even though terminal writes use xSCOPE.
    command = (["xrun", "--adapter-id", args.adapter_id, "--io" if args.jtag else "--xscope", args.xe.resolve()]
               if args.adapter_id else
               ["xsim", "--max-cycles", "1500000000", "--xscope", "-offline pipeline.xmt", args.xe.resolve()])
    log_path = work / ("xrun.log" if args.adapter_id else "xsim.log")
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
    for stage in ("Receive YUV / wait", "YUV -> RGB565"):
        if not re.search(rf"^\s+{re.escape(stage)}\s+0\s+[1-9][0-9]*\s*$", log, re.MULTILINE):
            raise SystemExit(f"FAIL: missing tile-0 timing for {stage}")
    for stage, tile in (("Send YUV / backpressure", "1 -> 0"), ("Wait receiver ack", "1")):
        if not re.search(rf"^\s+{re.escape(stage)}\s+{re.escape(tile)}\s+[1-9][0-9]*\s*$",
                         log, re.MULTILINE):
            raise SystemExit(f"FAIL: missing sender timing for {stage}")
    for number, value in enumerate(hashes):
        if log.count(f"RGB565 frame={number} hash={value}") != 4:
            raise SystemExit("FAIL: missing or mismatched RGB565 frame")


if __name__ == "__main__":
    main()
