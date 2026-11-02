import sys, struct, pefile

def secs(p):
    pe = pefile.PE(p)
    return pe, {s.Name.rstrip(b"\0").decode(): s for s in pe.sections}

pa, sa = secs(sys.argv[1])
pb, sb = secs(sys.argv[2])
ta, tb = sa[".text"].get_data(), sb[".text"].get_data()
tva = sa[".text"].VirtualAddress
d = sa[".data"]
lo, hi = d.VirtualAddress, d.VirtualAddress + d.Misc_VirtualSize
runs, i, n = [], 0, len(ta)
while i < n:
    if ta[i] != tb[i]:
        j = i
        while j < n and ta[j] != tb[j]:
            j += 1
        runs.append((i, j))
        i = j
    else:
        i += 1
mapping, bad, fields = {}, [], 0
for s, e in runs:
    ok = False
    for f in range(e - 4, s + 1):
        if f < 0:
            continue
        for tail in range(0, 5):
            da = struct.unpack_from("<i", ta, f)[0]
            db = struct.unpack_from("<i", tb, f)[0]
            xa = tva + f + 4 + tail + da
            xb = tva + f + 4 + tail + db
            if lo <= xa < hi and lo <= xb < hi and (xa - da) == (xb - db):
                if xa in mapping and mapping[xa] != xb:
                    continue
                mapping[xa] = xb
                ok = True
                fields += 1
                break
        if ok:
            break
    if not ok:
        bad.append((tva + s, tva + e))
inv = {}
for a, b in mapping.items():
    inv.setdefault(b, set()).add(a)
coll = [b for b, a in inv.items() if len(a) > 1]
print(f"text diff runs={len(runs)} explained_as_data_displacements={fields} unexplained={len(bad)} distinct_targets={len(mapping)} many_to_one={len(coll)}")
for r in bad[:20]:
    print("unexplained", hex(r[0]), hex(r[1]))
