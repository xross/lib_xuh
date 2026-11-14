#!/usr/bin/env python3
"""Summarise XTC 32-bit PC-sample counters, not gprof's synthetic time units."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import struct
import subprocess


def sample_counts(data):
    """Read little-endian gmon v1 histogram/basic-block records from xgdb.

    XTC stores its full-width PC sample counts in the basic-block record.
    Although gprof calls these 'executions', they are samples, not call counts.
    Do not reconstruct counts from rounded source-line percentages or times.
    """
    if len(data) < 20 or data[:8] != b'gmon\x01\x00\x00\x00':
        raise ValueError('Expected an XTC little-endian 32-bit gmon v1 file')
    position = 20
    counts = Counter()
    while position < len(data):
        tag = data[position]
        position += 1
        if tag == 0:
            if position + 32 > len(data):
                raise ValueError('Truncated histogram header')
            size = struct.unpack_from('<I', data, position + 8)[0]
            position += 32
            if size > (len(data) - position) // 2:
                raise ValueError('Truncated histogram')
            position += 2 * size
        elif tag == 2:
            if position + 4 > len(data):
                raise ValueError('Truncated sample-count header')
            size = struct.unpack_from('<I', data, position)[0]
            position += 4
            if size > (len(data) - position) // 8:
                raise ValueError('Truncated sample counts')
            for _ in range(size):
                address, count = struct.unpack_from('<II', data, position)
                position += 8
                counts[address] += count
        else:
            raise ValueError(f'Unsupported gmon record {tag}; use xgdb --gprof without -pg')
    return counts


def function_ranges(table):
    functions = []
    for line in table.splitlines():
        match = re.match(r'0x([0-9a-fA-F]+)\s+[lg]\s+f\s+\.text\s+0x([0-9a-fA-F]+)\s+(\S+)', line)
        if match and int(match[2], 16):
            start, size = int(match[1], 16), int(match[2], 16)
            functions.append((start, start + size, match[3]))
    return functions


def group_samples(counts, functions):
    grouped = Counter()
    for address, count in counts.items():
        if not count:
            continue
        candidates = [function for function in functions if function[0] <= address < function[1]]
        name = min(candidates, key=lambda item: item[1] - item[0])[2] if candidates else '<unmapped>'
        grouped[name] += count
    return grouped


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True, help='Matching tile application ELF, not bootstrap')
    parser.add_argument('--profile', type=Path, required=True, help='One tile/core .gprof file')
    parser.add_argument('--json', type=Path, help='Optional full result file')
    args = parser.parse_args()
    counts = sample_counts(args.profile.read_bytes())
    total = sum(counts.values())
    if not total:
        parser.error('No PC samples in this profile; select the active decoder core')
    table = subprocess.check_output(['xobjdump', '-t', str(args.elf)], text=True)
    grouped = group_samples(counts, function_ranges(table))
    plm_total = sum(count for name, count in grouped.items() if name.startswith('plm_'))
    rows = [dict(function=name, samples=count, percent_thread=100 * count / total,
                 percent_plm=100 * count / plm_total if plm_total and name.startswith('plm_') else None)
            for name, count in grouped.most_common()]
    result = dict(elf=str(args.elf), profile=str(args.profile), samples=total,
                  sampled_addresses=len(counts), plm_samples=plm_total, functions=rows)
    print(f'PC samples: {total}; PL_MPEG function samples: {plm_total}')
    print(f'{"Function":36} {"Samples":>10} {"% thread":>9} {"% PL_MPEG":>10}')
    for row in rows:
        share = f'{row["percent_plm"]:.2f}' if row['percent_plm'] is not None else '-'
        print(f'{row["function"]:36} {row["samples"]:10d} {row["percent_thread"]:9.2f} {share:>10}')
    print('Percentages are PC-sample shares, including blocked PCs, not precise CPU timings or calls.')
    if args.json:
        args.json.write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
