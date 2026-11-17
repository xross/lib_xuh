#!/usr/bin/env python3
"""Check how the pipeline runners launch hardware, without a decoder build or attached board."""
from contextlib import redirect_stdout
import io
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import run_audio_pipeline as audio
import run_mpeg_pipeline as video


def pyxrun(calls, output, *, stderr="", success=True):
    """Stand-in module for Pyxrun, recording how run() is called."""
    def run(xe, tester=None, **options):
        calls.append(options)
        options["log_path"].write_text(output + stderr)
        if success and tester is not None:
            return tester.run([line.strip() for line in output.splitlines() if line])
        return success

    return {"Pyxrun": SimpleNamespace(run=run)}


class HardwareRunnerTests(unittest.TestCase):
    def test_audio_uses_full_xscope_for_host_fixture_reads(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            calls = []

            def ffmpeg(*args, **kwargs):
                (work / "short.mp2").write_bytes(bytes(1728))

            args = ["run_audio_pipeline.py", "--xe", "audio.xe", "--work", str(work),
                    "--adapter-id", "SERIAL", "--timeout", "45"]
            modules = pyxrun(calls, "PASS: audio pipeline 10 cases; checked\n")
            with patch.object(sys, "argv", args), patch.object(audio.subprocess, "run", ffmpeg), \
                    patch.dict(sys.modules, modules), redirect_stdout(io.StringIO()):
                audio.main()
            self.assertEqual(calls, [{"adapter_id": "SERIAL", "cwd": work.resolve(),
                                      "log_path": work.resolve() / "run.log", "timeout": 45}])

    def test_audio_rejects_failure_or_pass_marker_only_on_stderr(self):
        for success, stderr in ((False, ""), (True, "PASS: audio pipeline 10 cases; checked\n")):
            with self.subTest(success=success), tempfile.TemporaryDirectory() as directory:
                work = Path(directory)

                def ffmpeg(*args, **kwargs):
                    (work / "short.mp2").write_bytes(bytes(1728))

                args = ["run_audio_pipeline.py", "--xe", "audio.xe", "--work", directory,
                        "--adapter-id", "SERIAL"]
                with patch.object(sys, "argv", args), patch.object(audio.subprocess, "run", ffmpeg), \
                        patch.dict(sys.modules, pyxrun([], "", stderr=stderr, success=success)), \
                        self.assertRaises(SystemExit):
                    audio.main()

    def test_audio_simulation_timeout_keeps_partial_output(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            def run(command, **options):
                if command[0] == "ffmpeg":
                    (work / "short.mp2").write_bytes(bytes(1728))
                    return
                raise audio.subprocess.TimeoutExpired(command, 1, output=b"audio case 0\n",
                                                       stderr=b"simulation diagnostic\n")
            args = ["run_audio_pipeline.py", "--xe", "audio.xe", "--work", directory, "--timeout", "1"]
            with patch.object(sys, "argv", args), patch.object(audio.subprocess, "run", run), \
                    redirect_stdout(io.StringIO()), self.assertRaisesRegex(SystemExit, "exceeded"):
                audio.main()
            self.assertEqual((work / "run.log").read_text(), "audio case 0\nsimulation diagnostic\n")

    def test_video_uses_full_xscope_and_preserves_explicit_legacy_mode(self):
        output = "\n".join(
            ["PASS: pipeline play=" + str(n) for n in range(7)] +
            ["source rate=25/1 FPS"] * 4 +
            ["  Receive YUV / wait 1 1", "  YUV -> RGB565 1 1",
             "  Send YUV / backpressure 0 -> 1 1", "  Wait receiver ack 0 1"] +
            ["RGB565 frame=0 hash=aaaaaaaa"] * 3 + ["RGB565 frame=0 hash=bbbbbbbb"] +
            ["PASS: MPEG pipeline repeat, EOF drain, open/read/empty errors and recovery"]
        )
        for jtag in (False, True):
            with self.subTest(jtag=jtag), tempfile.TemporaryDirectory() as directory:
                calls = []

                args = ["run_mpeg_pipeline.py", "--bench", "bench", "--xe", "video.xe",
                        "--work", directory, "--adapter-id", "reserved"] + (["--jtag"] if jtag else [])
                with patch.object(sys, "argv", args), \
                        patch.object(video, "reference", side_effect=[["aaaaaaaa"], ["bbbbbbbb"]]), \
                        patch.dict(sys.modules, pyxrun(calls, output)), \
                        redirect_stdout(io.StringIO()):
                    video.main()
                self.assertNotIn("xscope_io_only", calls[0])
                self.assertEqual(calls[0]["adapter_id"], "reserved")
                self.assertEqual(calls[0]["attach"], "io" if jtag else "xscope")

    def test_video_rejects_failure_or_pass_output_only_on_stderr(self):
        for success, stderr in ((False, ""), (True, "PASS: MPEG pipeline repeat, EOF drain, open/read/empty errors and recovery\n")):
            with self.subTest(success=success), tempfile.TemporaryDirectory() as directory:
                args = ["run_mpeg_pipeline.py", "--bench", "bench", "--xe", "video.xe",
                        "--work", directory, "--adapter-id", "reserved"]
                with patch.object(sys, "argv", args), \
                        patch.object(video, "reference", side_effect=[["aaaaaaaa"], ["bbbbbbbb"]]), \
                        patch.dict(sys.modules, pyxrun([], "", stderr=stderr, success=success)), \
                        self.assertRaises(SystemExit):
                    video.main()


if __name__ == "__main__":
    unittest.main()
