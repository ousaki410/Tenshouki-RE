#!/usr/bin/env python3
"""verify_resources.py - standalone validator for Tenshouki resource containers

Independent of the C++ parser. Validates the LINK container layout documented
in docs/formats/link.md (16-byte header, 8-byte entry table, 0x89ABCDEF
payload sentinel) and decodes nested LINK/GT1G blocks where present.
Only hashes and metadata are printed, never game content.

Usage:
  verify_resources.py DATA_DIR [--json report.json] [--manifest manifest.json]
                      [--update-manifest] [--strict]
Exit codes: 0 all checks passed | 1 structural error or hash mismatch | 2 unknown format (format discovery needed)
"""
import argparse, hashlib, json, math, struct, sys
from collections import Counter
from pathlib import Path

HEADER_FMT = "<4sIII"          # magic, entry_count, version, reserved
HEADER_SIZE = struct.calcsize(HEADER_FMT)   # 16
ENTRY_FMT = "<II"              # absolute offset, size
ENTRY_SIZE = struct.calcsize(ENTRY_FMT)     # 8
SENTINEL = 0x89ABCDEF
SUPPORTED_VERSION = 1

NESTED_DATA_TABLE_OFFSET = 0x20
NESTED_DESCRIPTOR_OFFSET = 0x40
NESTED_RECORD_SIZE = 12
GT1G_FIXED_HEADER_SIZE = 0x1C
GT1G_TEXTURE_HEADER_SIZE = 20

SIGNATURES = [
    (b"DDS ", "dds"), (b"\x89PNG", "png"), (b"BM", "bmp"), (b"RIFF", "riff/wav"),
    (b"OggS", "ogg"), (b"LINK", "link"), (b"GT1G", "gt1g"), (b"_N1G", "n1g"),
    (b"LWC", "lwc"), (b"LS11", "ls11"),
    (b"DBUG", "dxbc-debug"), (b"DXBC", "dxbc"),
    (b"\x78\x9c", "zlib"), (b"\x78\x01", "zlib"),
    (b"\x78\xda", "zlib"), (b"\x1f\x8b", "gzip"), (b"MZ", "pe"), (b"\x7fELF", "elf"),
    (b"#version", "glsl"), (b"\x03\x02\x23\x07", "spirv"),
]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def entropy(b):
    if not b:
        return 0.0
    n = len(b)
    return -sum(c / n * math.log2(c / n) for c in Counter(b).values())


def sniff(b):
    for sig, name in SIGNATURES:
        if b.startswith(sig):
            return name
    return "unknown"


def discover(data, size):
    """Format discovery: when the magic is not LINK, return clues that can be used to infer the layout."""
    info = {"first_32_hex": data[:32].hex(), "head_entropy_4k": round(entropy(data[:4096]), 3)}
    u32 = [struct.unpack_from("<I", data, i)[0] for i in range(0, min(len(data), 64) - 3, 4)]
    info["first_u32_le"] = u32[:16]
    # assumption: the file starts with a count followed by a monotonically increasing offset table
    if len(u32) >= 4 and 0 < u32[0] < 1_000_000:
        n = u32[0]
        offs = u32[1:1 + min(n, 12)]
        info["hint"] = f"possible count={n}, first_offsets={offs}"
    return info


def parse_gt1g(block):
    """Decode a GT1G 0600 block header and texture offset table (docs/formats/gt1g.md)."""
    if len(block) < GT1G_FIXED_HEADER_SIZE or not block.startswith(b"GT1G"):
        raise ValueError("not a GT1G block")
    if block[4:8] != b"0600":
        raise ValueError(f"unsupported GT1G version tag {block[4:8]!r}")

    total_size, header_size, texture_count = struct.unpack_from("<III", block, 0x08)
    if texture_count == 0 or texture_count > 4096:
        raise ValueError(f"invalid texture_count {texture_count}")
    if header_size != GT1G_FIXED_HEADER_SIZE + 4 * texture_count:
        raise ValueError(f"header_size {header_size} != {GT1G_FIXED_HEADER_SIZE + 4 * texture_count}")
    if total_size > len(block):
        raise ValueError(f"total_size {total_size} > block size {len(block)}")

    effective = total_size or len(block)
    textures = []
    for i in range(texture_count):
        rel = struct.unpack_from("<I", block, header_size + 4 * i)[0]
        start = header_size + rel
        if start + GT1G_TEXTURE_HEADER_SIZE > effective:
            raise ValueError(f"texture {i} header at 0x{start:X} out of bounds")
        mip_info, format_id, size_log2 = block[start], block[start + 1], block[start + 2]
        width = 1 << (size_log2 >> 4)
        height = 1 << (size_log2 & 0x0F)
        data_offset = start + GT1G_TEXTURE_HEADER_SIZE
        next_start = (header_size + struct.unpack_from("<I", block, header_size + 4 * (i + 1))[0]
                      if i + 1 < texture_count else effective)
        if next_start < data_offset or next_start > effective:
            raise ValueError(f"texture {i} data range invalid")
        textures.append({
            "index": i, "width": width, "height": height,
            "format_id": format_id, "mip_info": mip_info,
            "data_offset": data_offset, "data_size": next_start - data_offset,
        })
    return {"version": "0600", "total_size": total_size, "header_size": header_size,
            "texture_count": texture_count, "textures": textures}


def parse_nested(payload):
    """Decode a nested LINK container (docs/formats/link.md section 7)."""
    if len(payload) < NESTED_DATA_TABLE_OFFSET + 8 or not payload.startswith(b"LINK"):
        raise ValueError("payload is not a nested LINK container")

    data_entry_count, data_table_offset, record_count, descriptor_offset = \
        struct.unpack_from("<IIII", payload, 0x04)

    data_table_end = data_table_offset + 8 * data_entry_count
    if data_entry_count == 0 or data_table_offset < NESTED_DATA_TABLE_OFFSET or data_table_end > len(payload):
        raise ValueError(f"data table ({data_entry_count} entries at 0x{data_table_offset:X}) out of bounds")
    if data_table_end > descriptor_offset:
        raise ValueError("data table overlaps the descriptor table")

    descriptor_end = descriptor_offset + record_count * NESTED_RECORD_SIZE
    if descriptor_offset < NESTED_DATA_TABLE_OFFSET or descriptor_end > len(payload):
        raise ValueError("descriptor table out of bounds")

    warnings = []
    data_entries = []
    for i in range(data_entry_count):
        offset, size = struct.unpack_from("<II", payload, data_table_offset + i * 8)
        if offset + size > len(payload):
            raise ValueError(
                f"data block {i} [0x{offset:X}, 0x{offset + size:X}) exceeds payload")
        if offset < descriptor_end:
            warnings.append(f"data block {i} at 0x{offset:X} overlaps the descriptor table")
        data_entries.append({
            "offset": offset,
            "size": size,
            "signature": sniff(payload[offset:offset + 16]),
        })

    predicted = ((descriptor_end) & ~0xF) + 16
    records = []
    for i in range(record_count):
        pos = descriptor_offset + i * NESTED_RECORD_SIZE
        x, y, w, h, tex = struct.unpack_from("<HHHHI", payload, pos)
        records.append({"x": x, "y": y, "w": w, "h": h, "texture_index": tex})

    info = {
        "data_entry_count": data_entry_count,
        "data_table_offset": data_table_offset,
        "record_count": record_count,
        "descriptor_offset": descriptor_offset,
        "descriptor_end": descriptor_end,
        "predicted_data_offset": predicted,
        "data_entries": data_entries,
        "records": records,
    }
    if warnings:
        info["warnings"] = warnings

    for entry in data_entries:
        if entry["signature"] != "gt1g":
            continue
        block = payload[entry["offset"]:entry["offset"] + entry["size"]]
        info["gt1g"] = parse_gt1g(block)
        for rec in records:
            if rec["texture_index"] >= info["gt1g"]["texture_count"]:
                info.setdefault("warnings", []).append(
                    f"record references texture {rec['texture_index']} but only "
                    f"{info['gt1g']['texture_count']} exist")
        break
    return info


def verify_one(path, manifest=None, strict=False):
    errors = []
    warnings = []
    hints = []

    with open(path, "rb") as f:
        data = f.read()
    size = len(data)

    if size < HEADER_SIZE:
        return {"file": str(path), "status": "FAIL", "errors": ["file too small for header"],
                "warnings": [], "hints": [], "sha256": sha256(path), "size": size}

    magic, entry_count, version, reserved = struct.unpack_from(HEADER_FMT, data)

    if magic != b"LINK":
        return {
            "file": str(path), "status": "UNKNOWN_FORMAT",
            "magic": magic.decode("ascii", errors="ignore"),
            "size": size, "sha256": sha256(path),
            "errors": [], "warnings": [], "hints": [discover(data, size)],
        }

    if not (0 < entry_count <= 100000):
        errors.append(f"implausible entry_count {entry_count}")
    if version != SUPPORTED_VERSION:
        warnings.append(f"version {version} != {SUPPORTED_VERSION}")
    if reserved != 0:
        warnings.append(f"reserved header field 0x{reserved:08X} != 0")

    table_end = HEADER_SIZE + entry_count * ENTRY_SIZE
    if table_end > size:
        errors.append(f"entry table ends at {table_end} > file size {size}")
        entries = []
    else:
        entries = []
        for i in range(entry_count):
            offset, length = struct.unpack_from(ENTRY_FMT, data, HEADER_SIZE + i * ENTRY_SIZE)
            entries.append({
                "index": i,
                "name": f"entry_{i:04d}",
                "offset": offset,
                "size": length,
                "signature": sniff(data[offset:offset + 16]) if length else "empty",
            })

        for e in entries:
            if e["size"] == 0:
                if e["offset"] < table_end:
                    warnings.append(f"empty entry {e['index']} offset 0x{e['offset']:X} inside table")
                continue
            if e["offset"] < table_end:
                errors.append(f"entry {e['index']} starts inside the table: 0x{e['offset']:X} < 0x{table_end:X}")
            if e["offset"] + e["size"] > size:
                errors.append(f"entry {e['index']} exceeds file: 0x{e['offset'] + e['size']:X} > 0x{size:X}")
            e["sentinel"] = (e["offset"] + e["size"] + 4 <= size and
                             struct.unpack_from("<I", data, e["offset"] + e["size"])[0] == SENTINEL)
            if not e["sentinel"]:
                warnings.append(f"entry {e['index']} has no sentinel at 0x{e['offset'] + e['size']:X}")

    if entries and entries[0]["size"] > 0 and entries[0]["offset"] != table_end:
        warnings.append(f"first payload starts at 0x{entries[0]['offset']:X}, expected 0x{table_end:X}")

    # The documented gap rule: next.offset == prev.offset + prev.size + 4
    non_empty = [e for e in entries if e["size"] > 0]
    for prev, cur in zip(non_empty, non_empty[1:]):
        expected = prev["offset"] + prev["size"] + 4
        if cur["offset"] != expected:
            warnings.append(f"gap before entry {cur['index']}: 0x{cur['offset']:X} != expected 0x{expected:X}")

    if non_empty and non_empty[-1]["offset"] + non_empty[-1]["size"] + 4 != size:
        warnings.append(f"last payload ends at 0x{non_empty[-1]['offset'] + non_empty[-1]['size']:X}, expected 0x{size - 4:X}")

    # Nested LINK / GT1G validation
    nested_entries = 0
    for e in entries:
        if e["size"] == 0 or e["signature"] != "link":
            continue
        nested_entries += 1
        try:
            nested = parse_nested(data[e["offset"]:e["offset"] + e["size"]])
            e["nested"] = {
                "data_entry_count": nested["data_entry_count"],
                "data_signatures": sorted({b["signature"] for b in nested["data_entries"]}),
                "record_count": nested["record_count"],
                "texture_count": nested.get("gt1g", {}).get("texture_count"),
            }
            first_data = nested["data_entries"][0]["offset"]
            if nested["predicted_data_offset"] != first_data:
                warnings.append(
                    f"entry {e['index']}: first data offset 0x{first_data:X} != "
                    f"predicted 0x{nested['predicted_data_offset']:X}")
            warnings.extend(f"entry {e['index']}: {w}" for w in nested.get("warnings", []))
        except ValueError as exc:
            errors.append(f"entry {e['index']}: nested parse failed: {exc}")

    type_dist = Counter(e["signature"] for e in entries)
    total_data = sum(e["size"] for e in entries)
    coverage = total_data / size if size else 0.0
    sentinel_count = sum(1 for e in entries if e.get("sentinel"))

    file_sha256 = sha256(path)

    if manifest and str(path) in manifest:
        m = manifest[str(path)]
        if m.get("sha256") != file_sha256:
            errors.append(f"manifest hash mismatch: expected {m['sha256']} got {file_sha256}")
        if m.get("size") != size:
            warnings.append(f"manifest size mismatch: expected {m['size']} got {size}")

    if strict and warnings:
        errors.extend(f"strict: {w}" for w in warnings)

    return {
        "file": str(path),
        "status": "PASS" if not errors else "FAIL",
        "magic": "LINK",
        "version": version,
        "file_count": entry_count,
        "table_end": table_end,
        "reserved": reserved,
        "entries": entries[:20],
        "nested_entries": nested_entries,
        "type_distribution": dict(type_dist),
        "sentinel_count": sentinel_count,
        "data_coverage": round(coverage, 4),
        "size": size,
        "sha256": file_sha256,
        "errors": errors,
        "warnings": warnings,
        "hints": hints,
    }


def main():
    ap = argparse.ArgumentParser(description="Tenshouki resource container validation")
    ap.add_argument("data_dir", type=Path, help="data/res directory")
    ap.add_argument("--json", type=Path, help="write a JSON report")
    ap.add_argument("--manifest", type=Path, help="manifest file path")
    ap.add_argument("--update-manifest", action="store_true", help="update/create the manifest")
    ap.add_argument("--strict", action="store_true", help="treat warnings as errors")
    args = ap.parse_args()

    data_dir = args.data_dir
    if not data_dir.exists():
        print(f"Directory does not exist: {data_dir}")
        return 2

    manifest = {}
    if args.manifest and args.manifest.exists():
        with open(args.manifest) as f:
            manifest = json.load(f)

    bin_files = sorted(data_dir.glob("*.bin"))
    if not bin_files:
        print("No .bin files found")
        return 2

    reports = []
    overall_status = 0
    for f in bin_files:
        r = verify_one(f, manifest=manifest, strict=args.strict)
        reports.append(r)
        version = r.get('version', '?')
        sha256_short = r.get('sha256', '?')[:16] if r.get('sha256') else '?'
        print(f"[{r['status']}] {f.name} v{version} entries={r.get('file_count', '?')} "
              f"sentinels={r.get('sentinel_count', '?')} cov={r.get('data_coverage', 0):.2%} sha256={sha256_short}...")
        if r['errors']:
            for e in r['errors']:
                print(f"  ERROR: {e}")
        if r['warnings']:
            for w in r['warnings']:
                print(f"  WARN: {w}")
        if r['hints']:
            for h in r['hints']:
                print(f"  HINT: {h}")
        if r['status'] == "FAIL":
            overall_status = 1
        elif r['status'] == "UNKNOWN_FORMAT" and overall_status == 0:
            overall_status = 2

    if args.update_manifest:
        new_manifest = {}
        for r in reports:
            new_manifest[r["file"]] = {"sha256": r["sha256"], "size": r["size"]}
        if args.manifest:
            with open(args.manifest, "w") as f:
                json.dump(new_manifest, f, indent=2)
            print(f"Manifest updated: {args.manifest}")

    if args.json:
        with open(args.json, "w") as f:
            json.dump({"files": reports, "overall": overall_status}, f, indent=2)
        print(f"JSON report: {args.json}")

    return overall_status


if __name__ == "__main__":
    sys.exit(main())
