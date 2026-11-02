import argparse
import csv
import datetime
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_RULES = os.path.join(HERE, "diffrules.tsv")
INV_CANDIDATES = [
    os.environ.get("NOLA_INV", ""),
    r"D:\modding\cpp\S2CoopPort\data\invariants.tsv",
    r"D:\modding\cpp\wt\tc1\data\invariants.tsv",
]
SEV_ORDER = {"error": 0, "warn": 1, "info": 2}
WH_RX = re.compile(r"NOLA WH (\d+) role=(\S+) t=(\S+)((?: \w+=[0-9a-f]{8}/\d+)+)")
INV_RX = re.compile(r"NOLA INV (\S+) first=(\S+) key=(\S+) ?(.*)$")


def to_num(s):
    if s is True:
        return 1.0
    if s is False:
        return 0.0
    if isinstance(s, (int, float)):
        return float(s)
    if s == "true":
        return 1.0
    if s == "false":
        return 0.0
    try:
        return float(s)
    except (TypeError, ValueError):
        return None


def num_text(v):
    if isinstance(v, bool):
        return "1" if v else "0"
    if isinstance(v, (int, float)):
        if math.isfinite(v) and v == math.floor(v) and abs(v) < 1e15:
            return "%.0f" % v
        return "%.3f" % v
    if v is None:
        return None
    return str(v)


def row_text(row, fam, k):
    if k == "key":
        return row.get("key")
    if k == "fam":
        return fam
    if k not in row or row[k] is None:
        return None
    return num_text(row[k])


OPS = ["!=", "<=", ">=", "=", "<", ">"]


def parse_where(text):
    t = (text or "").strip()
    if not t or t == "-":
        return []
    out = []
    for part in t.split(","):
        p = part.strip()
        if not p:
            continue
        best = None
        for pos in range(len(p)):
            for op in OPS:
                if p.startswith(op, pos):
                    best = (pos, op)
                    break
            if best:
                break
        if not best or best[0] == 0:
            raise ValueError("bad condition %r" % p)
        k = p[: best[0]].strip()
        v = p[best[0] + len(best[1]):].strip()
        nv = to_num(v)
        if best[1] in ("<", "<=", ">", ">=") and nv is None:
            raise ValueError("non-numeric compare %r" % p)
        out.append((k, best[1], v, nv))
    return out


def cond_ok(row, fam, c):
    k, op, v, nv = c
    tv = row_text(row, fam, k)
    if tv is None:
        return op == "!="
    rv = to_num(tv)
    if nv is not None and rv is not None:
        return {"=": rv == nv, "!=": rv != nv, "<": rv < nv, "<=": rv <= nv, ">": rv > nv, ">=": rv >= nv}[op]
    if op == "=":
        return tv == v
    if op == "!=":
        return tv != v
    return False


def match(row, fam, conds):
    return all(cond_ok(row, fam, c) for c in conds)


def read_tsv(path):
    with open(path, encoding="utf-8", newline="") as f:
        rows = [r for r in csv.reader(f, delimiter="\t") if r and any(x.strip() for x in r)]
    if not rows:
        return []
    head = [h.strip() for h in rows[0]]
    return [dict(zip(head, [x.strip() for x in r] + [""] * (len(head) - len(r)))) for r in rows[1:]]


def load_rules(path):
    out = []
    for r in read_tsv(path):
        r["conds"] = parse_where(r.get("where", "-"))
        if r["rule"] not in ("client_extra", "host_missing", "field", "pos", "count"):
            raise ValueError("unknown rule %r in %s" % (r["rule"], path))
        if r.get("severity", "error") not in SEV_ORDER:
            raise ValueError("bad severity %r" % r.get("severity"))
        out.append(r)
    return out


def load_inv(path):
    out = []
    for r in read_tsv(path):
        r["conds"] = parse_where(r["where"])
        r["detail_fields"] = [x for x in r.get("detail", "-").split(",") if x and x != "-"]
        out.append(r)
    return out


def find_inv(explicit):
    if explicit:
        return explicit
    for p in INV_CANDIDATES:
        if p and os.path.exists(p):
            return p
    return ""


def load_census(path):
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    if d.get("census") != 1:
        raise ValueError("%s is not a census v1 file" % path)
    d["_path"] = path
    d.setdefault("rows", {})
    return d


def near(row, me, radius):
    if radius is None or me is None:
        return True
    x, y = to_num(row.get("x")), to_num(row.get("y"))
    if x is None or y is None:
        return True
    return math.hypot(x - me[0], y - me[1]) <= radius


def short(row, fields=("cls", "x", "y", "z", "alive", "hidden", "ai", "items", "locked", "n", "kind")):
    o = []
    for k in fields:
        if k in row and row[k] is not None:
            o.append("%s=%s" % (k, num_text(row[k])))
    return " ".join(o)


def diff_pair(host, cli, rules, cname):
    out = []
    hme = host.get("me")
    cme = cli.get("me")
    for r in rules:
        fam = r["family"]
        hrows = {x["key"]: x for x in host["rows"].get(fam, [])}
        crows = {x["key"]: x for x in cli["rows"].get(fam, [])}
        arg = r.get("arg", "-")
        sev = r.get("severity", "error")
        note = r.get("note", "")
        conds = r["conds"]
        if r["rule"] == "client_extra":
            rad = to_num(arg) if arg not in ("", "-") else None
            for k, row in sorted(crows.items()):
                if k in hrows or not match(row, fam, conds) or not near(row, hme, rad):
                    continue
                out.append(dict(client=cname, family=fam, rule="client_extra", severity=sev, key=k, host="-", cli=short(row), note=note, row=row))
        elif r["rule"] == "host_missing":
            rad = to_num(arg) if arg not in ("", "-") else None
            for k, row in sorted(hrows.items()):
                if k in crows or not match(row, fam, conds) or not near(row, cme, rad):
                    continue
                out.append(dict(client=cname, family=fam, rule="host_missing", severity=sev, key=k, host=short(row), cli="-", note=note, row=row))
        elif r["rule"] == "field":
            for k in sorted(set(hrows) & set(crows)):
                h, c = hrows[k], crows[k]
                if not match(h, fam, conds):
                    continue
                hv, cv = row_text(h, fam, arg), row_text(c, fam, arg)
                if hv != cv:
                    out.append(dict(client=cname, family=fam, rule="field " + arg, severity=sev, key=k, host="%s=%s" % (arg, hv), cli="%s=%s" % (arg, cv), note=note, row=h))
        elif r["rule"] == "pos":
            tol = to_num(arg) or 0
            for k in sorted(set(hrows) & set(crows)):
                h, c = hrows[k], crows[k]
                if not match(h, fam, conds):
                    continue
                hx, hy, cx, cy = (to_num(h.get("x")), to_num(h.get("y")), to_num(c.get("x")), to_num(c.get("y")))
                if None in (hx, hy, cx, cy):
                    continue
                d = math.hypot(hx - cx, hy - cy)
                if d > tol:
                    out.append(dict(client=cname, family=fam, rule="pos", severity=sev, key=k, host="%.0f,%.0f" % (hx, hy), cli="%.0f,%.0f d=%.0f" % (cx, cy, d), note=note, row=h))
        elif r["rule"] == "count":
            hn = sum(1 for x in hrows.values() if match(x, fam, conds))
            cn = sum(1 for x in crows.values() if match(x, fam, conds))
            if hn != cn:
                out.append(dict(client=cname, family=fam, rule="count " + (r.get("where") or "-"), severity=sev, key="*", host=str(hn), cli=str(cn), note=note, row={}))
    return out


def local_inv(census, invs):
    role = census.get("role", "")
    views = int(census.get("views", 1) or 0)
    out = []
    for v in invs:
        if v["role"] not in ("all", role):
            continue
        fam = v["fam"]
        mx = views if v["max"] == "views" else int(float(v["max"]))
        groups = {}
        for row in census["rows"].get(fam, []):
            if not match(row, fam, v["conds"]):
                continue
            g = row_text(row, fam, v["per"]) if v["per"] not in ("", "-") else ""
            groups.setdefault(g or "-", []).append(row)
        for g, rs in sorted(groups.items()):
            if len(rs) <= mx:
                continue
            if mx == 0:
                for row in rs:
                    out.append((v["id"], row["key"], " ".join("%s=%s" % (k, row_text(row, fam, k) or "-") for k in v["detail_fields"]), float(v["grace"])))
            else:
                out.append((v["id"], g, "count=%d max=%d" % (len(rs), mx), float(v["grace"])))
    return out


def fmt_wall(ms):
    try:
        return datetime.datetime.fromtimestamp(int(ms) / 1000.0).strftime("%H:%M:%S.%f")[:-3]
    except (OverflowError, OSError, ValueError):
        return str(ms)


def summary_line(divs, invs):
    n = len(divs)
    by = {}
    for d in divs:
        by.setdefault(d["family"], []).append(d)
    errs = sum(1 for d in divs if d["severity"] == "error")
    warns = sum(1 for d in divs if d["severity"] == "warn")
    parts = []
    for fam in sorted(by, key=lambda f: (min(SEV_ORDER[d["severity"]] for d in by[f]), f)):
        ds = by[fam]
        d0 = ds[0]
        where = ""
        x, y = d0["row"].get("x"), d0["row"].get("y")
        if x is not None and y is not None:
            where = " at %s,%s" % (num_text(x), num_text(y))
        parts.append("%s x%d (%s %s %s%s)" % (fam, len(ds), d0["client"], d0["rule"], d0["key"], where))
    s = "%d divergences (errors %d, warn %d)" % (n, errs, warns)
    if parts:
        s += ": " + ", ".join(parts)
    if invs:
        s += "; INV %d (%s)" % (len(invs), ", ".join(sorted(set("%s:%s" % (i[0], i[1]) for i in invs))))
    return s


def cmd_run(a):
    rules = load_rules(a.rules)
    inv_path = find_inv(a.inv)
    invs = load_inv(inv_path) if inv_path else []
    host = load_census(a.host)
    if host.get("role") != "host":
        print("WARNING %s says role=%s, used as the host" % (a.host, host.get("role")))
    clients = [load_census(p) for p in a.clients]
    lines = []
    divs = []
    for i, c in enumerate(clients):
        name = "client%d" % (i + 1) if len(clients) > 1 else "client"
        divs += diff_pair(host, c, rules, name)
    mod_inv = []
    for name, c in [("host", host)] + [("client%d" % (i + 1) if len(clients) > 1 else "client", c) for i, c in enumerate(clients)]:
        for h in c.get("inv", []):
            if h.get("due", True):
                mod_inv.append((name, h["id"], h["key"], h.get("detail", ""), h.get("first")))
    pend = []
    for name, c in [("host", host)] + [("client%d" % (i + 1) if len(clients) > 1 else "client", c) for i, c in enumerate(clients)]:
        for iid, key, det, grace in local_inv(c, invs):
            if not any(m[0] == name and m[1] == iid and m[2] == key for m in mod_inv):
                pend.append((name, iid, key, det, grace))
    divs.sort(key=lambda d: (SEV_ORDER[d["severity"]], d["family"], d["client"], d["key"]))
    lines.append("worlddiff host=%s (t=%s) clients=%s" % (a.host, host.get("t"), " ".join("%s (t=%s)" % (c["_path"], c.get("t")) for c in clients)))
    lines.append("rules=%s (%d)  invariants=%s (%d)" % (a.rules, len(rules), inv_path or "-", len(invs)))
    fam = None
    for d in divs:
        if d["family"] != fam:
            fam = d["family"]
            lines.append("[%s]" % fam)
        lines.append("  %-5s %-7s %-16s %-40s host: %-30s %s: %s%s" % (d["severity"], d["client"], d["rule"], d["key"][:40], d["host"][:30], d["client"], d["cli"], ("  # " + d["note"]) if d["note"] else ""))
    if mod_inv:
        lines.append("[inv] in-mod invariant breaks past their grace")
        for m in mod_inv:
            lines.append("  error %-7s %-22s %s %s" % (m[0], m[1], m[2], m[3]))
    if pend:
        lines.append("[inv-pending] rows that break an invariant in this snapshot but are still inside its grace or were not checked in-mod")
        for p in pend:
            lines.append("  info  %-7s %-22s %s %s (grace %ss)" % p)
    inv_errs = [(m[1], m[0]) for m in mod_inv]
    summ = summary_line(divs, inv_errs)
    lines.append("SUMMARY " + summ)
    text = "\n".join(lines)
    print(text)
    if a.out:
        os.makedirs(a.out, exist_ok=True)
        with open(os.path.join(a.out, "worlddiff.txt"), "w", encoding="utf-8") as f:
            f.write(text + "\n")
        with open(os.path.join(a.out, "worlddiff.tsv"), "w", encoding="utf-8", newline="") as f:
            w = csv.writer(f, delimiter="\t", lineterminator="\n")
            w.writerow(["severity", "client", "family", "rule", "key", "host", "client_value", "note"])
            for d in divs:
                w.writerow([d["severity"], d["client"], d["family"], d["rule"], d["key"], d["host"], d["cli"], d["note"]])
            for m in mod_inv:
                w.writerow(["error", m[0], "inv", m[1], m[2], "-", m[3], "in-mod invariant"])
    bad = sum(1 for d in divs if SEV_ORDER[d["severity"]] <= SEV_ORDER[a.fail_on]) + len(mod_inv)
    return 1 if bad else 0


def parse_wh(path):
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = WH_RX.search(line)
            if not m:
                continue
            fams = {}
            for part in m.group(4).split():
                k, v = part.split("=", 1)
                fams[k] = v
            out.append((int(m.group(1)), m.group(2), m.group(3), fams))
    return out


def parse_inv_lines(path):
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for n, line in enumerate(f, 1):
            m = INV_RX.search(line)
            if m:
                out.append((n, m.group(1), m.group(3), m.group(4).strip()))
    return out


def cmd_logs(a):
    host = parse_wh(a.host)
    lines = ["worlddiff logs host=%s (%d WH lines)" % (a.host, len(host))]
    rc = 0
    if not host:
        lines.append("no NOLA WH lines in the host log")
        print("\n".join(lines))
        return 2
    for i, cp in enumerate(a.clients):
        cli = parse_wh(cp)
        name = "client%d" % (i + 1) if len(a.clients) > 1 else "client"
        lines.append("%s=%s (%d WH lines)" % (name, cp, len(cli)))
        state = {}
        first = {}
        for wall, role, t, fams in cli:
            best = min(host, key=lambda h: abs(h[0] - wall))
            if abs(best[0] - wall) > a.window * 1000:
                continue
            for fam, cv in fams.items():
                if a.families and fam not in a.families:
                    continue
                hv = best[3].get(fam)
                if hv is None:
                    continue
                st = state.setdefault(fam, {"run": 0, "start": None, "open": False, "spans": []})
                if hv != cv:
                    if st["run"] == 0:
                        st["start"] = (wall, hv, cv)
                    st["run"] += 1
                    if st["run"] == a.debounce and not st["open"]:
                        st["open"] = True
                        st["spans"].append([st["start"], None])
                        first.setdefault(fam, st["start"])
                else:
                    if st["open"]:
                        st["spans"][-1][1] = wall
                    st["run"] = 0
                    st["open"] = False
        if not first:
            lines.append("  no divergence (debounce %d samples, window %d s)" % (a.debounce, a.window))
        for fam in sorted(first, key=lambda f: first[f][0]):
            w, hv, cv = first[fam]
            spans = state[fam]["spans"]
            end = spans[-1][1]
            lines.append("  FIRST DIVERGENCE %-6s at %s (wall %d) host %s %s %s; %d span(s), %s" % (fam, fmt_wall(w), w, hv, name, cv, len(spans), "re-converged at " + fmt_wall(end) if end else "still diverged at the end"))
            rc = 1
    for p in [a.host] + a.clients:
        for n, iid, key, det in parse_inv_lines(p):
            lines.append("  INV %s:%d %s key=%s %s" % (os.path.basename(p), n, iid, key, det))
            rc = 1
    print("\n".join(lines))
    return rc


def cmd_check(a):
    ok = True
    try:
        rules = load_rules(a.rules)
        print("ok   %s: %d rules" % (a.rules, len(rules)))
    except Exception as e:
        print("FAIL %s: %s" % (a.rules, e))
        ok = False
    p = find_inv(a.inv)
    if p:
        try:
            print("ok   %s: %d invariants" % (p, len(load_inv(p))))
        except Exception as e:
            print("FAIL %s: %s" % (p, e))
            ok = False
    return 0 if ok else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description="NOLA worlddiff: compare census JSON from host and clients, or find the first WH digest divergence in logs")
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="host.json client.json [client2.json]")
    r.add_argument("host")
    r.add_argument("clients", nargs="+")
    r.add_argument("--rules", default=DEFAULT_RULES)
    r.add_argument("--inv", default="")
    r.add_argument("--out", default="")
    r.add_argument("--fail-on", default="error", choices=["error", "warn", "info"])
    lg = sub.add_parser("logs", help="host UE4SS.log client UE4SS.log [...]")
    lg.add_argument("host")
    lg.add_argument("clients", nargs="+")
    lg.add_argument("--window", type=int, default=6)
    lg.add_argument("--debounce", type=int, default=2)
    lg.add_argument("--families", nargs="*", default=[])
    ck = sub.add_parser("check", help="parse the rule and invariant tables")
    ck.add_argument("--rules", default=DEFAULT_RULES)
    ck.add_argument("--inv", default="")
    a = ap.parse_args(argv)
    if a.cmd == "run":
        return cmd_run(a)
    if a.cmd == "logs":
        return cmd_logs(a)
    return cmd_check(a)


if __name__ == "__main__":
    sys.exit(main())
