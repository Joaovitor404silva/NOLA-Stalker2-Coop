import argparse
import fnmatch
import os
import re
import sys

LIST = "nola_runtime.tsv"
COLS = ("kind", "name", "class", "where", "rows", "marker", "note")
RELEASE_MARKER = "a:NOLA x0 flags: release build"
SHIPPED_BRIDGE = {"flags.txt"}
DOC = "RELEASE.md in the mod repo (D:/modding/cpp/S2CoopPort/RELEASE.md)"
NAME_RE = re.compile(r'L?"([A-Za-z0-9_.\\*-]*\.(?:tsv|txt|json|bin|lua|csv|dat|id|ini|cfg|sav|part))"')
SRC_EXT = (".cpp", ".h", ".inc", ".hpp")


def load_list(data_dir):
    path = os.path.join(data_dir, LIST)
    if not os.path.isfile(path):
        return None, ["%s not found: the list of every file the DLL reads at runtime is missing (%s)" % (path, DOC)]
    lines = open(path, encoding="utf-8").read().split("\n")
    head = lines[0].split("\t")
    if tuple(head) != COLS:
        return None, ["%s columns %s, expected %s" % (path, head, COLS)]
    rows = [dict(zip(head, l.split("\t") + [""] * len(head))) for l in lines[1:] if l.strip()]
    return rows, []


def rc_name(fn):
    return re.sub(r"[^A-Z0-9]", "_", fn.upper())


def src_names(src_dir):
    found = {}
    for root, _, fs in os.walk(src_dir):
        for f in fs:
            if not f.endswith(SRC_EXT):
                continue
            p = os.path.join(root, f)
            for i, ln in enumerate(open(p, encoding="utf-8", errors="replace").read().split("\n"), 1):
                for m in NAME_RE.finditer(ln):
                    n = m.group(1).replace("\\\\", "\\")
                    base = n.rsplit("\\", 1)[-1]
                    if not base or base.startswith(".") or base.startswith("*"):
                        continue
                    found.setdefault(base, "%s:%d" % (os.path.relpath(p, src_dir), i))
    return found


def marker_bytes(m):
    kind, _, text = m.partition(":")
    if kind == "w":
        return text.encode("utf-16-le")
    return text.encode("ascii")


def check(dll, data_dir, src_dir=None, test_off=()):
    rows, fails = load_list(data_dir)
    info = []
    if rows is None:
        return fails, info
    listed = [r for r in rows if r["kind"] == "file"]
    if src_dir and os.path.isdir(src_dir):
        for name, at in sorted(src_names(src_dir).items()):
            if not any(fnmatch.fnmatchcase(name, r["name"]) for r in listed):
                fails.append("src reads %s (%s) but data/%s does not list it: add a row (class a = table embedded in the DLL, b = state the mod writes, c = dev only) and see %s" % (name, at, LIST, DOC))
        rc = os.path.join(src_dir, "s2x_data.rc")
        rct = open(rc, encoding="ascii", errors="replace").read() if os.path.isfile(rc) else ""
    else:
        rct = None
    list_bytes = open(os.path.join(data_dir, LIST), "rb").read()
    if dll and list_bytes not in dll:
        fails.append("%s is not the list embedded in this DLL: --data must be the data folder of the tree that built the DLL (wt.ps1 build <name> -Release -> --data D:/modding/cpp/wt/<name>/data). With another list the embedded list text is not stripped, so its marker column reads as reachable dev markers; table and marker results skipped (%s)" % (os.path.join(data_dir, LIST), DOC))
        return fails, info
    scan = dll.replace(list_bytes, b"")
    emb = 0
    for r in listed:
        if r["class"] != "a":
            continue
        n = r["name"]
        p = os.path.join(data_dir, n)
        if not os.path.isfile(p):
            fails.append("runtime table %s (read by %s) is missing from data/: put it in data/ and run tools/gen_x0_data.py --rc (%s)" % (n, r["where"], DOC))
            continue
        if rct is not None and ('%s RCDATA' % rc_name(n)) not in rct:
            fails.append("runtime table %s is listed in data/%s but not in src/s2x_data.rc: run tools/gen_x0_data.py --rc and rebuild (%s)" % (n, LIST, DOC))
        if open(p, "rb").read() not in dll and n not in SHIPPED_BRIDGE:
            fails.append("runtime table %s (read by %s) is neither embedded in the DLL nor shipped (it carries: %s). Add it to data/%s as class a, run tools/gen_x0_data.py --rc and rebuild the DLL (%s)" % (n, r["where"], r["note"] or "a table the release would read as empty", LIST, DOC))
            continue
        emb += 1
    info.append("%d class a tables embedded byte-identical" % emb)
    rel = marker_bytes(RELEASE_MARKER) in scan
    if not rel:
        fails.append("the DLL is not an S2X_RELEASE build (no '%s' line): build it with wt.ps1 build <name> -Release, which compiles out every class c dev input and locks the class c dev flags (%s)" % (RELEASE_MARKER[2:], DOC))
    dev = 0
    for r in rows:
        if r["class"] != "c" or not r["marker"]:
            continue
        for m in r["marker"].split(","):
            if marker_bytes(m) in scan:
                fails.append("dev-only %s %s is reachable in the release DLL (marker %r found): wrap it in #ifndef S2X_RELEASE (%s)" % (r["kind"], r["name"], m, DOC))
            dev += 1
    locked = {r["name"] for r in rows if r["kind"] == "flag" and r["class"] == "c"}
    miss = sorted(k for k in test_off if k not in locked)
    if miss:
        fails.append("test/diagnostic flags %s are not class c flag rows in data/%s, so a release DLL does not lock them (%s)" % (", ".join(miss), LIST, DOC))
    info.append("%d dev markers checked, %d dev flags locked in release builds, release build marker %s" % (dev, len(locked), "present" if rel else "MISSING"))
    return fails, info


def main():
    ap = argparse.ArgumentParser(description="Fails when a file the NOLA DLL reads at runtime is neither embedded nor shipped, or when dev-only inputs or flags are reachable in a release DLL. See " + DOC + ".")
    ap.add_argument("--dll", required=True)
    ap.add_argument("--repo", default="D:/modding/cpp/S2CoopPort")
    ap.add_argument("--test-off", default="")
    a = ap.parse_args()
    dll = open(a.dll, "rb").read()
    fails, info = check(dll, os.path.join(a.repo, "data"), os.path.join(a.repo, "src"), [k for k in a.test_off.split(",") if k])
    for i in info:
        print("info   " + i)
    for f in fails:
        print("FAIL   " + f)
    print("check_release_tables %s: %s" % (a.dll, "PASS" if not fails else "FAIL (%d)" % len(fails)))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
