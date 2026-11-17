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
    parser.add_argument("--dac", action="store_true",
                        help="Require the DAC queue/mailbox fixture (build with TEST_AUDIO_DAC=ON)")
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
        import Pyxrun
        from hardware_output import HardwareOutput

        # Full xSCOPE (the default), not I/O only: short.mp2 is read with host-file
        # system calls. run() prints the output and keeps it in the log.
        captured = HardwareOutput()
        failed = not Pyxrun.run(args.xe.resolve(), captured, adapter_id=args.adapter_id, timeout=args.timeout,
                                cwd=work, log_path=work / "run.log")
        output = captured.text
    else:
        command = ["xsim", "--max-cycles", "500000000", "--xscope",
                   f"-offline {work / 'audio-pipeline.vcd'}", str(args.xe.resolve())]
        try:
            result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=args.timeout)
        except subprocess.TimeoutExpired as error:
            def text(value):
                return value.decode(errors="replace") if isinstance(value, bytes) else value or ""
            output = text(error.stdout) + text(error.stderr)
            (work / "run.log").write_text(output)
            print(output, end="")
            raise SystemExit(f"Audio simulation exceeded {args.timeout:g} host seconds; see {work / 'run.log'}")
        output = result.stdout + result.stderr
        (work / "run.log").write_text(output)
        failed = result.returncode != 0
        print(output, end="")
    marker = "PASS: DAC audio pipeline 10 cases;" if args.dac else "PASS: audio pipeline 10 cases;"
    if failed or marker not in output:
        raise SystemExit("Audio pipeline did not complete the selected fixture")


if __name__ == "__main__":
    main()
