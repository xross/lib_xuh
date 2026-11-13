#!/usr/bin/env python3
from collections import Counter
import struct
import unittest

from summarize_mpeg_profile import function_ranges, group_samples, sample_counts


class ProfileSummaryTest(unittest.TestCase):
    def fixture(self):
        header = b'gmon\x01\x00\x00\x00' + bytes(12)
        histogram = b'\x00' + struct.pack('<4I', 0x1000, 0x2004, 3, 250000)
        histogram += b'milliseconds\0\0\0m' + struct.pack('<3H', 4, 6, 2)
        counters = b'\x02' + struct.pack('<I', 3)
        counters += struct.pack('<6I', 0x1004, 9, 0x1008, 12, 0x2000, 5)
        return header + histogram + counters

    def test_full_width_counts_not_histogram(self):
        self.assertEqual(sample_counts(self.fixture()), Counter({0x1004: 9, 0x1008: 12, 0x2000: 5}))

    def test_malformed_files(self):
        for data in (b'', bytes(20), self.fixture()[:-1], self.fixture() + b'\x01'):
            with self.subTest(data=data[-8:]), self.assertRaises(ValueError):
                sample_counts(data)

    def test_symbols_boundaries_and_unmapped_samples(self):
        table = ('0x00001000 g f .text 0x00000008 plm_outer\n'
                 '0x00001004 l f .text 0x00000004 plm_inner\n'
                 '0x00001000 g f .text 0x00000000 alias\n')
        grouped = group_samples(sample_counts(self.fixture()), function_ranges(table))
        self.assertEqual(grouped, Counter({'plm_inner': 9, '<unmapped>': 17}))
        self.assertEqual(sum(grouped.values()), 26)


if __name__ == '__main__':
    unittest.main()
