#!/usr/bin/env python3
"""Inspect a nested LINK container stored in one top-level entry of a LINK file.

Usage: python3 tools/link_nested.py <file> <top_level_entry_index>

The script reads the nested header, the descriptor table, and the GT1G header
(see docs/formats/link.md and docs/formats/gt1g.md) and prints statistics
only. It never prints pixel or payload bytes beyond header fields.
"""
import struct
import sys
from collections import Counter


def gt1g(f, start):
    f.seek(start)
    h = f.read(0x1C)
    if len(h) < 0x1C:
        print('GT1G: truncated header')
        return
    total, hsize, cnt = struct.unpack_from('<3I', h, 8)
    f14, f18 = struct.unpack_from('<2I', h, 0x14)
    print(f'GT1G: version={h[4:8]!r} total=0x{total:x} header_size=0x{hsize:x} count={cnt} field14={f14} field18={f18}')
    if cnt == 0 or cnt > 64:
        return
    flags = struct.unpack('<%dI' % cnt, f.read(4 * cnt))
    f.seek(start + hsize)
    offs = struct.unpack('<%dI' % cnt, f.read(4 * cnt))
    print(f'  per-texture u32 at +0x1C: {flags}')
    print('  texture offsets (relative to header_size):', [hex(o) for o in offs])
    ends = list(offs[1:]) + [total - hsize]
    for i, o in enumerate(offs):
        f.seek(start + hsize + o)
        t = f.read(20)
        if len(t) < 20:
            break
        b = t[:8]
        ext = struct.unpack_from('<I', t, 8)[0]
        span = ends[i] - o
        print(f'  tex {i}: head8={b.hex()} byte1=0x{b[1]:02x} byte2=0x{b[2]:02x} (hi={b[2] >> 4}, lo={b[2] & 15}) ext=0x{ext:x} span=0x{span:x} span-20=0x{span - 20:x}')


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
            print(f'entry {idx} is not a nested LINK (first 4 bytes: {hdr[:4]!r}, outer_size=0x{outer_size:x})')
            return 1
        ver, table_off, n, desc_off = struct.unpack_from('<4I', hdr, 4)
        f.seek(base + table_off)
        data_off, data_size = struct.unpack('<II', f.read(8))
        f.seek(base + desc_off)
        raw = f.read(12 * n)
        f.seek(base + data_off)
        tag = f.read(8)
        print(f'entry {idx} base=0x{base:x} outer_size=0x{outer_size:x}')
        print(f'version={ver} table_off=0x{table_off:x} n={n} desc_off=0x{desc_off:x}')
        print(f'data entry: off=0x{data_off:x} size=0x{data_size:x} (off+size=0x{data_off + data_size:x}, outer=0x{outer_size:x})')
        desc_end = desc_off + 12 * n
        predicted = (desc_end & ~0xF) + 0x10
        print(f'descriptor end=0x{desc_end:x} predicted data start=0x{predicted:x} actual=0x{data_off:x}')
        print(f'data tag (8 bytes): {tag!r}')
        if tag[:4] == b'GT1G':
            gt1g(f, base + data_off)
    recs = [struct.unpack_from('<HHHHI', raw, 12 * i) for i in range(len(raw) // 12)]
    if len(recs) != n:
        print(f'descriptor table truncated: {len(recs)} of {n}')
    by_layer = {}
    for i, r in enumerate(recs):
        by_layer.setdefault(r[4], []).append((i, r))
    for layer, items in sorted(by_layer.items()):
        mx = max(r[0] + r[2] for _, r in items)
        my = max(r[1] + r[3] for _, r in items)
        print(f'layer {layer}: records={len(items)} max x+w={mx} max y+h={my}')
    nonzero = [(i, r) for i, r in enumerate(recs) if r[4] != 0]
    for i, r in nonzero[:10]:
        print(f'  record {i} with layer {r[4]}: x={r[0]} y={r[1]} w={r[2]} h={r[3]}')
    wide = [(i, r) for i, r in enumerate(recs) if r[0] + r[2] > 1024 or r[1] + r[3] > 1024]
    print(f'records beyond 1024 in x or y: {len(wide)}')
    for i, r in wide[:10]:
        print(f'  record {i}: x={r[0]} y={r[1]} w={r[2]} h={r[3]} layer={r[4]}')
    sizes = Counter((r[2], r[3]) for r in recs)
    print('most common sizes (w,h):', sizes.most_common(6))
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    try:
        sys.exit(main(sys.argv[1], int(sys.argv[2])))
    except Exception as exc:
        print(f'error: {exc}')
        sys.exit(1)
