# Copyright 2016-2026 XMOS LIMITED.
# This Software is subject to the terms of the XMOS Public Licence: Version 1.
"""CRC helpers adapted from lib_xud/tests/usb_packet.py.

Source: xmos/lib_xud, revision 8375c51efc1d0c8c403e624eff0197880f7b8795.
Only the wire-format primitives are shared; host/device roles live in the PHY model.
"""

SOF, IN, DATA0, ACK = 0xa5, 0x69, 0xc3, 0xd2


def crc16(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xa001 if crc & 1 else crc >> 1
    return crc ^ 0xffff


def crc5(value):
    crc = 0x1f
    for _ in range(11):
        crc = (crc >> 1) ^ 0x14 if (value ^ crc) & 1 else crc >> 1
        value >>= 1
    return crc ^ 0x1f


def token(pid, address, endpoint):
    value = address | (endpoint << 7)
    return bytes((pid, value & 0xff, (value >> 8) | (crc5(value) << 3)))


def data_packet(pid, payload):
    checksum = crc16(payload)
    return bytes((pid,)) + bytes(payload) + checksum.to_bytes(2, "little")
