import struct, csv, collections, statistics as st, os
D="scenario"; BASE,REC=0x0ae8,47; NB,NL=0x6c94,24; NSLOT=532
os.makedirs("out",exist_ok=True)

def load(sn):
    d=open(f"{D}/sndata{sn}.dat","rb").read(); o={}
    for s in range(NSLOT):
        n=d[NB+s*NL:NB+s*NL+NL]
        a=d[BASE+s*REC:BASE+s*REC+REC]
        if len(n)<NL or len(a)<REC or n[0]==0: continue
        nm=n[:18].split(b"\0")[0].decode("cp932","replace")
        o[s]=dict(name=nm,tail=n[18:],a=a)
    return o

S={sn:load(sn) for sn in range(1,7)}
ALIAS={"織田信長":["織田信長"],"羽柴秀吉":["羽柴秀吉"],"徳川家康":["徳川家康","松平元康"],
"北条氏康":["北条氏康"],"武田信玄":["武田信玄","武田晴信"],
"上杉謙信":["上杉謙信","上杉政虎","長尾景虎"],"毛利元就":["毛利元就"],"島津義久":["島津義久","島津龍伯"]}
EID={"織田信長":0,"羽柴秀吉":5,"徳川家康":8,"北条氏康":18,"武田信玄":19,"上杉謙信":26,"毛利元就":37,"島津義久":47}
key=[(lab,sn,s,r) for sn,t in S.items() for s,r in t.items() for lab,al in ALIAS.items() if r["name"] in al]
print("关键人物记录数",len(key))

def val(blob,off,w,r):
    b=r[blob]
    return int.from_bytes(b[off:off+w],"little") if off+w<=len(b) else None

print("\n== 人物ID字段候选(前8)")
cand=[]
for blob,n in (("a",REC),("tail",6)):
    for w in (1,2):
        for off in range(n-w+1):
            ok=sum(val(blob,off,w,r)==EID[l] for l,_,_,r in key)
            cand.append((ok,blob,off,w))
for ok,blob,off,w in sorted(cand,reverse=True)[:8]: print(f"{blob} +{off} w={w} 命中 {ok}/{len(key)}")

def first(l,sn):
    for ll,s_,_,r in key:
        if ll==l and s_==sn: return r

print("\n== 状态字节候选")
for blob,n in (("a",REC),("tail",6)):
    for off in range(n):
        try:
            a=[val(blob,off,1,first("織田信長",s)) for s in (2,3,4)]
            a5=val(blob,off,1,first("織田信長",5))
            b=[val(blob,off,1,first("徳川家康",s)) for s in (3,4,6)]
            b2=val(blob,off,1,first("徳川家康",2))
        except Exception: continue
        if len(set(a))==1 and a[0]!=a5 and len(set(b))==1 and b[0]!=b2:
            print(f"{blob} +{off}: 信长 {a[0]:#x}->{a5:#x}  家康 {b[0]:#x}<-{b2:#x}")

print("\n== 各偏移跨剧本稳定比例(同名人物)")
g=collections.defaultdict(list)
for sn,t in S.items():
    for s,r in t.items(): g[r["name"]].append(r["a"])
multi=[v for v in g.values() if len(v)>1]
print("同名跨剧本人数",len(multi))
for off in range(REC):
    same=sum(len({x[off] for x in v})==1 for v in multi)
    print(f"+{off:<3} {same/len(multi):.2f}")

print("\n== 熟练值(+9,+11,+13 u16)范围")
ks=[]
for t in S.values():
    for r in t.values(): ks+=[struct.unpack_from("<3H",r["a"],9)[i] for i in range(3)]
inr=sum(500<=k<=2000 for k in ks)/len(ks)
print("最小",min(ks),"最大",max(ks),"落在500-2000比例",round(inr,3))

print("\n== +33 年龄推测:出生年+13+b33 的年份分布")
for sn,t in S.items():
    ys=[1454+r["a"][35]+13+r["a"][33] for r in t.values() if 20<r["a"][35]<150]
    if ys: print("sn",sn,"中位",st.median(ys),"最小",min(ys),"最大",max(ys),"人数",len(ys))

with open("out/all_attr.csv","w",newline="",encoding="utf-8") as f:
    w=csv.writer(f); w.writerow(["sn","slot","name","tail_hex","raw_hex"])
    for sn,t in S.items():
        for s,r in t.items(): w.writerow([sn,s,r["name"],r["tail"].hex(),r["a"].hex()])
print("\n已写出 out/all_attr.csv")
