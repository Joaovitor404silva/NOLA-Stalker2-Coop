import argparse
import bisect
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE_DEFAULT = r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\Stalker2\Binaries\Win64\Stalker2-Win64-Shipping.exe"
MANIFEST = os.path.join(ROOT, "data", "hooks.tsv")
SRC = os.path.join(ROOT, "src")
OUT_TSV = os.path.join(ROOT, "data", "buildchk.tsv")
OUT_INC = os.path.join(ROOT, "src", "s2x_buildchk_tab.inc")
SELF_INC = "s2x_buildchk_tab.inc"
WIN = 32
MIN_FIXED = 8
MIN_RVA = 0x100000
MAN_COLS = ["name", "kind", "rva", "at", "aob", "pro", "via", "val", "fn", "use", "build", "feature"]
CONTEXTS = [
    ("plus", re.compile(r"[\w\)\]]\s*\+\s*$")),
    ("call", re.compile(r"\b(eq|eq_seh|fn<\w+>)\(\s*$")),
    ("const", re.compile(r"\bk\w+\s*=\s*$")),
    ("hook", re.compile(r"\bHook\s+g_\w+\{\"[^\"]*\",\s*$")),
    ("aob", re.compile(r"[0-9A-Fa-f?]{2}\",\s*$")),
]
LIT = re.compile(r"\b0x([0-9a-fA-F]+)\b")


class Pe:
    def __init__(self, data):
        self.data = data
        d = data
        e = struct.unpack_from("<I", d, 0x3C)[0]
        if d[e:e + 4] != b"PE\0\0":
            raise ValueError("not a PE file")
        nsec, self.timestamp = struct.unpack_from("<HI", d, e + 6)
        optsize = struct.unpack_from("<H", d, e + 20)[0]
        opt = e + 24
        if struct.unpack_from("<H", d, opt)[0] != 0x20B:
            raise ValueError("not PE32+")
        self.size_of_image = struct.unpack_from("<I", d, opt + 56)[0]
        ndir = struct.unpack_from("<I", d, opt + 108)[0]
        self.dirs = [struct.unpack_from("<II", d, opt + 112 + 8 * i) for i in range(min(ndir, 16))]
        so = opt + optsize
        self.sections = []
        for i in range(nsec):
            s = so + 40 * i
            name = d[s:s + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, va, rsize, raw = struct.unpack_from("<IIII", d, s + 8)
            self.sections.append((name, va, vsize, raw, rsize))
        self._relocs = None

    def section(self, rva):
        for s in self.sections:
            if s[1] <= rva < s[1] + max(s[2], s[4]):
                return s
        return None

    def read(self, rva, n):
        s = self.section(rva)
        if s is None:
            return None
        name, va, vsize, raw, rsize = s
        if rva + n > va + max(vsize, rsize):
            return None
        out = bytearray(n)
        avail = max(0, min(n, rsize - (rva - va)))
        out[:avail] = self.data[raw + rva - va:raw + rva - va + avail]
        return bytes(out)

    def text_vsize(self):
        for s in self.sections:
            if s[0] == ".text":
                return s[2]
        return None

    def relocs(self):
        if self._relocs is not None:
            return self._relocs
        lens = {}
        rva, size = self.dirs[5] if len(self.dirs) > 5 else (0, 0)
        blob = self.read(rva, size) if size else b""
        p = 0
        while p + 8 <= len(blob):
            page, bs = struct.unpack_from("<II", blob, p)
            if bs < 8:
                break
            for k in range(p + 8, p + bs, 2):
                ent = struct.unpack_from("<H", blob, k)[0]
                t, off = ent >> 12, ent & 0xFFF
                if t == 10:
                    lens[page + off] = 8
                elif t == 3:
                    lens[page + off] = 4
            p += bs
        self._reloc_len = lens
        self._relocs = sorted(lens)
        return self._relocs

    def reloc_bytes(self, rva, n):
        rs = self.relocs()
        i = bisect.bisect_left(rs, rva - 8)
        wild = set()
        while i < len(rs) and rs[i] < rva + n:
            r = rs[i]
            for k in range(self._reloc_len[r]):
                if rva <= r + k < rva + n:
                    wild.add(r + k - rva)
            i += 1
        return wild

    def version(self):
        rva, size = self.dirs[2] if len(self.dirs) > 2 else (0, 0)
        if not size:
            return None
        root = rva

        def entries(off):
            h = self.read(root + off, 16)
            nn, ni = struct.unpack_from("<HH", h, 12)
            return [struct.unpack_from("<II", self.read(root + off + 16 + 8 * k, 8)) for k in range(nn + ni)]

        def child(off, want):
            for ident, o in entries(off):
                if want is None or (not ident & 0x80000000 and ident == want):
                    return o
            return None

        o = child(0, 16)
        if o is None or not o & 0x80000000:
            return None
        o = child(o & 0x7FFFFFFF, None)
        if o is None or not o & 0x80000000:
            return None
        o = child(o & 0x7FFFFFFF, None)
        if o is None or o & 0x80000000:
            return None
        drva, dsize = struct.unpack_from("<II", self.read(root + o, 8))
        vi = self.read(drva, dsize)
        if vi is None or len(vi) < 6 + 32 + 52:
            return None
        wlen, vlen, wtype = struct.unpack_from("<HHH", vi, 0)
        key = "VS_VERSION_INFO\0".encode("utf-16-le")
        if vi[6:6 + len(key)] != key or vlen < 52:
            return None
        p = (6 + len(key) + 3) & ~3
        sig, _, ms, ls = struct.unpack_from("<IIII", vi, p)
        if sig != 0xFEEF04BD:
            return None
        return "%d.%d.%d.%d" % (ms >> 16, ms & 0xFFFF, ls >> 16, ls & 0xFFFF)

    def loader_written(self):
        spans = []
        for i in (1, 6, 9, 10, 12, 13):
            if i < len(self.dirs) and self.dirs[i][1]:
                spans.append((self.dirs[i][0], self.dirs[i][0] + self.dirs[i][1]))
        lc_rva, lc_size = self.dirs[10] if len(self.dirs) > 10 else (0, 0)
        lc = self.read(lc_rva, lc_size) if lc_size else None
        if lc:
            base = struct.unpack_from("<Q", self.data, struct.unpack_from("<I", self.data, 0x3C)[0] + 24 + 24)[0]
            for off in (0x58, 0x70, 0x78, 0xD8, 0xE8, 0x118, 0x120, 0x128, 0x130, 0x138):
                if off + 8 <= len(lc):
                    va = struct.unpack_from("<Q", lc, off)[0]
                    if base <= va < base + self.size_of_image:
                        spans.append((va - base, va - base + 8))
        return spans


def read_manifest(path):
    meta = {}
    rows = []
    for ln, line in enumerate(open(path, encoding="utf-8", newline="").read().split("\n"), 1):
        line = line.rstrip("\r")
        if not line:
            continue
        if line.startswith("#"):
            parts = line[1:].split("\t")
            if parts[0] == "exe":
                for kv in parts[1:]:
                    if "=" in kv:
                        k, v = kv.split("=", 1)
                        meta[k] = v
            continue
        cols = line.split("\t")
        if cols[0] == "name":
            continue
        cols += [""] * (len(MAN_COLS) - len(cols))
        r = dict(zip(MAN_COLS, cols))
        r["line"] = ln
        rows.append(r)
    return meta, rows


def tokens_match(tokens, buf):
    if buf is None or len(buf) < len(tokens):
        return False
    return all(t == "??" or int(t, 16) == buf[i] for i, t in enumerate(tokens))


def scan_literals(src_dir):
    out = []
    for fn in sorted(os.listdir(src_dir)):
        if not fn.endswith((".cpp", ".h", ".inc")) or fn == SELF_INC:
            continue
        text = open(os.path.join(src_dir, fn), encoding="utf-8", errors="replace").read()
        for ln, line in enumerate(text.split("\n"), 1):
            for m in LIT.finditer(line):
                v = int(m.group(1), 16)
                if v < MIN_RVA:
                    continue
                before = line[:m.start()]
                ctx = next((n for n, rx in CONTEXTS if rx.search(before)), None)
                out.append({"rva": v, "file": fn, "line": ln, "ctx": ctx, "text": line.strip()[:120]})
    return out


def pat_text(tokens):
    return " ".join(tokens)


def generate(exe_data, manifest_path=MANIFEST, src_dir=SRC, build=None):
    pe = Pe(exe_data)
    meta, mrows = read_manifest(manifest_path)
    build = build or meta.get("build", "")
    ts = pe.timestamp
    if meta.get("ts") and int(meta["ts"], 16) != ts:
        raise SystemExit("manifest ts=%s, exe ts=%08x" % (meta["ts"], ts))
    ver = pe.version()
    tvs = pe.text_vsize()
    if ver is None or tvs is None:
        raise SystemExit("exe has no version resource or no .text section")
    loader = pe.loader_written()
    rows = []
    errors = []
    reloc_wild = 0
    spans = []
    for r in mrows:
        rva = int(r["rva"], 16)
        at = int(r["at"], 16) if r["at"] else rva
        toks = r["aob"].split()
        buf = pe.read(at, len(toks))
        if not tokens_match(toks, buf):
            errors.append("manifest row %s: aob does not match at %x" % (r["name"], at))
            continue
        for k in pe.reloc_bytes(at, len(toks)):
            if toks[k] != "??":
                toks[k] = "??"
                reloc_wild += 1
        rows.append({"name": r["name"], "rva": rva, "at": at, "src": "manifest:" + r["feature"], "pat": toks})
        spans.append((at, at + len(toks)))
        spans.append((rva, rva + 1))
    if errors:
        raise SystemExit("; ".join(errors))
    man_n = len(rows)
    lits = scan_literals(src_dir)
    skipped = []
    src_spans = []
    seen = {}
    for L in lits:
        where = "%s:%d" % (L["file"], L["line"])
        rva = L["rva"]
        if L["ctx"] is None:
            skipped.append((rva, where, "no address context", L["text"]))
            continue
        if rva in seen:
            continue
        seen[rva] = where
        if any(a <= rva < b for a, b in spans):
            skipped.append((rva, where, "covered by the hook manifest", L["text"]))
            continue
        if any(a <= rva < b for a, b in src_spans):
            skipped.append((rva, where, "inside an earlier source row", L["text"]))
            continue
        s = pe.section(rva)
        if s is None or rva >= pe.size_of_image:
            skipped.append((rva, where, "outside the image", L["text"]))
            continue
        sname, va, vsize, raw, rsize = s
        if sname not in (".text", ".rdata", "_RDATA"):
            reason = "in %s (mutable)" % sname if sname in (".data", ".tls", ".msvcjmc") else "in %s (not code or constant data)" % sname
            skipped.append((rva, where, reason, L["text"]))
            continue
        n = min(WIN, va + min(vsize, rsize) - rva)
        if n < MIN_FIXED:
            skipped.append((rva, where, "too close to the end of %s" % sname, L["text"]))
            continue
        if any(a < rva + n and rva < b for a, b in loader):
            skipped.append((rva, where, "overlaps a loader-written table (imports, IAT, load config, TLS)", L["text"]))
            continue
        buf = pe.read(rva, n)
        toks = ["%02X" % b for b in buf]
        for k in pe.reloc_bytes(rva, n):
            toks[k] = "??"
        fixed = sum(1 for t in toks if t != "??")
        if fixed < MIN_FIXED:
            skipped.append((rva, where, "fully relocated (%d fixed bytes) in %s" % (fixed, sname), L["text"]))
            continue
        stem = L["file"].rsplit(".", 1)[0]
        rows.append({"name": stem, "rva": rva, "at": rva, "src": L["file"], "pat": toks})
        src_spans.append((rva, rva + n))
    extra_n = len(rows) - man_n
    badnames = [r["name"] for r in rows if not re.fullmatch(r"[\w.\-]+", r["name"])]
    if badnames:
        raise SystemExit("row names must be [A-Za-z0-9_.-]: " + ", ".join(badnames))
    head = "#exe\tts=%08x\ttextvs=%x\tver=%s\tbuild=%s" % (ts, tvs, ver, build)
    tsv = [head, "name\trva\tat\tsrc\tpattern"]
    for r in rows:
        tsv.append("%s\t%x\t%x\t%s\t%s" % (r["name"], r["rva"], r["at"], r["src"], pat_text(r["pat"])))
    tsv_text = "\n".join(tsv) + "\n"
    inc = ["static const uint32_t kBcTs = 0x%08x;" % ts,
           "static const uint32_t kBcTextVs = 0x%x;" % tvs,
           "static const char kBcVer[] = \"%s\";" % ver,
           "static const char kBcBuild[] = \"%s\";" % build,
           "static const bchk::Sig kBcSigs[] = {"]
    for r in rows:
        inc.append("    {R\"(%s)\", 0x%x, \"%s\", %s}," % (r["name"], r["at"], pat_text(r["pat"]), "true" if r["src"].startswith("manifest:") else "false"))
    inc.append("};")
    inc_text = "\n".join(inc) + "\n"
    return {"tsv": tsv_text, "inc": inc_text, "manifest": man_n, "extra": extra_n, "skipped": skipped, "reloc_wild": reloc_wild,
            "ts": ts, "ver": ver, "textvs": tvs, "build": build, "rows": rows}


def main():
    ap = argparse.ArgumentParser(description="build the startup game-build check table (data/buildchk.tsv + src/s2x_buildchk_tab.inc)")
    ap.add_argument("--exe", default=EXE_DEFAULT)
    ap.add_argument("--manifest", default=MANIFEST)
    ap.add_argument("--src", default=SRC)
    ap.add_argument("--build", default=None)
    ap.add_argument("--out-tsv", default=OUT_TSV)
    ap.add_argument("--out-inc", default=OUT_INC)
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    data = open(a.exe, "rb").read()
    g = generate(data, a.manifest, a.src, a.build)
    with open(a.out_tsv, "w", encoding="utf-8", newline="\n") as f:
        f.write(g["tsv"])
    with open(a.out_inc, "w", encoding="ascii", newline="\n") as f:
        f.write(g["inc"])
    if not a.quiet:
        for rva, where, why, text in g["skipped"]:
            print("skip 0x%x %s: %s | %s" % (rva, where, why, text))
    print("buildchk table: exe ts=%08x .text=%x ver=%s build=%s; %d manifest rows, %d source rows, %d literals skipped, %d reloc bytes wildcarded in manifest patterns" % (
        g["ts"], g["textvs"], g["ver"], g["build"], g["manifest"], g["extra"], len(g["skipped"]), g["reloc_wild"]))


if __name__ == "__main__":
    main()
