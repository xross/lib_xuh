import time

import pytest


def pytest_addoption(parser):
    parser.addoption("--seed", type=int, help="Seed for randomised tests; default is the time")


@pytest.fixture
def test_seed(request):
    """A 32-bit seed. pytest shows it when a test fails; pass it to --seed to reproduce."""
    seed = request.config.getoption("--seed")
    return (time.time_ns() if seed is None else seed) & 0xffffffff
