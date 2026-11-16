lib_xuh simulator tests
=======================

These tests run the real XUH manager and transfer code against a small Python
device peer at the digital UTMI boundary. They do not model a physical cable,
enumerate a complete device, or require attached hardware.

Activate XTC Tools 15.3.1 and a Python virtual environment, then run from the
repository root::

    python -m pip install -r tests/requirements.txt
    cmake -G "Unix Makefiles" -S tests -B build-tests
    python -m pytest tests/test_bulk_in.py --build-dir build-tests -v

The XS3 fixture uses a 600 MHz system clock, a 100 MHz reference clock and a
60 MHz Python-driven USB clock. Its FB265 simulator package exposes the pins
used to observe UTMI traffic. It is compiled in single-issue mode to match the
host assembly entry points. A simulator-only define reduces connection debounce
to 100 microseconds; normal builds keep the existing 100 millisecond debounce.

``test_short_bulk_in`` binds endpoint slot 1 to endpoint 4 at address 5. The peer
checks the host's IN token and CRC5, returns a seven-byte DATA0 packet with CRC16,
and checks the host ACK. The application checks the byte count, payload and guard
bytes around receive storage. Both peer waits and the simulator run are bounded;
success requires the packet checks and application completion messages, not just
reaching the cycle limit.

``pytest.ini`` selects this library suite by default. The player's native and
pipeline fixtures remain separate under ``examples/app_xuh_msc_player/tests``.
