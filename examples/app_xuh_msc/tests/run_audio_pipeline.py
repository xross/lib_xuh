#!/usr/bin/env python3
"""Run the actual cross-tile MP2 channel task with a bounded, generated fixture."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xe", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--adapter-id", help="Use attached hardware; otherwise simulate")
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
                    "sine=frequency=997:sample_rate=48000", "-frames:a", "3",
                    "-c:a", "mp2", "-ar", "48000", "-ac", "2", "-b:a", "192k",
                    "-f", "mp2", str(work / "short.mp2")], check=True, timeout=30)
    # Some FFmpeg versions flush an extra encoder frame. At these fixed settings
    # each MP2 frame is exactly 576 bytes; retain precisely three complete frames.
    fixture = work / "short.mp2"
    data = fixture.read_bytes()
    if len(data) < 1728:
        raise SystemExit("FFmpeg produced fewer than three MP2 frames")
    fixture.write_bytes(data[:1728])
    if args.adapter_id:
        command = ["xrun", "--adapter-id", args.adapter_id, "--io", str(args.xe.resolve())]
    else:
        command = ["xsim", "--max-cycles", "100000000", str(args.xe.resolve())]
    result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=args.timeout)
    output = result.stdout + result.stderr
    (work / "run.log").write_text(output)
    print(output, end="")
    if result.returncode or "PASS: audio pipeline 8 cases;" not in output:
        raise SystemExit("Audio pipeline did not complete all eight cases")


if __name__ == "__main__":
    main()
