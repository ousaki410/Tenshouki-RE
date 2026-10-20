import struct, pathlib, sys, math
from collections import Counter
def ent(b):
    if not b: return 0.0
    n=len(b); return round(-sum(c/n*math.log2(c/n) for c in Counter(b).values()),3)
def u32s(b, n):
    return [struct.unpack_from("<I", b, i)[0] for i in range(0, min(len(b), n*4)-3, 4)]
def table(d, base, length):
    if length < 16 or d[base:base+4] != b"LINK": return None
    c = struct.unpack_from("<I", d, base+4)[0]
    if 16+8*c > length or c > 100000: return None
    return c, [struct.unpack_from("<II", d, base+16+8*i) for i in range(c)]
root = pathlib.Path(sys.argv[1]); d = (root/"res_grp.bin").read_bytes()
c0, e0 = table(d, 0, len(d))
print("== variant containers (u32@8 != 1)")
for i, (o, s) in enumerate(e0):
    if d[o:o+4] != b"LINK": continue
    h = u32s(d[o:o+32], 8)
    if h[2] == 1: continue
    pos = [(p, struct.unpack_from("<I", d, o+p)[0]) for p in range(32, min(s, 4096)-3, 4) if struct.unpack_from("<I", d, o+p)[0]]
    print("L1", i, "size", s, "hdr", h, "nz@32+", pos[:24], "nz_count", len(pos))
    if len(pos) >= 2:
        off, sz = pos[0][1], pos[1][1]
        if off + sz <= s and sz:
            pw = 1 << (sz.bit_length()-1); sub = sz - pw
            print("   payload off", off, "size", sz, "pow2", pw, "sub", sub, "subhdr u32", u32s(d[o+off:o+off+min(sub,128)], 32))
print("== indexed containers")
for i in (15, 16, 17, 20):
    o, s = e0[i]; c, es = table(d, o, s)
    sig = Counter(); shown = 0
    for lo, ls in es:
        if ls >= 4: sig[d[o+lo:o+lo+4].hex()] += 1
    print("L1", i, "count", c, "sigs", sig.most_common(5))
    for lo, ls in es:
        if ls >= 64 and shown < 3:
            b = d[o+lo:o+lo+ls]; print("   leaf size", ls, "u32", u32s(b, 12), "ent", ent(b[:4096])); shown += 1
