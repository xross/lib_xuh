from pathlib import Path

import pytest


def pytest_addoption(parser):
    parser.addoption("--build-dir", type=Path, default=Path(__file__).parent / "build",
                     help="Configured XCommon CMake build directory for library tests")


@pytest.fixture(scope="session")
def build_dir(request):
    directory = request.config.getoption("--build-dir").resolve()
    if not (directory / "CMakeCache.txt").is_file():
        pytest.fail(f"Configure tests/ with XCommon CMake first: missing {directory / 'CMakeCache.txt'}")
    return directory
