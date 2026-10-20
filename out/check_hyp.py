import struct, collections, statistics as st
D="scenario"; BASE,REC=0x0ae8,47; NB,NL=0x6c94,24
def dec(b): return b.split(b"\0")[0].decode("cp932","replace")
def load(sn):
    d=open(f"{D}/sndata{sn}.dat","rb").read(); o={}
    for s in range(532):
        n=d[NB+s*NL:NB+s*NL+NL]; a=d[BASE+s*REC:BASE+s*REC+REC]
        pid=struct.unpack_from("<H",n,18)[0]
        if pid==0xffff or n[0]==0: continue
        o[pid]=dict(name=dec(n[:9])+dec(n[9:18]),st=n[22],a=a)
    return o
S={sn:load(sn) for sn in range(1,7)}

print("== 1. top 4 bits of status vs. merit (+18 u16)")
for hn in range(0,8):
    c=collections.Counter(struct.unpack_from("<H",r["a"],18)[0] for t in S.values() for r in t.values() if r["st"]>>4==hn)
    if c: print(hn,sum(c.values()),c.most_common(5))

print("\n== 2. +20 as lineage: groups with equal values (sn2, 3 or more people)")
g=collections.defaultdict(list)
for pid,r in S[2].items(): g[r["a"][20]].append(r["name"])
for k,v in sorted(g.items(),key=lambda x:-len(x[1]))[:12]: print(k,len(v),v[:7])

print("\n== 3. +36,+37 u16 as face IDs")
for sn,t in S.items():
    v=[struct.unpack_from("<H",r["a"],36)[0] for r in t.values()]
    print("sn",sn,"count",len(v),"distinct",len(set(v)),"min",min(v),"max",max(v))

print("\n== 4. sn6 proficiency mean by b33")
for lab,f in (("b33==2",lambda r:r["a"][33]==2),("other",lambda r:r["a"][33]!=2)):
    v=[st.mean(struct.unpack_from("<3H",r["a"],9)) for r in S[6].values() if f(r)]
    print(lab,len(v),"mean",round(st.mean(v)),"min",round(min(v)),"max",round(max(v)))

print("\n== 5. +17 by status")
for stv in (0x10,0x31,0x32,0x42,0x52,0x62):
    v=[r["a"][17] for t in S.values() for r in t.values() if r["st"]==stv]
    if v: print(hex(stv),len(v),"mean",round(st.mean(v),1),"min",min(v),"==100",round(sum(x==100 for x in v)/len(v),2))
