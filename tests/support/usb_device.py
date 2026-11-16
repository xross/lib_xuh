"""Small high-speed UTMI device peer for the real XUH host transport.

This models the digital PHY boundary, not a physical cable or a complete USB device.
All waits are bounded in simulated time; pytest also bounds the simulator process.
"""
from Pyxsim import SimThread

from .usb_packets import ACK, DATA0, IN, SOF, crc5, data_packet, token


class BulkInDevice(SimThread):
    txd = "tile[0]:XS1_PORT_8A"
    txv = "tile[0]:XS1_PORT_1K"
    txrdy = "tile[0]:XS1_PORT_1H"

    def __init__(self, clock, address, endpoint, payload):
        self.clock = clock
        self.address = address
        self.endpoint = endpoint
        self.payload = payload

    def until(self, predicate, timeout_us, reason):
        deadline = self.xsi.get_time() + timeout_us * 1_000_000_000
        self.wait(lambda xsi: predicate() or xsi.get_time() >= deadline)
        assert predicate(), f"USB device timed out: {reason}"

    def cycles(self, count):
        for _ in range(count):
            self.until(lambda: self.clock.high, 1, "clock high")
            self.until(lambda: not self.clock.high, 1, "clock low")

    def receive(self, timeout_us=250, max_bytes=1024):
        self.until(lambda: self.xsi.sample_port_pins(self.txv) == 1, timeout_us, "host packet")
        packet = bytearray()
        # Ready is established before the rising edge. Sample on the next rising
        # edge, after XUH's delayed falling-edge output launch has settled.
        self.until(lambda: not self.clock.high, 1, "packet clock boundary")
        self.xsi.drive_port_pins(self.txrdy, 1)
        while True:
            self.until(lambda: self.clock.high, 1, "packet sample edge")
            if not self.xsi.sample_port_pins(self.txv):
                break
            assert len(packet) < max_bytes, "Host packet/chirp exceeded its bound"
            packet.append(self.xsi.sample_port_pins(self.txd))
            self.until(lambda: not self.clock.high, 1, "packet clock low")
        # A high-speed peer can accept every byte; keep ready asserted between
        # packets too, so the first transfer sees the configured input delay.
        return bytes(packet)

    def attach(self):
        for pin in ("XS1_USB_RXA", "XS1_USB_RXV", "XS1_USB_RXE", "XS1_USB_RXD", "XS1_USB_LS"):
            self.xsi.drive_periph_pin(pin, 0)
        self.xsi.drive_port_pins(self.txrdy, 1)
        self.until(lambda: self.xsi.sample_periph_pin("XS1_USB_XCVRSEL") == 1 and
                   self.xsi.sample_periph_pin("XS1_USB_TERMSEL") == 1, 500, "host attach detection")
        # XUH's signalling mode reads raw DP on flag0 and DM on flag1.
        self.xsi.drive_periph_pin("XS1_USB_LS", 1)  # DP high: full-speed attach.
        self.until(lambda: self.xsi.sample_periph_pin("XS1_USB_XCVRSEL") == 0 and
                   self.xsi.sample_periph_pin("XS1_USB_TERMSEL") == 0, 500, "host reset")
        self.xsi.drive_periph_pin("XS1_USB_LS", 2)  # DM high: device chirp K.
        self.wait_until(self.xsi.get_time() + 10_000_000_000)
        self.xsi.drive_periph_pin("XS1_USB_LS", 0)
        chirp = self.receive(max_bytes=200_000)
        assert chirp and set(chirp) == {0x00, 0xff}, "Missing host K/J chirp response"
        print("USB device: attach and host chirp complete")

    def send(self, packet):
        self.cycles(4)
        self.xsi.drive_periph_pin("XS1_USB_RXA", 1)
        self.cycles(5)
        for byte in packet:
            assert not self.xsi.sample_port_pins(self.txv), "Host transmitted during device reply"
            self.xsi.drive_periph_pin("XS1_USB_RXD", byte)
            self.xsi.drive_periph_pin("XS1_USB_RXV", 1)
            self.cycles(1)
            self.xsi.drive_periph_pin("XS1_USB_RXV", 0)
        self.xsi.drive_periph_pin("XS1_USB_RXA", 0)

    def run(self):
        self.attach()
        for _ in range(8):
            packet = self.receive()
            if packet[0] != SOF:
                break
            assert len(packet) == 3, f"Malformed SOF: {packet.hex()}"
            frame = packet[1] | ((packet[2] & 7) << 8)
            assert packet[2] >> 3 == crc5(frame), "Incorrect SOF CRC5"
        else:
            raise AssertionError("Host sent SOFs but did not service the IN endpoint")
        expected = token(IN, self.address, self.endpoint)
        assert packet == expected, f"IN token mismatch: {packet.hex()} != {expected.hex()}"
        self.send(data_packet(DATA0, self.payload))
        assert self.receive(timeout_us=10) == bytes((ACK,)), "Host did not ACK DATA0"
        print("PASS: USB device checked IN token, DATA0 response and host ACK")
