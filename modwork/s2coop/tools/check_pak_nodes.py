import argparse, os, re, shutil, subprocess, sys, tempfile

GAME = "C:/Program Files (x86)/Steam/steamapps/common/S.T.A.L.K.E.R. 2 Heart of Chornobyl"
PAKSRC = GAME + "/ModWork/s2coop/paksrc"
DATA = "D:/modding/cpp/S2CoopPort/data"
REPAK = os.environ.get("REPAK", "D:/modding/tools/repak/repak.exe")
GD = "stalker2/content/gamelite/gamedata/"
DEV_ONLY_SRC = {"client_nosave"}
TOP = re.compile(r"^(\S+)\s*:\s*struct\.begin")
KV = re.compile(r"^\s+(SID|ConsoleCommand|Repeatable|QuestSID)\s*=\s*(.*?)\s*$")
def verb(n, *vs):
    w = (n["ConsoleCommand"] or "").split()
    return len(w) >= 2 and w[0] == "s2q" and w[1] in vs


FAMILIES = [
    ("quest observers s2q J (host)", lambda s, n: verb(n, "J"), 4000),
    ("global variable observers s2q G (host)", lambda s, n: verb(n, "G"), 500),
    ("world/scene observers s2q W (host)", lambda s, n: verb(n, "W"), 10000),
    ("observers s2q N (host)", lambda s, n: verb(n, "N"), 1),
    ("quest pick s2q L", lambda s, n: verb(n, "L"), 16000),
    ("dialog pick s2q D", lambda s, n: verb(n, "D"), 3000),
    ("client replays _S2C", lambda s, n: s.endswith("_S2C"), 16000),
    ("quest event relay s2q E _S2E (qevent)", lambda s, n: s.endswith("_S2E") and verb(n, "E"), 1600),
    ("quest drop S2QD0_/S2QD1_", lambda s, n: s.startswith("S2QD"), 1),
    ("quest item guard S2QI_", lambda s, n: s.startswith("S2QI_"), 1),
    ("container add S2U_K_", lambda s, n: s.startswith("S2U_K_"), 1000),
    ("take back container S2U_C_", lambda s, n: s.startswith("S2U_C_"), 1000),
    ("take back player S2U_P_", lambda s, n: s.startswith("S2U_P_"), 1000),
    ("stash add S2U_SA_", lambda s, n: s.startswith("S2U_SA_"), 1000),
    ("avatar prototypes Nola_", lambda s, n: s.startswith("Nola"), 5),
]


def read_tsv(path):
    rows = []
    with open(path, encoding="utf-8") as fh:
        for i, ln in enumerate(fh):
            ln = ln.rstrip("\r\n")
            if i == 0 or not ln:
                continue
            rows.append(ln.split("\t"))
    return rows


def cell(c):
    return c[1:] if c[:1] in ("s", "i") else c


def scan(roots):
    nodes = {}
    for r, _, fs in (w for root in roots for w in os.walk(root)):
        for f in fs:
            if not f.lower().endswith(".cfg"):
                continue
            p = os.path.join(r, f)
            cur = None
            with open(p, encoding="utf-8-sig", errors="replace") as fh:
                for ln in fh:
                    m = TOP.match(ln)
                    if m:
                        cur = {"file": p, "SID": None, "ConsoleCommand": None, "Repeatable": None, "QuestSID": None, "links": []}
                        nodes[m.group(1)] = cur
                        continue
                    if cur is None:
                        continue
                    k = KV.match(ln)
                    if k:
                        key, val = k.group(1), k.group(2)
                        if key == "SID" and cur["SID"] is not None:
                            cur["links"].append(val)
                        else:
                            cur[key] = val
    return nodes


def unpack(pak, work):
    out = os.path.join(work, "u")
    r = subprocess.run([REPAK, "unpack", "-q", "-f", "-o", out, pak], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("repak unpack failed: " + (r.stderr or r.stdout)[-300:])
    return out


def check(nodes, data, fails, info):
    def need(ok, what, detail=""):
        (info if ok else fails).append(("PASS " if ok else "FAIL ") + what + ("" if ok or not detail else ": " + detail))

    for name, pred, least in FAMILIES:
        n = sum(1 for s, d in nodes.items() if pred(s, d))
        need(n >= least, "%s: %d nodes (need >= %d)" % (name, n, least))

    ev = {}
    for r in read_tsv(os.path.join(data, "s2u_qevent.tsv")):
        if len(r) >= 4 and cell(r[0]) == "ev":
            w = cell(r[3]).split()
            ev[cell(r[1])] = (w[0], w[2] == "1") if len(w) >= 3 else (None, False)
    have = {s[:-4]: s for s in nodes if s.endswith("_S2E")}
    miss = sorted(set(ev) - set(have))
    extra = sorted(set(have) - set(ev))
    need(not miss, "s2u_qevent.tsv: every one of %d event SIDs has its <sid>_S2E relay node" % len(ev), "%d missing, e.g. %s" % (len(miss), miss[:3]))
    need(not extra, "every _S2E node is listed in s2u_qevent.tsv", "%d not listed, e.g. %s" % (len(extra), extra[:3]))
    bad = []
    for base, sid in have.items():
        n = nodes[sid]
        q, rep = ev.get(base, (None, False))
        if n["SID"] != sid or n["ConsoleCommand"] != "s2q E " + base or base not in n["links"] or (q and n["QuestSID"] != q) or ((n["Repeatable"] == "true") != rep):
            bad.append(sid)
    need(not bad, "every _S2E node: SID, launcher = event node, 'ConsoleCommand = s2q E <sid>', QuestSID and Repeatable match s2u_qevent.tsv", "%d wrong, e.g. %s" % (len(bad), bad[:3]))

    qd = {cell(r[1]) for r in read_tsv(os.path.join(data, "s2u_qdrop.tsv")) if len(r) >= 2 and cell(r[0]) == "D"}
    for pfx in ("S2QD0_", "S2QD1_"):
        m = sorted(s for s in qd if pfx + s not in nodes)
        need(not m, "s2u_qdrop.tsv: every one of %d items has its %s node" % (len(qd), pfx), "%d missing, e.g. %s" % (len(m), m[:3]))

    qi = read_tsv(os.path.join(data, "s2u_qitems.tsv"))
    norm = {cell(r[1]) for r in qi if len(r) >= 2 and cell(r[0]) == "norm"}
    qs = {cell(r[1]) for r in qi if len(r) >= 2 and cell(r[0]) == "S"} - norm
    m = sorted(s for s in qs if "S2QI_" + s not in nodes)
    need(not m, "s2u_qitems.tsv: every one of %d removable quest items has its S2QI_ node" % len(qs), "%d missing, e.g. %s" % (len(m), m[:3]))

    obs = set()
    for sid, n in nodes.items():
        w = (n["ConsoleCommand"] or "").split()
        if len(w) >= 3 and w[0] == "s2q" and w[1] in ("J", "G") and len(w) >= 6:
            obs.add(w[5])
        elif len(w) >= 3 and w[0] == "s2q" and w[1] in ("N", "W"):
            obs.add(w[2])
    m = sorted(s for s in obs if s != "-" and s + "_S2C" not in nodes)
    need(not m, "every SID a host observer reports (s2q J/G/N/W) has its <sid>_S2C client replay", "%d missing, e.g. %s" % (len(m), m[:3]))


def check_tree(roots, data):
    nodes = scan(roots)
    fails, info = [], []
    check(nodes, data, fails, info)
    return nodes, fails, info


def check_pak(pak, data):
    work = tempfile.mkdtemp(prefix="pakcheck_")
    try:
        return check_tree([unpack(pak, work)], data)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def paksrc_roots(src):
    return [os.path.join(src, d) for d in sorted(os.listdir(src)) if os.path.isdir(os.path.join(src, d)) and d not in DEV_ONLY_SRC]


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--pak", help="merged pak to check (zzzzzz_Nola_P.pak)")
    g.add_argument("--paksrc", default=None, help="paksrc folder to check instead of a pak")
    ap.add_argument("--data", default=DATA, help="NolaNative data folder (s2u_qevent.tsv, s2u_qdrop.tsv, s2u_qitems.tsv)")
    a = ap.parse_args()
    if a.pak:
        what = a.pak
        nodes, fails, info = check_pak(a.pak, a.data)
    else:
        what = a.paksrc or PAKSRC
        nodes, fails, info = check_tree(paksrc_roots(what), a.data)
    print("pak node check: %s (%d nodes), data %s" % (what, len(nodes), a.data))
    for ln in info + fails:
        print("  " + ln)
    if fails:
        print("FAIL: %d required node sets missing or wrong" % len(fails))
        sys.exit(1)
    print("PASS: all required node sets present")


if __name__ == "__main__":
    main()
