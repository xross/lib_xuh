import Pyxsim
from Pyxsim.testers import ComparisonTester

from support.usb_clock import UsbClock
from support.usb_device import BulkInDevice


def test_short_bulk_in(build_dir, capfd):
    """The real host receives a seven-byte DATA0 packet, ACKs it and preserves guards."""
    xe = build_dir / "bin/test_bulk_in.xe"
    Pyxsim.build(xe, build_dir=build_dir)
    clock = UsbClock()
    device = BulkInDevice(clock, address=5, endpoint=4,
                          payload=bytes((0x00, 0x11, 0x7f, 0x80, 0xa5, 0xfe, 0xff)))
    tester = ComparisonTester([
        "XUH waiting for device...",
        "FULL speed dev detected!",
        "USB device: attach and host chirp complete",
        "PASS: USB device checked IN token, DATA0 response and host ACK",
        "PASS: bulk IN slot=1 endpoint=4 address=5 bytes=7 guards intact",
    ])
    assert Pyxsim.run(xe, tester, [clock, device], capfd=capfd,
                      simargs=["--max-cycles", "10000000"], timeout=90)
