#!/usr/bin/env python3
"""Validate the top-level table of LINK containers.

Usage: python3 tools/link_check.py <file> [<file> ...]

Checks (see docs/formats/link.md section 6):
  - magic is LINK
  - first offset == 16 + 8 * count
  - every payload lies inside the file
  - gap between consecutive payloads is reported
  - last end + 4 is compared with the file size
The script only reads files. It prints sizes and offsets, never payload bytes.
"""
import struct
import sys


def check(path):
    with open(path, 'rb') as f:
        data_head = f.read(16)
        f.seek(0, 2)
        size = f.tell()
        if len(data_head) < 16 or data_head[:4] != b'LINK':
            print(f'{path}: not a LINK container')
            return False
        count, version, reserved = struct.unpack_from('<III', data_head, 4)
        f.seek(16)
        raw = f.read(8 * count)
    if len(raw) < 8 * count:
        print(f'{path}: table truncated')
        return False
    entries = [struct.unpack_from('<II', raw, 8 * i) for i in range(count)]
    ok = True
    print(f'{path}: size={size} count={count} version_or_flags={version} reserved={reserved}')
    expected_first = 16 + 8 * count
    if entries and entries[0][0] != expected_first:
        print(f'  first offset {entries[0][0]} != expected {expected_first}')
        ok = False
    gaps = {}
    for i, (off, sz) in enumerate(entries):
        if off + sz > size:
            print(f'  entry {i}: out of bounds (offset={off}, size={sz})')
            ok = False
        if i + 1 < count:
            gap = entries[i + 1][0] - (off + sz)
            gaps[gap] = gaps.get(gap, 0) + 1
    print(f'  gap histogram: {gaps}')
    if entries:
        last_end = entries[-1][0] + entries[-1][1]
        print(f'  last_end={last_end} last_end+4={last_end + 4} file_size={size}')
        if last_end + 4 != size:
            ok = False
    print('  RESULT:', 'consistent' if ok else 'INCONSISTENT')
    return ok


if __name__ == '__main__':
    results = [check(p) for p in sys.argv[1:]]
    sys.exit(0 if all(results) else 1)
