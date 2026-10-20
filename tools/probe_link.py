import struct, pathlib, sys
root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "data/res")
for p in sorted(root.glob("*.bin")):
    d = p.read_bytes(); n = len(d)
    if n < 16:
        print(p.name, "too small", n); continue
    magic, count, a, b = struct.unpack_from("<4sIII", d, 0)
    end = 16 + 8 * count
    if magic != b"LINK" or end > n:
        print(p.name, "magic", magic, "count", count, "bad header, file size", n); continue
    ents = [struct.unpack_from("<II", d, 16 + 8 * i) for i in range(count)]
    first_ok = ents[0][0] == end if ents else None
    mono = all(ents[i][0] <= ents[i + 1][0] for i in range(len(ents) - 1))
    inb = all(o + s <= n for o, s in ents)
    last = max((o + s for o, s in ents), default=0)
    print(p.name, "size", n, "count", count, "a,b", (a, b), "first_off==table_end", first_ok,
          "monotonic", mono, "in_bounds", inb, "last_end", last, "tail_pad", n - last)
    print("  entries[:8]", ents[:8])
    print("  sig@off", [d[o:o + 4].hex() for o, s in ents[:8] if s > 0 and o + 4 <= n])
