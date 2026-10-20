#!/usr/bin/env python3
"""gt1g_extract.py - strict GT1G 0600 texture extractor

Usage:
  python3 tools/gt1g_extract.py <link_file> <entry_index> [texture_index]
  python3 tools/gt1g_extract.py <link_file> --list
  python3 tools/gt1g_extract.py <link_file> --extract-all

Only strictly conforming GT1G 0600 data is accepted:
- top-level LINK magic = "LINK"
- nested entry must carry the LINK magic
- nested header: version=1, table_off=0x20, desc_off=0x40
- data-region tag must be "GT1G0600"
- GT1G header: version=b"0600", field14=0, field18=0
- texture header: byte2 high nibble=log2(width), low nibble=log2(height)
- pixel data: 4 bytes/pixel (BGRA8888)

Output: 4 PNGs (RGBA/BGRA x normal/flipped) for visual comparison
"""

import struct
import sys
import os
from pathlib import Path
try:
    from PIL import Image
except ImportError:
    print("Pillow is required: pip install Pillow")
    sys.exit(1)


def parse_top_level_link(f):
    """Parse the top-level LINK file and return (base, outer_size) for every entry."""
    f.seek(0)
    magic = f.read(4)
    if magic != b'LINK':
        raise ValueError("not a valid LINK file")
    count = struct.unpack('<I', f.read(4))[0]
    
    entries = []
    for i in range(count):
        base, outer_size = struct.unpack('<II', f.read(8))
        entries.append((base, outer_size))
    return entries


def read_entry_header(f, base):
    """Read the header of a nested entry (0x40 bytes)."""
    f.seek(base)
    hdr_data = f.read(0x40)
    if len(hdr_data) < 0x40 or hdr_data[:4] != b'LINK':
        return None
    ver, table_off, n, desc_off = struct.unpack_from('<4I', hdr_data, 4)
    data_off, data_size = struct.unpack('<II', hdr_data[8:16])
    return {
        'version': ver,
        'table_off': table_off,
        'n': n,
        'desc_off': desc_off,
        'data_off': data_off,
        'data_size': data_size,
    }


def read_gt1g_header(f, offset):
    """Read the GT1G header (0x1C bytes)."""
    f.seek(offset)
    hdr = f.read(0x1C)
    if len(hdr) < 0x1C:
        return None
    if hdr[:8] != b'GT1G0600':
        return None
    total, hsize, cnt = struct.unpack_from('<3I', hdr, 8)
    f14, f18 = struct.unpack_from('<2I', hdr, 0x14)
    if f14 != 0 or f18 != 0:
        return None
    return {
        'total': total,
        'header_size': hsize,
        'count': cnt,
    }


def parse_texture_header(f, start, header_size, count, total_size):
    """Parse the per-texture header information."""
    f.seek(start + 0x1C)
    flags = struct.unpack('<%dI' % count, f.read(4 * count))
    f.seek(start + header_size)
    offs = list(struct.unpack('<%dI' % count, f.read(4 * count)))
    ends = list(offs[1:]) + [total_size - header_size]
    
    textures = []
    for i, o in enumerate(offs):
        f.seek(start + header_size + o)
        t = f.read(20)
        if len(t) < 20:
            continue
        b = t[:8]
        ext = struct.unpack_from('<I', t, 8)[0]
        span = ends[i] - o
        byte1 = b[1]
        byte2 = b[2]
        w_log2 = byte2 >> 4
        h_log2 = byte2 & 0xF
        width = 1 << w_log2
        height = 1 << h_log2
        expected = width * height * 4
        textures.append({
            'index': i,
            'offset': o,
            'byte1': b[1],
            'byte2': b[2],
            'w_log2': w_log2,
            'h_log2': h_log2,
            'width': width,
            'height': height,
            'expected_size': expected,
            'actual_span': span,
            'ext': ext,
            'head8': b.hex(),
        })
    return textures


def extract_texture(f, start, texture, header_size):
    """Extract the pixel data of a single texture."""
    expected = texture['width'] * texture['height'] * 4
    actual = texture['actual_span']
    
    if actual != expected:
        print(f"  Warning: texture {texture['index']} span=0x{texture['actual_span']:x} != expected 0x{expected:x}")
    
    f.seek(start + header_size + texture['offset'])
    data = f.read(expected)
    if len(data) < expected:
        # fall back to the actual span if the read came up short
        f.seek(start + header_size + texture['offset'])
        data = f.read(actual)
    
    return data


def save_png_variants(data, width, height, out_prefix):
    """Save the 4 variants: RGBA/BGRA x normal/flipped."""
    variants = []
    
    # 1. BGRA -> RGBA (standard)
    img = Image.frombytes('RGBA', (width, height), data, 'raw', 'BGRA')
    img.save(f'{out_prefix}_bgra.png')
    variants.append(f'{out_prefix}_bgra.png')
    
    # 2. BGRA + vertical flip
    img = Image.frombytes('RGBA', (width, height), data, 'raw', 'BGRA')
    img = img.transpose(Image.FLIP_TOP_BOTTOM)
    img.save(f'{out_prefix}_bgra_flip.png')
    variants.append(f'{out_prefix}_bgra_flip.png')
    
    # 3. RGBA written directly
    img = Image.frombytes('RGBA', (width, height), data, 'raw', 'RGBA')
    img.save(f'{out_prefix}_rgba.png')
    variants.append(f'{out_prefix}_rgba.png')
    
    # 4. RGBA + vertical flip
    img = Image.frombytes('RGBA', (width, height), data, 'raw', 'RGBA')
    img = img.transpose(Image.FLIP_TOP_BOTTOM)
    img.save(f'{out_prefix}_rgba_flip.png')
    variants.append(f'{out_prefix}_rgba_flip.png')
    
    return variants


def extract_entry(file_path, base, outer_size, entry_idx, texture_idx=None):
    """Extract the textures of a single entry."""
    with open(file_path, 'rb') as f:
        f.seek(base)
        hdr_data = f.read(0x40)
        if len(hdr_data) < 0x40 or hdr_data[:4] != b'LINK':
            print(f'Entry {entry_idx}: not a nested LINK (magic mismatch)')
            return False
        
        ver, table_off, n, desc_off = struct.unpack_from('<4I', hdr_data, 4)
        data_off, data_size = struct.unpack('<II', hdr_data[8:16])
        
        hdr = {
            'version': ver,
            'table_off': table_off,
            'n': n,
            'desc_off': desc_off,
            'data_off': data_off,
            'data_size': data_size,
        }
        
        if hdr['version'] != 1:
            print(f'Entry {entry_idx}: version {hdr["version"]} != 1, skipping')
            return False
        if hdr['table_off'] != 0x20:
            print(f'Entry {entry_idx}: table_off=0x{hdr["table_off"]:x} != 0x20')
            return False
        if hdr['desc_off'] != 0x40:
            print(f'Entry {entry_idx}: desc_off=0x{hdr["desc_off"]:x} != 0x40')
            return False
        
        # locate the GT1G0600 tag
        f.seek(base)
        entry_data = f.read(outer_size)
        gt1g_offset = entry_data.find(b'GT1G0600')
        if gt1g_offset < 0:
            print(f'Entry {entry_idx}: GT1G0600 tag not found')
            return False
        
        gt1g_offset = base + gt1g_offset
        f.seek(gt1g_offset)
        tag = f.read(8)
        if tag != b'GT1G0600':
            print(f'Entry {entry_idx}: GT1G tag mismatch')
            return False
        
        # parse the GT1G header
        gt1g = read_gt1g_header(f, gt1g_offset)
        if gt1g is None:
            print(f'Entry {entry_idx}: invalid GT1G header')
            return False
        
        if gt1g['count'] == 0 or gt1g['count'] > 64:
            print(f'Entry {entry_idx}: invalid texture count {gt1g["count"]}')
            return False
        
        # parse the texture headers
        textures = parse_texture_header(f, gt1g_offset, gt1g['header_size'], gt1g['count'], gt1g['total'])
        
        # select the textures to extract
        if texture_idx is not None:
            if texture_idx >= len(textures):
                print(f'Texture index {texture_idx} out of range (0-{len(textures)-1})')
                return False
            textures = [textures[texture_idx]]
        
        # extract each texture
        for tex in textures:
            if tex['actual_span'] != tex['expected_size']:
                print(f"  Texture {tex['index']}: span=0x{tex['actual_span']:x} != expected 0x{tex['expected_size']:x} (possible mismatch)")
            
            # read the pixel data
            pixel_offset = gt1g_offset + gt1g['header_size'] + tex['offset']
            f.seek(pixel_offset)
            data = f.read(tex['expected_size'])
            if len(data) < tex['expected_size']:
                # fall back to the actual span if the read came up short
                f.seek(pixel_offset)
                data = f.read(tex['actual_span'])
            
            # save the 4 variants
            out_prefix = f'extracted_e{entry_idx}_t{tex["index"]}_{tex["width"]}x{tex["height"]}'
            variants = save_png_variants(data, tex['width'], tex['height'], out_prefix)
            print(f'Extracted: {out_prefix} ({tex["width"]}x{tex["height"]}) -> {variants}')
        
        return True


def list_entries(file_path):
    """List all top-level entries."""
    with open(file_path, 'rb') as f:
        entries = parse_top_level_link(f)
    
    print(f'Top-level entries: {len(entries)}')
    for i, (base, outer_size) in enumerate(entries):
        with open(file_path, 'rb') as f:
            f.seek(base)
            hdr_data = f.read(0x40)
            if len(hdr_data) >= 0x40 and hdr_data[:4] == b'LINK':
                ver, table_off, n, desc_off = struct.unpack_from('<4I', hdr_data, 4)
                data_off, data_size = struct.unpack('<II', hdr_data[8:16])
                print(f'  [{i}] base=0x{base:x} outer=0x{outer_size:x} ver={ver} n={n} data_off=0x{data_off:x} size=0x{data_size:x}')
            else:
                print(f'  [{i}] base=0x{base:x} outer=0x{outer_size:x} (not nested LINK)')
    return 0


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    
    file_path = sys.argv[1]
    
    if not os.path.exists(file_path):
        print(f'File not found: {file_path}')
        return 1
    
    with open(file_path, 'rb') as f:
        entries = parse_top_level_link(f)
    
    if len(sys.argv) == 2 or (len(sys.argv) == 3 and sys.argv[2] == '--list'):
        return list_entries(file_path)
    
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    
    if sys.argv[2] == '--list':
        return list_entries(file_path)
    
    if sys.argv[2] == '--extract-all':
        for i in range(len(entries)):
            extract_entry(file_path, entries[i][0], entries[i][1], i)
        return 0
    
    entry_idx = int(sys.argv[2])
    texture_idx = int(sys.argv[3]) if len(sys.argv) > 3 else None
    
    if entry_idx >= len(entries):
        print(f'Entry index {entry_idx} out of range (0-{len(entries)-1})')
        return 1
    
    success = extract_entry(file_path, entries[entry_idx][0], entries[entry_idx][1], entry_idx, texture_idx)
    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
