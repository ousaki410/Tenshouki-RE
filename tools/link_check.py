#!/usr/bin/env python3
"""Validate the top-level table of LINK containers.

Usage: python3 tools/link_check.py <file> [<file> ...]

Checks (see docs/formats/link.md section 6):
  - magic is LINK
  - first offset == 16 + 8 * count
  - every non-empty payload lies inside the file
  - empty entries (size 0) may point at or beyond the end of the file; they are reported
  - gap between consecutive entries is reported as a histogram
  - bytes between the last non-empty payload and the end of the file are reported
The script only reads files. It prints sizes and offsets, never payload bytes.
"""
import struct
import sys


def check(path):
    with open(path, 'rb') as f:
        head = f.read(16)
        f.seek(0, 2)
        size = f.tell()
        if len(head) < 16 or head[:4] != b'LINK':
            print(f'{path}: not a LINK container')
            return False
        count, version, reserved = struct.unpack_from('<III', head, 4)
        f.seek(16)
        raw = f.read(8 * count)
    if len(raw) < 8 * count:
        print(f'{path}: table truncated')
        return False
    entries = [struct.unpack_from('<II', raw, 8 * i) for i in range(count)]
    ok = True
    print(f'{path}: size={size} count={count} version_or_flags={version} reserved={reserved}')
    if entries and entries[0][0] != 16 + 8 * count:
        print(f'  first offset {entries[0][0]} != expected {16 + 8 * count}')
        ok = False
    gaps = {}
    empty_beyond_eof = []
    for i, (off, sz) in enumerate(entries):
        if sz == 0:
            if off > size:
                empty_beyond_eof.append(i)
        elif off + sz > size:
            print(f'  entry {i}: out of bounds (offset={off}, size={sz})')
            ok = False
        if i + 1 < count:
            gap = entries[i + 1][0] - (off + sz)
            gaps[gap] = gaps.get(gap, 0) + 1
    print(f'  gap histogram: {gaps}')
    if empty_beyond_eof:
        print(f'  empty entries with offset beyond EOF: {empty_beyond_eof}')
    nonempty = [(o + s) for o, s in entries if s > 0]
    if nonempty:
        tail = size - max(nonempty)
        print(f'  last non-empty end={max(nonempty)} tail_bytes={tail}')
        if tail not in (0, 4):
            print('  NOTE: tail is neither 0 nor 4')
            ok = False
    print('  RESULT:', 'consistent' if ok else 'INCONSISTENT')
    return ok


if __name__ == '__main__':
    results = [check(p) for p in sys.argv[1:]]
    sys.exit(0 if all(results) else 1)
