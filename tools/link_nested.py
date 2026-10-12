#!/usr/bin/env python3
"""Inspect a nested LINK container stored in one top-level entry of a LINK file.

Usage: python3 tools/link_nested.py <file> <top_level_entry_index>

The script reads the nested header and the descriptor table (see
docs/formats/link.md section 7) and prints statistics only. It never prints
payload bytes except the 8-byte format tag at the start of the data section.
"""
import struct
import sys
from collections import Counter


def main(path, idx):
    with open(path, 'rb') as f:
        top = f.read(16)
        if top[:4] != b'LINK':
            print('not a LINK file')
            return 1
        count = struct.unpack_from('<I', top, 4)[0]
        if idx >= count:
            print(f'index {idx} out of range (count={count})')
            return 1
        f.seek(16 + 8 * idx)
        base, outer_size = struct.unpack('<II', f.read(8))
        f.seek(base)
        hdr = f.read(0x40)
        if hdr[:4] != b'LINK':
            print('entry is not a nested LINK')
            return 1
        ver, table_off, n, desc_off = struct.unpack_from('<4I', hdr, 4)
        f.seek(base + table_off)
        data_off, data_size = struct.unpack('<II', f.read(8))
        f.seek(base + desc_off)
        raw = f.read(12 * n)
        f.seek(base + data_off)
        tag = f.read(8)
    recs = [struct.unpack_from('<HHHHI', raw, 12 * i) for i in range(len(raw) // 12)]
    print(f'entry base=0x{base:x} outer_size=0x{outer_size:x}')
    print(f'version={ver} table_off=0x{table_off:x} n={n} desc_off=0x{desc_off:x}')
    print(f'data entry: off=0x{data_off:x} size=0x{data_size:x} (off+size=0x{data_off + data_size:x}, outer=0x{outer_size:x})')
    desc_end = desc_off + 12 * n
    predicted = (desc_end & ~0xF) + 0x10
    print(f'descriptor end=0x{desc_end:x} predicted data start=0x{predicted:x} actual=0x{data_off:x}')
    print(f'data tag (8 bytes): {tag!r}')
    print(f'data size low 12 bits: 0x{data_size & 0xFFF:x}')
    if len(recs) != n:
        print(f'descriptor table truncated: {len(recs)} of {n}')
    layers = Counter(r[4] for r in recs)
    print(f'last-u32 values (count): {dict(sorted(layers.items()))}')
    if recs:
        print(f'max x+w={max(r[0] + r[2] for r in recs)} max y+h={max(r[1] + r[3] for r in recs)}')
        sizes = Counter((r[2], r[3]) for r in recs)
        print('most common sizes (w,h): ', sizes.most_common(8))
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], int(sys.argv[2])))
