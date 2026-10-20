import struct, pathlib, sys
from collections import Counter
def parse(d, base, length):
    if length < 16 or d[base:base+4] != b"LINK": return None
    count = struct.unpack_from("<I", d, base+4)[0]
    if 16 + 8*count > length or count > 100000: return None
    return [struct.unpack_from("<II", d, base+16+8*i) for i in range(count)]
def walk(d, base, length, depth, maxd, stats, gaps):
    ents = parse(d, base, length)
    if ents is None:
        stats[(depth, d[base:base+4].hex() if length >= 4 else "short")] += 1
        return
    stats[(depth, "LINK")] += 1
    for o, s in ents:
        if o + s > length:
            stats[(depth, "oob")] += 1; continue
        if o + s + 4 <= length:
            gaps[d[base+o+s:base+o+s+4].hex()] += 1
        if s and depth < maxd:
            walk(d, base+o, s, depth+1, maxd, stats, gaps)
        elif s:
            stats[(depth+1, "cut:" + d[base+o:base+o+4].hex())] += 1
root = pathlib.Path(sys.argv[1]); name = sys.argv[2]; maxd = int(sys.argv[3])
d = (root/name).read_bytes()
stats, gaps = Counter(), Counter()
walk(d, 0, len(d), 0, maxd, stats, gaps)
for k, v in sorted(stats.items()): print(k, v)
print("gap4 values", gaps.most_common(8))
