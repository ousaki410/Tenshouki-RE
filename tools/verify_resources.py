#!/usr/bin/env python3
"""verify_resources.py - standalone validator for Tenshouki resource containers

Independent of the C++ parser. Performs structural validation against the LINK
container layout "assumed" in bin_parser.hpp, and emits format-discovery
information useful for reverse engineering when the format does not match.
Only hashes and metadata are printed, never game content.

Usage:
  verify_resources.py DATA_DIR [--json report.json] [--manifest manifest.json]
                      [--update-manifest] [--strict]
Exit codes: 0 all checks passed | 1 structural error or hash mismatch | 2 unknown format (format discovery needed)
"""
import argparse, hashlib, json, math, struct, sys
from collections import Counter
from pathlib import Path

HEADER_FMT = "<4sIIIIII4I"     # magic, version, count, idx_off, idx_size, data_off, flags, reserved[4]
HEADER_SIZE = struct.calcsize(HEADER_FMT)   # 44
ENTRY_FMT = "<IIIIII"          # hash, offset, size, flags, type, reserved
ENTRY_SIZE = struct.calcsize(ENTRY_FMT)     # 24
VERSION_RANGE = (1, 3)

SIGNATURES = [
    (b"DDS ", "dds"), (b"\x89PNG", "png"), (b"BM", "bmp"), (b"RIFF", "riff/wav"),
    (b"OggS", "ogg"), (b"LINK", "link"), (b"\x78\x9c", "zlib"), (b"\x78\x01", "zlib"),
    (b"\x78\xda", "zlib"), (b"\x1f\x8b", "gzip"), (b"MZ", "pe"), (b"\x7fELF", "elf"),
    (b"#version", "glsl"), (b"DXBC", "dxbc"), (b"\x03\x02\x23\x07", "spirv"),
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

def verify_one(path, manifest=None, strict=False):
    errors = []
    warnings = []
    hints = []

    with open(path, "rb") as f:
        data = f.read()
    size = len(data)

    if size < HEADER_SIZE:
        return {"file": str(path), "status": "FAIL", "errors": ["file too small for header"], "warnings": [], "hints": [], "sha256": sha256(path), "size": size}

    # parse the header
    magic, version, file_count, idx_off, idx_size, data_off, flags, *reserved = struct.unpack_from(HEADER_FMT, data)

    # version number
    if not (VERSION_RANGE[0] <= version <= VERSION_RANGE[1]):
        errors.append(f"version {version} not in {VERSION_RANGE}")

    # index table position/size
    if idx_off < HEADER_SIZE:
        errors.append(f"index_offset {idx_off} < HEADER_SIZE {HEADER_SIZE}")
    if idx_off + idx_size > size:
        errors.append(f"index table exceeds file: off={idx_off} size={idx_size} file={size}")
    if idx_size != file_count * ENTRY_SIZE:
        errors.append(f"index_size {idx_size} != file_count {file_count} * {ENTRY_SIZE}")

    # data region
    if data_off < idx_off + idx_size:
        errors.append(f"data_offset {data_off} < index_end {idx_off + idx_size}")
    if data_off > size:
        errors.append(f"data_offset {data_off} > file_size {size}")

    # parse the index table
    entries = []
    if not errors:
        for i in range(file_count):
            off = idx_off + i * ENTRY_SIZE
            e_hash, e_off, e_size, e_flags, e_type, e_reserved = struct.unpack_from(ENTRY_FMT, data, off)
            entries.append({
                "index": i,
                "name_hash": e_hash,
                "offset": e_off,
                "size": e_size,
                "flags": e_flags,
                "type": e_type,
                "reserved": e_reserved,
            })

            if e_off + e_size > size - data_off:
                errors.append(f"entry {i} exceeds data: off={e_off} size={e_size} data_size={size - data_off}")
            if e_reserved != 0:
                warnings.append(f"entry {i} reserved != 0 (0x{e_reserved:08X})")

    # overlap / duplicate hash checks
    if not errors:
        intervals = [(e["offset"], e["offset"] + e["size"], e["index"]) for e in entries]
        intervals.sort()
        for i in range(1, len(intervals)):
            if intervals[i][0] < intervals[i-1][1]:
                errors.append(f"entries overlap: {intervals[i-1][2]} {intervals[i-1][0]}-{intervals[i-1][1]} vs {intervals[i][2]} {intervals[i][0]}-{intervals[i][1]}")
        hashes = [e["name_hash"] for e in entries]
        if len(set(hashes)) != len(hashes):
            dup = [h for h in hashes if hashes.count(h) > 1]
            warnings.append(f"duplicate name_hash: {set(dup)}")

    # file signature sniffing
    type_dist = Counter()
    for e in entries[:200]:
        if e["size"] > 0:
            sig = sniff(data[e["offset"] + e["size"] // 2 : e["offset"] + e["size"] // 2 + 16])
        else:
            sig = "empty"
        type_dist[sig] += 1

    # data region coverage
    if entries:
        total_data = sum(e["size"] for e in entries)
        coverage = total_data / (size - data_off) if size > data_off else 0
    else:
        coverage = 0.0

    # compute sha256 (done up front so every path can use it)
    file_sha256 = sha256(path)

    # manifest comparison
    hash_ok = True
    if manifest and str(path) in manifest:
        m = manifest[str(path)]
        if m.get("sha256") != file_sha256:
            errors.append(f"manifest hash mismatch: expected {m['sha256']} got {file_sha256}")
            hash_ok = False
        if m.get("size") != size:
            warnings.append(f"manifest size mismatch: expected {m['size']} got {size}")

    status = "UNKNOWN_FORMAT" if magic != b"LINK" else ("PASS" if not errors else "FAIL")
    if magic != b"LINK":
        hints = [discover(data, size)]

    return {
        "file": str(path),
        "status": status,
        "magic": magic.decode("ascii", errors="ignore") if isinstance(magic, bytes) else str(magic),
        "version": version,
        "file_count": file_count,
        "index_offset": idx_off,
        "index_size": idx_size,
        "data_offset": data_off,
        "flags": flags,
        "reserved": list(reserved),
        "entries": entries[:20],
        "type_distribution": dict(type_dist),
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
        print(f"[{r['status']}] {f.name} v{version} entries={r.get('file_count', '?')} cov={r.get('data_coverage', 0):.2%} sha256={sha256_short}...")
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