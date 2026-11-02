import sys, pefile

def load(p):
    pe = pefile.PE(p)
    return pe, {s.Name.rstrip(b"\0").decode(): s for s in pe.sections}

def runs(a, b):
    out, i, n = [], 0, min(len(a), len(b))
    while i < n:
        if a[i] != b[i]:
            j = i
            while j < n and a[j] != b[j]:
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    return out

pa, sa = load(sys.argv[1])
pb, sb = load(sys.argv[2])
print("timestamp", hex(pa.FILE_HEADER.TimeDateStamp), hex(pb.FILE_HEADER.TimeDateStamp))
print("sections", list(sa), list(sb))
for name in sa:
    a, b = sa[name].get_data(), sb[name].get_data()
    va = sa[name].VirtualAddress
    r = runs(a, b)
    print(f"{name}: size {len(a)} vs {len(b)} vsize {sa[name].Misc_VirtualSize} vs {sb[name].Misc_VirtualSize} diff_runs={len(r)} diff_bytes={sum(e - s for s, e in r)}")
    for s, e in r[:12]:
        ctx_a = a[max(0, s - 24):e + 24]
        ctx_b = b[max(0, s - 24):e + 24]
        print(f"   rva {va + s:#x}..{va + e:#x}  A={ctx_a!r}")
        print(f"   {'':>22} B={ctx_b!r}")
for d in pa.DIRECTORY_ENTRY_DEBUG if hasattr(pa, "DIRECTORY_ENTRY_DEBUG") else []:
    print("debugA", d.struct.Type, getattr(d.entry, "PdbFileName", b""), getattr(d.entry, "Signature_String", ""))
for d in pb.DIRECTORY_ENTRY_DEBUG if hasattr(pb, "DIRECTORY_ENTRY_DEBUG") else []:
    print("debugB", d.struct.Type, getattr(d.entry, "PdbFileName", b""), getattr(d.entry, "Signature_String", ""))
