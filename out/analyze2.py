import struct, csv, collections, statistics as st
D="scenario"; BASE,REC=0x0ae8,47; NB,NL=0x6c94,24; NSLOT=532
YEAR={1:1534,2:1560,3:1571,4:1582,5:1546,6:1599}
def dec(b): return b.split(b"\0")[0].decode("cp932","replace")
def load(sn):
    d=open(f"{D}/sndata{sn}.dat","rb").read(); o={}
    for s in range(NSLOT):
        n=d[NB+s*NL:NB+s*NL+NL]; a=d[BASE+s*REC:BASE+s*REC+REC]
        pid=struct.unpack_from("<H",n,18)[0]
        if pid==0xffff or n[0]==0: continue
        o[pid]=dict(slot=s,name=dec(n[:9])+dec(n[9:18]),st=n[22],sex=n[23],a=a)
    return o
S={sn:load(sn) for sn in range(1,7)}
print("valid records",[len(S[s]) for s in S],"expected [381,509,514,508,490,479]")
for pid in (0,5,8,18,19,26,37,47):
    print(pid,[S[sn][pid]["name"] if pid in S[sn] else "-" for sn in S])

g=collections.defaultdict(list)
for sn,t in S.items():
    for pid,r in t.items(): g[pid].append(r["a"])
multi=[v for v in g.values() if len(v)>=2]
print("\n== grouped by character ID, count",len(multi))
print("offset constant-ratio mean distinct values")
for off in range(REC):
    same=sum(len({x[off] for x in v})==1 for v in multi)
    dist=st.mean(len({x[off] for x in v}) for v in multi)
    print(f"+{off:<3} {same/len(multi):.2f}  {dist:.2f}")

print("\n== +33 age estimate, delta = derived year - official year")
for sn,t in S.items():
    c=collections.Counter()
    for r in t.values(): c[1454+r["a"][35]+13+r["a"][33]-YEAR[sn]]+=1
    tot=sum(c.values())
    print("sn",sn,"share with delta 0",round(c[0]/tot,3),"top 6",c.most_common(6))
print("\n== sn6 first 25 characters with non-zero delta")
n=0
for pid,r in S[6].items():
    df=1454+r["a"][35]+13+r["a"][33]-YEAR[6]
    if df!=0 and n<25:
        print(pid,r["name"],"b33",r["a"][33],"birth",1454+r["a"][35],"status",hex(r["st"]),"delta",df); n+=1

print("\n== status byte (name record +22) distribution")
for sn,t in S.items():
    print("sn",sn,{hex(k):v for k,v in sorted(collections.Counter(r["st"] for r in t.values()).items())})

print("\n== same character +15,+16,+17 across scenarios")
for pid in (0,5,8,18,19,26,37,47):
    for sn in S:
        if pid in S[sn]:
            a=S[sn][pid]["a"]; print(pid,"sn",sn,a[15],a[16],a[17],"proficiency",struct.unpack_from("<3H",a,9))

with open("out/all_attr.csv","w",newline="",encoding="utf-8") as f:
    w=csv.writer(f); w.writerow(["sn","pid","slot","name","status","sex","raw_hex"])
    for sn,t in S.items():
        for pid,r in t.items(): w.writerow([sn,pid,r["slot"],r["name"],r["st"],r["sex"],r["a"].hex()])
