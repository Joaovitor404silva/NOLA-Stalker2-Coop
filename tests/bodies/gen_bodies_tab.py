import hashlib
import sys
from pathlib import Path

import lupa.lua54 as lupa

G = Path(r"C:/Program Files (x86)/Steam/steamapps/common/S.T.A.L.K.E.R. 2 Heart of Chornobyl/ModWork")
CORE = G / "s2coop/Mods/S2CoopAlife/Scripts/s2core.lua"
OUT = Path(__file__).resolve().parents[2] / "src" / "s2m_bodies_tab.inc"
WANT_MD5 = "0b56e1b5"


def block(lines, start_txt, end_txt):
    a = next(i for i, l in enumerate(lines) if l.startswith(start_txt))
    b = next(i for i in range(a, len(lines)) if lines[i].startswith(end_txt))
    return "\n".join(lines[a:b + 1])


def main():
    raw = CORE.read_bytes()
    md5 = hashlib.md5(raw).hexdigest()
    if not md5.startswith(WANT_MD5):
        sys.exit("core md5 %s != %s" % (md5, WANT_MD5))
    lines = raw.decode("utf-8").replace("\r\n", "\n").split("\n")
    pup = block(lines, "local PUP_ANIM = {", "local WALK_MAX")
    pa = block(lines, 'S2L.PA_DIR = "', "S2L.PA_SEC = {")
    offt = block(lines, "S2L.OFFT_CV = {", "S2L.OFFT_DEF = {")
    tgt = next(l for l in lines if l.startswith("local TGT_HP = "))
    L = lupa.LuaRuntime(unpack_returned_tuples=True)
    env = "local S2L = {}\n" + pup + "\n" + pa + "\n" + offt + "\n" + tgt + "\nreturn S2L, PUP_ANIM, PUP_NAT, WALK_MAX, TGT_HP"
    S2L, PUP_ANIM, PUP_NAT, WALK_MAX, TGT_HP = L.execute(env)
    out = ["#pragma once"]
    out.append('static const char* const kPaDir = "%s";' % S2L["PA_DIR"])
    out.append("static const int kWalkMax = %d;" % WALK_MAX)
    out.append("static const double kTgtHp = %s;" % repr(float(TGT_HP)))
    rows, fbs = [], []
    for st in sorted(S2L["PA"].keys()):
        row = S2L["PA"][st]
        for fam in ("bh", "ar", "pt", "any", "bhn"):
            e = row[fam]
            if e is None:
                continue
            nat = e[2]
            rows.append('    {"%s", "%s", "%s", %s, %s},' % (st, fam, e[1], repr(float(nat)) if nat is not None else "0.0", "true" if nat is not None else "false"))
        if row["fb"] is not None:
            fbs.append('    {"%s", "%s"},' % (st, row["fb"]))
    out.append("struct PaEnt { const char* st; const char* fam; const char* rel; double nat; bool has_nat; };")
    out.append("static const PaEnt kPa[] = {")
    out += rows
    out.append("};")
    out.append("struct PaFb { const char* st; const char* fb; };")
    out.append("static const PaFb kPaFb[] = {")
    out += fbs
    out.append("};")
    sec = S2L["PA_SEC"]
    out.append("struct PaSec { const char* sec; const char* st; };")
    out.append("static const PaSec kPaSec[] = {")
    for k in sorted(sec.keys()):
        out.append('    {"%s", "%s"},' % (k, sec[k]))
    out.append("};")
    out.append("struct PupAnim { const char* st; const char* path; };")
    out.append("static const PupAnim kPupAnim[] = {")
    for k in sorted(PUP_ANIM.keys()):
        out.append('    {"%s", "%s"},' % (k, PUP_ANIM[k]))
    out.append("};")
    out.append("struct PupNat { const char* st; double nat; };")
    out.append("static const PupNat kPupNat[] = {")
    for k in sorted(PUP_NAT.keys()):
        out.append('    {"%s", %s},' % (k, repr(float(PUP_NAT[k]))))
    out.append("};")
    out.append("struct OfftCv { const char* name; double def; };")
    out.append("static const OfftCv kOfftCv[] = {")
    cv = S2L["OFFT_CV"]
    for i in range(1, len(cv) + 1):
        out.append('    {"%s", %s},' % (cv[i], repr(float(S2L["OFFT_DEF"][cv[i]]))))
    out.append("};")
    OUT.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    print("wrote", OUT, "rows", len(rows), "fb", len(fbs))


if __name__ == "__main__":
    main()
