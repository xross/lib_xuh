"""Simulator test that builds what it is about to run.

Each case builds one configuration of an already-configured CMake project with
Pyxsim.build() and runs it with Pyxsim.run(). The peak width is a build
configuration, declared in CMakeLists.txt from the same params.json read here.
The seed is chosen when the test runs. One test puts it in a generated header and
rebuilds; the other passes it as a command-line argument to an unchanged build.
Use a header or a build configuration for anything that must be a compile-time
constant, such as an array size.
"""
import json
from pathlib import Path

import pytest

import Pyxsim
from Pyxsim.testers import ComparisonTester

HERE = Path(__file__).parent
NAME = Path(__file__).stem
PARAMS = json.loads((HERE / "params.json").read_text())


BUILD_DIR = HERE / "build"
SIMARGS = ["--max-cycles", "50000000"]


def executable(peak_bits):
    config = f"bits{peak_bits}"
    return config, HERE / NAME / "bin" / config / f"{NAME}_{config}.xe"


@pytest.mark.parametrize("peak_bits", PARAMS["peak_bits"])
def test_audio_meter_seed_in_header(peak_bits, test_seed, capfd):
    config, xe = executable(peak_bits)
    header = BUILD_DIR / "generated" / config / "test_seed.h"
    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text(f"#define TEST_SEED ({test_seed}u)\n")

    Pyxsim.build(xe, build_dir=BUILD_DIR)
    tester = ComparisonTester([f"seed={test_seed} bits={peak_bits}", "PASS: 2000 peaks"])
    assert Pyxsim.run(xe, tester, capfd=capfd, simargs=SIMARGS, timeout=120)


@pytest.mark.parametrize("peak_bits", PARAMS["peak_bits"])
def test_audio_meter_seed_as_argument(peak_bits, test_seed, capfd):
    _, xe = executable(peak_bits)
    Pyxsim.build(xe, build_dir=BUILD_DIR)  # Up to date unless the sources changed.
    # The argument takes precedence over whatever seed the header last held.
    for seed in (test_seed, test_seed ^ 0x5a5a5a5a):
        tester = ComparisonTester([f"seed={seed} bits={peak_bits}", "PASS: 2000 peaks"])
        assert Pyxsim.run(xe, tester, capfd=capfd, simargs=SIMARGS, appargs=[str(seed)], timeout=120)
