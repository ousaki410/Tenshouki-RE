import struct, collections
D="scenario"; BASE,REC=0x0ae8,47; NB,NL=0x6c94,24
YEAR={1:1534,2:1560,3:1571,4:1582,5:1546,6:1599}
def dec(b): return b.split(b"\0")[0].decode("cp932","replace")
def load(sn):
    d=open(f"{D}/sndata{sn}.dat","rb").read(); o={}
    for s in range(532):
        n=d[NB+s*NL:NB+s*NL+NL]; a=d[BASE+s*REC:BASE+s*REC+REC]
        pid=struct.unpack_from("<H",n,18)[0]
        if pid==0xffff or n[0]==0: continue
        o[pid]=(dec(n[:9])+dec(n[9:18]),n[22],a)
    return o
S={sn:load(sn) for sn in range(1,7)}
EXP={"織田信長":(198,190,192,98,100),"畠山義綱":(120,112,62,73,74),
     "武田勝頼":(128,180,96,84,89),"前田利常":(158,130,146,81,46)}
for nm,e in EXP.items():
    print("\n==",nm,"expected pol/mil/int/cha/amb =",e)
    for sn,t in S.items():
        for pid,(n,st,a) in t.items():
            if n==nm:
                print(f"sn{sn} id{pid} +6x2={a[6]*2} +7x2={a[7]*2} +8x2={a[8]*2} "
                      f"+15={a[15]} +16={a[16]} +17={a[17]} +18..25={a[18:26].hex(' ')}")
print("\n== +33 mismatches")
for sn,t in S.items():
    bad=[(st,a[33]) for pid,(n,st,a) in t.items() if 1454+a[35]+13+a[33]!=YEAR[sn]]
    print("sn",sn,"mismatches",len(bad),"/",len(t))
    print("   b33 top 8",collections.Counter(b for s,b in bad).most_common(8))
    print("   status top 6",collections.Counter(hex(s) for s,b in bad).most_common(6))
