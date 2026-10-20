import struct, pathlib, sys, math
from collections import Counter
def ent(b):
    if not b: return 0.0
    n=len(b); return round(-sum(c/n*math.log2(c/n) for c in Counter(b).values()),3)
def table(d, base, length):
    if length < 16 or d[base:base+4] != b"LINK": return None
    c = struct.unpack_from("<I", d, base+4)[0]
    if 16+8*c > length or c > 100000: return None
    return c, [struct.unpack_from("<II", d, base+16+8*i) for i in range(c)]
def u32s(b, n):
    return [struct.unpack_from("<I", b, i)[0] for i in range(0, min(len(b), n*4)-3, 4)]
root = pathlib.Path(sys.argv[1]); d = (root/"res_grp.bin").read_bytes()
c0, e0 = table(d, 0, len(d))
leaf_sizes = []; shown = 0
for i, (o, s) in enumerate(e0):
    t = table(d, o, s)
    if t is None:
        print("L1", i, "size", s, "u32", u32s(d[o:o+64], 16)); continue
    c, es = t
    sizes = [x[1] for x in es]; leaf_sizes += sizes
    print("L1", i, "LINK size", s, "count", c, "min/max leaf", min(sizes), max(sizes))
    if shown < 3 and es:
        for j in range(min(3, c)):
            lo, ls = es[j]; b = d[o+lo:o+lo+ls]
            print("   leaf", j, "size", ls, "u32", u32s(b, 12), "ent", ent(b[:4096]))
        shown += 1
print("leaves", len(leaf_sizes), "total", sum(leaf_sizes))
x = (root/"res_exp.bin").read_bytes()
c, es = table(x, 0, len(x)); o, s = es[0]
print("exp outer", c, es, "inner head u32", u32s(x[o:o+64], 16), "inner ent", ent(x[o:o+4096]))
