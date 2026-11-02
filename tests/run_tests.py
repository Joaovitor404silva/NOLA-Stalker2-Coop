import argparse
import concurrent.futures
import datetime
import fnmatch
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
RUNDIR = os.path.join(HERE, ".run")
SUITES = os.path.join(HERE, "suites.tsv")
BASELINE = os.path.join(HERE, "baseline.tsv")
UV = ["uv", "run", "--no-project", "--with", "lupa", "python", "-I"]
SKIP_RC = 77
SKIP_RE = re.compile(r"^\s*SKIP\b[:\s]*(.*)$")

FAIL_RE = re.compile(r"^\s*(?:EXPECT )?(?:FAIL|FAILED|not ok)\b[:\s]*(.*)$")
FAIL_TAIL_RE = re.compile(r"^(\S+) (?:idempotence )?FAIL\b")
MISMATCH_RE = re.compile(r"^\s*([A-Za-z0-9_ ]*?MISMATCH\b.*)$")
SUMMARY_FAIL_RE = re.compile(r"^(?:\d+(?:/\d+)?\s*(?:\(|failed|failure|fail|FAIL)|\(\d+ fail|RESULT)")
PASS_RE = re.compile(r"^\s*(?:ok|OK|PASS)\s+(?!\d+(?:/\d+)?\s*(?:$|\())\S")


def read_tsv(path):
    rows = []
    if not os.path.exists(path):
        return rows
    with open(path, encoding="utf-8") as f:
        for i, line in enumerate(f):
            line = line.rstrip("\r\n")
            if i == 0 or not line or line.startswith("#"):
                continue
            rows.append(line.split("\t"))
    return rows


def load_suites():
    out = []
    for r in read_tsv(SUITES):
        r += [""] * (6 - len(r))
        steps = [s.strip() for s in r[4].split(";;") if s.strip()]
        dirs = []
        for st in steps:
            toks = st.split()
            if len(toks) > 1:
                d = toks[1].replace("\\", "/").split("/")[0]
                if d not in dirs:
                    dirs.append(d)
        out.append({"name": r[0], "group": r[1] or "par", "weight": int(r[2] or 0), "needs": [n for n in r[3].split(",") if n], "steps": steps, "globs": [g.strip() for g in r[5].split(";") if g.strip()], "dirs": dirs})
    return out


def git(*a):
    r = subprocess.run(["git", "-C", REPO] + list(a), capture_output=True, text=True, stdin=subprocess.DEVNULL)
    return r.stdout.strip() if r.returncode == 0 else ""


def parent_base():
    br = git("rev-parse", "--abbrev-ref", "HEAD")
    if br and br != "HEAD":
        made = [l for l in git("reflog", "show", "--format=%gs", "refs/heads/" + br).splitlines() if l.startswith("branch: Created from ")]
        if made:
            src = made[-1][len("branch: Created from "):].strip()
            if src != "HEAD" and git("rev-parse", "--verify", "-q", src + "^{commit}"):
                mb = git("merge-base", "HEAD", src)
                if mb:
                    return mb, src
    best = None
    for b in git("for-each-ref", "--format=%(refname:short)", "refs/heads").splitlines():
        if b == br:
            continue
        if subprocess.run(["git", "-C", REPO, "merge-base", "--is-ancestor", "HEAD", b], capture_output=True).returncode == 0:
            continue
        mb = git("merge-base", "HEAD", b)
        if not mb:
            continue
        n = int(git("rev-list", "--count", mb + "..HEAD") or 0)
        if best is None or n < best[0]:
            best = (n, mb, b)
    if best:
        return best[1], best[2]
    return git("rev-parse", "HEAD"), "HEAD"


def changed_files(base):
    names = set(git("diff", "--name-only", base).splitlines())
    names |= set(git("ls-files", "--others", "--exclude-standard").splitlines())
    return sorted(n.replace("\\", "/") for n in names if n and not n.startswith("tests/.run/"))


CODE_EXT = (".cpp", ".h", ".hpp", ".inc", ".rc", ".c")
TEST_EXT = (".cpp", ".h", ".hpp", ".inc", ".rc", ".c", ".bat", ".py", ".ps1", ".tsv", ".txt", ".json")


def read_dir(root, exts):
    out = {}
    for dp, dn, fn in os.walk(root):
        dn[:] = [d for d in dn if d not in ("out", ".run", "__pycache__")]
        for f in fn:
            if f.lower().endswith(exts):
                p = os.path.join(dp, f)
                try:
                    with open(p, encoding="utf-8", errors="replace") as fh:
                        out[p] = fh.read()
                except OSError:
                    pass
    return out


TOKEN_RE = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_.\-]*")


def tokens(text):
    return {t.rstrip(".") for t in TOKEN_RE.findall(text)}


def closure(names, src_toks):
    seen = set(names)
    todo = list(names)
    while todo:
        n = todo.pop()
        for p, t in src_toks.items():
            b = os.path.basename(p)
            if b not in seen and n in t:
                seen.add(b)
                todo.append(b)
    return seen


def select_changed(suites, files):
    pick = {}
    src_texts = None
    test_texts = {}

    def add(name, why):
        pick.setdefault(name, [])
        if why not in pick[name] and len(pick[name]) < 4:
            pick[name].append(why)

    for f in files:
        hit = False
        for s in suites:
            for g in s["globs"] + ["tests/%s/*" % d for d in s["dirs"]]:
                if fnmatch.fnmatchcase(f, g) or (g.endswith("/*") and f.startswith(g[:-1])):
                    add(s["name"], f)
                    hit = True
                    break
        top = f.split("/")[0]
        if top in ("src", "data", "tools", "assets"):
            if src_texts is None:
                src_texts = {p: tokens(t) for p, t in read_dir(os.path.join(REPO, "src"), CODE_EXT).items()}
            names = closure({os.path.basename(f)}, src_texts)
            for s in suites:
                for d in s["dirs"]:
                    if d not in test_texts:
                        acc = set()
                        for t in read_dir(os.path.join(HERE, d), TEST_EXT).values():
                            acc |= tokens(t)
                        test_texts[d] = acc
                if any(n in test_texts[d] for d in s["dirs"] for n in names):
                    add(s["name"], f + " (used by the tests)")
                    hit = True
        if not hit and not f.lower().endswith((".md", ".txt")):
            add("x0", f + " (no mapping)")
    return pick


def load_baseline():
    out = []
    for r in read_tsv(BASELINE):
        r += [""] * (4 - len(r))
        out.append({"suite": r[0], "case": r[1], "reason": r[2], "since": r[3]})
    return out


def expand(p):
    return p.replace("{repo}", REPO).replace("{tests}", HERE)


def step_cmd(step):
    parts = expand(step).split(None, 1)
    kind, rest = parts[0], parts[1] if len(parts) > 1 else ""
    toks = rest.split()
    if kind == "bat":
        return ["cmd.exe", "/c", os.path.join(HERE, toks[0].replace("/", "\\"))] + toks[1:], os.path.dirname(os.path.join(HERE, toks[0]))
    if kind == "exe":
        return [os.path.join(HERE, toks[0].replace("/", "\\"))] + toks[1:], os.path.dirname(os.path.join(HERE, toks[0]))
    if kind == "py":
        return UV + [os.path.join(HERE, toks[0].replace("/", "\\"))] + toks[1:], os.path.dirname(os.path.join(HERE, toks[0]))
    raise SystemExit("bad step " + step)


def parse(text):
    fails, passes = [], 0
    for raw in text.splitlines():
        line = raw.rstrip()
        m = FAIL_RE.match(line)
        if m:
            tail = m.group(1).strip()
            if not tail or SUMMARY_FAIL_RE.match(tail) or re.fullmatch(r"\d+/\d+", tail) or re.fullmatch(r"\(\d+ fail.*", tail):
                continue
            fails.append(re.sub(r"\s+", " ", tail)[:120])
            continue
        m = FAIL_TAIL_RE.match(line)
        if m:
            fails.append(re.sub(r"\s+", " ", line)[:120])
            continue
        m = MISMATCH_RE.match(line)
        if m:
            fails.append(re.sub(r"\s+", " ", m.group(1))[:120])
            continue
        if PASS_RE.match(line):
            passes += 1
    return fails, passes


def run_suite(s, timeout):
    t0 = time.time()
    res = {"name": s["name"], "fails": [], "passes": 0, "skipped": "", "secs": 0.0, "crash": "", "skipsteps": []}
    missing = [expand(n) for n in s["needs"] if not os.path.exists(expand(n))]
    if missing:
        res["skipped"] = "missing " + ", ".join(missing)
        return res
    log = []
    for step in s["steps"]:
        cmd, cwd = step_cmd(step)
        log.append(">>> " + step)
        try:
            r = subprocess.run(cmd, cwd=cwd, capture_output=True, stdin=subprocess.DEVNULL, timeout=timeout)
            text = (r.stdout + b"\n" + r.stderr).decode("utf-8", "replace")
            rc = r.returncode
        except subprocess.TimeoutExpired:
            text, rc = "", -999
            res["crash"] = "timeout %ds in step: %s" % (timeout, step)
        log.append(text)
        if rc == SKIP_RC:
            why = [m.group(1).strip() for m in map(SKIP_RE.match, text.splitlines()) if m]
            res["skipsteps"].append("%s (%s)" % (step, why[-1] if why else "external dependency missing"))
            continue
        f, p = parse(text)
        res["fails"] += f
        res["passes"] += p
        if rc != 0 and not f and not res["crash"]:
            res["crash"] = "exit %d in step: %s" % (rc, step)
        if rc == -999:
            break
    os.makedirs(RUNDIR, exist_ok=True)
    with open(os.path.join(RUNDIR, s["name"] + ".log"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(log))
    res["secs"] = time.time() - t0
    return res


def covered(suite, case, baseline):
    for b in baseline:
        if b["suite"] == suite and b["case"] and case.startswith(b["case"]):
            return b
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--skip", default="")
    ap.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 4))
    ap.add_argument("--timeout", type=int, default=1200)
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--group", default="")
    ap.add_argument("--full", action="store_true")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--changed", nargs="?", const="auto", default=None)
    ap.add_argument("--dry", action="store_true")
    args = ap.parse_args()
    suites = load_suites()
    baseline = load_baseline()
    if args.list:
        for s in suites:
            print("%-12s %-5s w=%-4d steps=%d needs=%s globs=%s" % (s["name"], s["group"], s["weight"], len(s["steps"]), ",".join(s["needs"]) or "-", ";".join(s["globs"]) or "-"))
        return 0
    only = {x for x in args.only.split(",") if x}
    skip = {x for x in args.skip.split(",") if x}
    mode = "changed"
    if args.full:
        mode = "full"
    elif args.all:
        mode = "all"
    elif (only or args.group) and args.changed is None:
        mode = "pick"
    if mode == "changed":
        if args.changed in (None, "auto"):
            base, parent = parent_base()
        else:
            base, parent = git("rev-parse", args.changed), args.changed
            if not base:
                raise SystemExit("unknown base " + args.changed)
        files = changed_files(base)
        pick = select_changed([s for s in suites if s["group"] != "full"], files)
        print("changed mode: base %s (%s), %d changed files, %d suites (full run: --full)" % (base[:10], parent, len(files), len(pick)))
        for n in sorted(pick):
            print("  %-12s <- %s" % (n, "; ".join(pick[n])))
        suites = [s for s in suites if s["name"] in pick]
        if not suites:
            print("no suite maps to these changes")
            return 0
    if only:
        suites = [s for s in suites if s["name"] in only]
    suites = [s for s in suites if s["name"] not in skip]
    if args.group:
        suites = [s for s in suites if s["group"] == args.group]
    elif mode != "full" and not only:
        suites = [s for s in suites if s["group"] != "full"]
    if args.dry:
        print("would run: " + ",".join(s["name"] for s in suites))
        return 0
    par = sorted([s for s in suites if s["group"] not in ("excl",)], key=lambda s: -s["weight"])
    excl = [s for s in suites if s["group"] == "excl"]
    t0 = time.time()
    results = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        futs = [ex.submit(run_suite, s, args.timeout) for s in par]
        for f in futs:
            results.append(f.result())
    for s in excl:
        results.append(run_suite(s, args.timeout))
    wall = time.time() - t0
    order = {s["name"]: i for i, s in enumerate(load_suites())}
    results.sort(key=lambda r: order.get(r["name"], 999))
    new_fail = 0
    known = 0
    seen_base = set()
    lines = []
    for r in results:
        nf = []
        kn = []
        for c in r["fails"]:
            b = covered(r["name"], c, baseline)
            if b:
                kn.append(c)
                seen_base.add((b["suite"], b["case"]))
            else:
                nf.append(c)
        if r["crash"]:
            b = covered(r["name"], "(suite) " + r["crash"], baseline)
            if b:
                kn.append("(suite) " + r["crash"])
                seen_base.add((b["suite"], b["case"]))
            else:
                nf.append("(suite) " + r["crash"])
        if r["skipsteps"] and not nf:
            r["skipped"] = "; ".join(["skipped step " + x for x in r["skipsteps"]] + ([r["skipped"]] if r["skipped"] else []))
        if r["skipped"]:
            status = "SKIP"
        elif nf:
            status = "FAIL"
        elif kn:
            status = "KNOWN"
        else:
            status = "PASS"
        new_fail += len(nf)
        known += len(kn)
        lines.append((r["name"], status, r["passes"], len(nf), len(kn), r["secs"], r["skipped"], nf, kn))
    print("")
    print("%-12s %-6s %6s %6s %6s %8s" % ("suite", "status", "pass", "new", "known", "secs"))
    print("-" * 52)
    for n, st, p, nf, kn, secs, sk, nfl, knl in lines:
        print("%-12s %-6s %6d %6d %6d %8.1f%s" % (n, st, p, nf, kn, secs, ("  " + sk) if sk else ""))
        for c in nfl:
            print("    NEW   " + c)
        for c in knl:
            print("    known " + c)
    fixed = [b for b in baseline if (b["suite"], b["case"]) not in seen_base and any(l[0] == b["suite"] and not l[6] for l in lines)]
    for b in fixed:
        print("FIXED (remove from baseline.tsv): %s | %s" % (b["suite"], b["case"]))
    print("-" * 52)
    print("mode %s, suites %d, new failures %d, known failures %d, stale baseline rows %d, wall %.1f s (%d jobs)" % (mode, len(lines), new_fail, known, len(fixed), wall, args.jobs))
    print(datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
    if new_fail:
        return 1
    if args.strict and fixed:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
