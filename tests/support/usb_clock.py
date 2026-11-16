# Copyright 2016-2026 XMOS LIMITED.
# This Software is subject to the terms of the XMOS Public Licence: Version 1.
"""60-MHz UTMI clock adapted from lib_xud/tests/usb_clock.py.

Source: xmos/lib_xud, revision 8375c51efc1d0c8c403e624eff0197880f7b8795.
"""
from Pyxsim import SimThread


class UsbClock(SimThread):
    period_fs = 16666667

    def __init__(self):
        self.high = False

    def run(self):
        time = self.xsi.get_time()
        self.xsi.drive_periph_pin("XS1_USB_CLK", 0)
        while True:
            time += self.period_fs / 2
            self.wait_until(time)
            self.high = not self.high
            self.xsi.drive_periph_pin("XS1_USB_CLK", int(self.high))
