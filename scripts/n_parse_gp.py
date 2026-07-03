#!/usr/bin/env python3
# γ.5-D1 N v2: 分字段独立正则，避开跨 CPU 穿插
import re, os, glob
from collections import Counter

LOG_DIR = "build/log"
GP_RUNS = [5, 8, 10, 11, 15, 16, 18, 19, 20, 25]

def hex_extract(text, key):
    """在 text 中查找 key=0x<hex>，返回所有匹配的 hex 字符串（无 0x）"""
    return re.findall(re.escape(key) + r"=0x([0-9A-Fa-f]+)", text)

def find_after(text, anchor, key):
    """在 anchor 之后 200 字符内找 key=0xhex"""
    idx = text.find(anchor)
    if idx < 0: return None
    window = text[idx:idx+500]
    m = re.search(re.escape(key) + r"=0x([0-9A-Fa-f]+)", window)
    return m.group(1) if m else None

def parse(path):
    raw = open(path, 'rb').read().decode('utf-8', errors='replace').replace('\r','').replace('\n',' ')
    # 缩小到 [x86_64][exception] 段附近
    r = {}

    # 1) exception 主帧（穿插严重，只信 vector/err/cr2 独立字段，不信位置相关）
    #    vector/err 在 anchor "[exception]" 开头就出，通常无穿插
    m = re.search(r"exception\] vector=0x([0-9A-Fa-f]+)\s+err=0x([0-9A-Fa-f]+)", raw)
    if m: r['exc_vec'], r['exc_err'] = m.group(1), m.group(2)
    # rip/cs/rsp/ss 独立找一次全局，看是否有多个候选
    r['exc_rips'] = re.findall(r"exception\] vector=[^ ]+ err=[^ ]+ rip=0x([0-9A-Fa-f]+)", raw)
    r['exc_rsps'] = re.findall(r"rsp=0x([0-9A-Fa-f]+) ss=0x", raw)
    r['exc_css']  = re.findall(r"cs=0x([0-9A-Fa-f]+) rflags=", raw)
    r['exc_cr2s'] = re.findall(r"cr2=0x([0-9A-Fa-f]+)", raw)

    # 2) irq0_iret：多次出现取第一个
    m = re.search(r"irq0_iret RIP=0x([0-9A-Fa-f]+) CS=0x([0-9A-Fa-f]+) RFLAGS=0x([0-9A-Fa-f]+) RSP=0x([0-9A-Fa-f]+) SS=0x([0-9A-Fa-f]+)", raw)
    if m:
        r['irq0'] = m.groups()

    # 3) GDT/TSS
    m = re.search(r"GDTR\.base=0x([0-9A-Fa-f]+) GDTR\.lim=0x([0-9A-Fa-f]+) TR=0x([0-9A-Fa-f]+)", raw)
    if m: r['gdtr'] = m.groups()
    m = re.search(r"GDT\[6\]=0x([0-9A-Fa-f]+) GDT\[7\]=0x([0-9A-Fa-f]+) TSS\.base=0x([0-9A-Fa-f]+) TSS\.rsp0=0x([0-9A-Fa-f]+)", raw)
    if m: r['tss'] = m.groups()

    # 4) J-probe ring
    r['ring'] = re.findall(r"t=0x([0-9A-Fa-f]+) cpu=0x([0-9A-Fa-f]+) slot=0x([0-9A-Fa-f]+) st=0x([0-9A-Fa-f]+) ktop=0x([0-9A-Fa-f]+) cs=0x([0-9A-Fa-f]+) rip=0x([0-9A-Fa-f]+) rsp=0x([0-9A-Fa-f]+)", raw)

    return r

def s(h):
    """截短 16 位 hex 到 16"""
    return h.rjust(16, '0')[-16:]

def main():
    rows = []
    for n in GP_RUNS:
        rows.append((n, parse(f"{LOG_DIR}/ser_f1_r{n}.clean.txt")))

    print("="*120)
    print("γ.5-D1 N v2: 10 GP runs — 分字段独立正则")
    print("="*120)

    print(f"\n### 表1: exception frame\n{'run':>4} {'vec':>4} {'err':>6}  rip_candidates                     rsp_candidates                     cs_cands   cr2")
    for n, r in rows:
        vec = r.get('exc_vec','?')
        err = r.get('exc_err','?')
        rips = list(dict.fromkeys(r.get('exc_rips',[])))
        rsps = list(dict.fromkeys(r.get('exc_rsps',[])))
        css  = list(dict.fromkeys(r.get('exc_css',[])))
        cr2s = list(dict.fromkeys(r.get('exc_cr2s',[])))
        print(f"{n:>4} 0x{vec} 0x{err}  rip={rips}  rsp={rsps}  cs={css}  cr2={cr2s}")

    print(f"\n### 表2: irq0_iret target")
    print(f"{'run':>4} {'RIP':>16} {'CS':>4} {'RFLAGS':>8} {'RSP':>16} {'SS':>4}")
    for n, r in rows:
        e = r.get('irq0')
        if e: print(f"{n:>4} {s(e[0]):>16} {e[1][-2:]:>2} {e[2][-8:]:>8} {s(e[3]):>16} {e[4][-2:]:>2}")
        else: print(f"{n:>4}  [no irq0]")

    print(f"\n### 表3: GDT/TSS")
    print(f"{'run':>4} {'GDTR.base':>16} {'lim':>4} {'TR':>4} {'GDT[6]':>16} {'GDT[7]':>10} {'TSS.base':>16} {'TSS.rsp0':>16}")
    for n, r in rows:
        g = r.get('gdtr'); t = r.get('tss')
        if g and t:
            print(f"{n:>4} {s(g[0]):>16} {g[1][-2:]:>2} {g[2][-2:]:>2} {s(t[0]):>16} {t[1][-8:]:>8} {s(t[2]):>16} {s(t[3]):>16}")
        else:
            print(f"{n:>4}  [missing]")

    print(f"\n### 表4: J-probe ring newest 3")
    print(f"{'run':>4} {'t':>3} {'cpu':>3} {'slot':>4} {'st':>3} {'ktop':>16} {'cs':>4} {'rip':>16} {'rsp':>16}")
    for n, r in rows:
        ring = r.get('ring', [])[:3]
        for i, e in enumerate(ring):
            t,cpu,slot,st,ktop,cs,rip,rsp = e
            tag = f"{n:>4}" if i==0 else "    "
            print(f"{tag} {t[-3:]:>3} {cpu[-1:]:>1} {slot[-2:]:>2} {st[-1:]:>1} {s(ktop):>16} {cs[-2:]:>2} {s(rip):>16} {s(rsp):>16}")

    # 汇总
    print("\n"+"="*120)
    print("### 关键 pattern 汇总")
    print("="*120)
    vecs = Counter(r.get('exc_vec','?') for _,r in rows)
    errs = Counter(r.get('exc_err','?') for _,r in rows)
    print(f"vector 分布: {dict(vecs)}")
    print(f"err    分布: {dict(errs)}")

    # exc rip 候选集合（去重后每 run 的候选个数）
    all_rips = Counter()
    for _, r in rows:
        for h in set(r.get('exc_rips', [])):
            all_rips[s(h)] += 1
    print(f"exc.rip 候选总集: {dict(all_rips)}")
    all_rsps = Counter()
    for _, r in rows:
        for h in set(r.get('exc_rsps', [])):
            all_rsps[s(h)] += 1
    print(f"exc.rsp 候选总集: {dict(all_rsps)}")

    irq0_rsps = Counter(s(r['irq0'][3]) for _,r in rows if r.get('irq0'))
    irq0_rips = Counter(s(r['irq0'][0]) for _,r in rows if r.get('irq0'))
    irq0_css  = Counter(r['irq0'][1][-2:] for _,r in rows if r.get('irq0'))
    print(f"irq0.RSP 分布: {dict(irq0_rsps)}")
    print(f"irq0.RIP 分布: {dict(irq0_rips)}")
    print(f"irq0.CS  分布: {dict(irq0_css)}")

    tss_rsp0 = Counter(s(r['tss'][3]) for _,r in rows if r.get('tss'))
    tss_base = Counter(s(r['tss'][2]) for _,r in rows if r.get('tss'))
    print(f"TSS.rsp0 分布: {dict(tss_rsp0)}")
    print(f"TSS.base 分布: {dict(tss_base)}")

    # ring[0].slot 分布
    slots = Counter()
    ktops = Counter()
    for _, r in rows:
        ring = r.get('ring', [])
        if ring:
            e = ring[0]
            slots[e[2][-2:]] += 1
            ktops[s(e[4])] += 1
    print(f"ring[0].slot: {dict(slots)}")
    print(f"ring[0].ktop: {dict(ktops)}")

if __name__ == "__main__":
    os.chdir(os.path.dirname(os.path.abspath(__file__)) + "/..")
    main()
