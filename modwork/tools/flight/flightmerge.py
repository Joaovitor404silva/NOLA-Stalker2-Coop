import argparse
import json
import os
import re
import shutil
import struct
import sys

MAGIC = b"NOLAFR1\x00"
HDR_SIZE = 4096
REC_SIZE = 128
KINDS = {0: "none", 1: "tx_snap", 2: "rx_snap", 3: "tx", 4: "rx", 5: "handled", 6: "log", 7: "state", 8: "trigger", 9: "meta", 10: "drop"}
KIND_IDS = {v: k for k, v in KINDS.items()}
FAMS = ["net", "session", "load", "travel", "npc", "body", "combat", "container", "lock", "quest", "item", "save", "world", "ui", "inv", "other"]
REC = struct.Struct("<QQqIIIHbBB3s8s76s")
HDR = struct.Struct("<8sIIIIQQQqIIIiqQ16s32s64s32s64s256s")
SNAP_RE = re.compile(r"sid=(\S*) f=(\d+)")
SESSION_RE = re.compile(r"^\d{8}_\d{6}_")


def cstr(b):
    return b.split(b"\x00", 1)[0].decode("latin-1")


class Source:
    def __init__(self):
        self.files = []
        self.hdr = None
        self.recs = {}
        self.torn = 0
        self.label = ""
        self.offset = 0.0
        self.idx = 0

    @property
    def key(self):
        h = self.hdr
        return (h["host"], h["pid"], h["qpc0"])

    def t0(self, r):
        h = self.hdr
        return h["wall0_us"] / 1e6 + (r["qpc"] - h["qpc0"]) / float(h["qpc_freq"])

    def t(self, r):
        return self.t0(r) - self.offset

    def sorted(self):
        return [self.recs[n] for n in sorted(self.recs)]


def read_dump(path):
    data = open(path, "rb").read()
    if len(data) < HDR_SIZE or data[:8] != MAGIC:
        raise ValueError("%s: not a NOLA flight recorder file" % path)
    f = HDR.unpack_from(data, 0)
    hdr = {"version": f[1], "hdr_size": f[2], "rec_size": f[3], "cap": f[4], "next": f[5], "qpc_freq": f[6], "qpc0": f[7], "wall0_us": f[8], "pid": f[9],
           "state": f[10], "dumps": f[11], "net_pid": f[12], "dump_wall_us": f[13], "dump_qpc": f[14], "role": cstr(f[15]), "sid": cstr(f[16]), "host": cstr(f[17]),
           "build": cstr(f[18]), "reason": cstr(f[19]), "tracked": [t for t in cstr(f[20]).split(",") if t], "path": path}
    if hdr["rec_size"] != REC_SIZE or hdr["hdr_size"] != HDR_SIZE or hdr["version"] != 1:
        raise ValueError("%s: unsupported layout v%d hdr %d rec %d" % (path, hdr["version"], hdr["hdr_size"], hdr["rec_size"]))
    cap, nxt = hdr["cap"], hdr["next"]
    if len(data) < HDR_SIZE + cap * REC_SIZE:
        raise ValueError("%s: truncated (%d bytes, need %d)" % (path, len(data), HDR_SIZE + cap * REC_SIZE))
    recs, torn = [], 0
    lo = max(0, nxt - cap)
    for slot in range(min(cap, nxt)):
        v = REC.unpack_from(data, HDR_SIZE + slot * REC_SIZE)
        n = v[0]
        if n == 0 or (n - 1) % cap != slot or n - 1 < lo or n - 1 >= nxt:
            torn += 1
            continue
        recs.append({"n": n, "qpc": v[1], "wall_us": v[2], "seq": v[3], "hash": v[4], "size": v[5], "aux": v[6], "peer": v[7], "kind": KINDS.get(v[8], str(v[8])),
                     "fam": FAMS[v[9]] if v[9] < len(FAMS) else "other", "tag": cstr(v[11]), "text": cstr(v[12])})
    return hdr, recs, torn


def expand_inputs(paths):
    out = []
    for p in paths:
        if os.path.isdir(p):
            for root, _, fs in os.walk(p):
                for f in sorted(fs):
                    if f.endswith(".frd") or f.endswith(".ring"):
                        out.append(os.path.join(root, f))
        elif os.path.exists(p):
            out.append(p)
        else:
            raise SystemExit("no such input: " + p)
    return out


def load(paths):
    by = {}
    for p in expand_inputs(paths):
        try:
            hdr, recs, torn = read_dump(p)
        except ValueError as e:
            print("skip " + str(e), file=sys.stderr)
            continue
        k = (hdr["host"], hdr["pid"], hdr["qpc0"])
        s = by.get(k)
        if not s:
            s = by[k] = Source()
        s.files.append(p)
        if s.hdr is None or hdr["next"] >= s.hdr["next"]:
            keep_tracked = s.hdr["tracked"] if s.hdr else []
            s.hdr = hdr
            s.hdr["tracked"] = sorted(set(hdr["tracked"]) | set(keep_tracked))
        else:
            s.hdr["tracked"] = sorted(set(s.hdr["tracked"]) | set(hdr["tracked"]))
        s.torn += torn
        for r in recs:
            s.recs.setdefault(r["n"], r)
    srcs = sorted(by.values(), key=lambda s: (0 if s.hdr["role"] == "host" else 1, s.hdr["wall0_us"]))
    for i, s in enumerate(srcs):
        s.idx = i
        s.label = "%s@%s:%d" % (s.hdr["role"] or "?", s.hdr["host"] or "pc", s.hdr["pid"])
    return srcs


def snap_pairs(srcs):
    tx = {}
    for s in srcs:
        sid = s.hdr["sid"]
        for r in s.recs.values():
            if r["kind"] == "tx_snap" and sid:
                tx[(sid, r["seq"])] = (s, r)
    pairs = []
    for y in srcs:
        for r in y.recs.values():
            if r["kind"] != "rx_snap":
                continue
            m = SNAP_RE.search(r["text"])
            if not m:
                continue
            k = (m.group(1), int(m.group(2)) & 0xFFFFFFFF)
            if k in tx and tx[k][0] is not y:
                x, rt = tx[k]
                pairs.append((x, rt, y, r))
    return pairs


def tag_pairs_unique(srcs):
    cnt = {}
    for s in srcs:
        for r in s.recs.values():
            if r["kind"] in ("tx", "rx"):
                cnt.setdefault((s.idx, r["kind"], r["tag"], r["hash"]), []).append(r)
    pairs = []
    for x in srcs:
        for y in srcs:
            if x is y:
                continue
            for (i, k, tag, h), rs in cnt.items():
                if i != x.idx or k != "tx" or len(rs) != 1:
                    continue
                ry = cnt.get((y.idx, "rx", tag, h))
                if ry and len(ry) == 1:
                    pairs.append((x, rs[0], y, ry[0]))
    return pairs


def align(srcs):
    for s in srcs:
        s.offset = 0.0
    if len(srcs) < 2:
        return {"method": "single", "pairs": 0, "offsets": {}}
    pairs = snap_pairs(srcs)
    method = "snap pairs"
    if not pairs:
        pairs = tag_pairs_unique(srcs)
        method = "unique tag pairs"
    d = {}
    for x, rt, y, rr in pairs:
        v = y.t0(rr) - x.t0(rt)
        k = (x.idx, y.idx)
        d[k] = min(d.get(k, v), v)
    ref = srcs[0]
    info = {"method": method if pairs else "wall clock only", "pairs": len(pairs), "offsets": {}}
    done = {ref.idx}
    changed = True
    while changed:
        changed = False
        for s in srcs:
            if s.idx in done:
                continue
            for r in srcs:
                if r.idx not in done:
                    continue
                ab, ba = d.get((r.idx, s.idx)), d.get((s.idx, r.idx))
                if ab is None and ba is None:
                    continue
                if ab is not None and ba is not None:
                    e = (ab - ba) / 2.0
                    how = "two-way"
                elif ab is not None:
                    e = ab
                    how = "one-way %s->%s (latency counted as offset)" % (r.label, s.label)
                else:
                    e = -ba
                    how = "one-way %s->%s (latency counted as offset)" % (s.label, r.label)
                s.offset = r.offset + e
                info["offsets"][s.label] = {"vs": r.label, "offset_s": round(s.offset, 6), "how": how,
                                            "min_ab_ms": None if ab is None else round(ab * 1000, 3), "min_ba_ms": None if ba is None else round(ba * 1000, 3)}
                done.add(s.idx)
                changed = True
                break
    for s in srcs:
        if s.idx not in done:
            info["offsets"][s.label] = {"vs": ref.label, "offset_s": 0.0, "how": "no pairs: wall clock only"}
    return info


def receivers(srcs, x):
    sid = x.hdr["sid"]
    out = []
    for y in srcs:
        if y is x:
            continue
        for r in y.recs.values():
            if r["kind"] == "rx_snap" and sid and ("sid=%s " % sid) in r["text"] + " ":
                out.append(y)
                break
    return out or [y for y in srcs if y is not x]


def build_index(srcs):
    idx = {}
    for s in srcs:
        for r in s.sorted():
            r["_t"] = s.t(r)
            r["_src"] = s
            if r["kind"] in ("rx", "handled"):
                idx.setdefault((s.idx, r["kind"], r["tag"], r["hash"]), []).append(r)
            if r["kind"] == "rx_snap":
                idx.setdefault((s.idx, "rx_snap"), []).append(r)
    return idx


def first_in(lst, lo, hi):
    for r in lst or ():
        if lo <= r["_t"] <= hi:
            return r
    return None


def match_flows(srcs, idx, window):
    flows = []
    for x in srcs:
        rec = receivers(srcs, x)
        for r in x.sorted():
            if r["kind"] != "tx":
                continue
            t = r["_t"]
            for y in rec:
                if r["peer"] >= 0 and y.hdr["net_pid"] >= 0 and any(z.hdr["net_pid"] == r["peer"] for z in rec) and y.hdr["net_pid"] != r["peer"]:
                    continue
                rr = first_in(idx.get((y.idx, "rx", r["tag"], r["hash"])), t - 0.5, t + window)
                if rr:
                    flows.append((x, r, y, rr))
    return flows


def never_handled(srcs, idx, window=10.0, persist=1.0):
    out = []
    for x in srcs:
        rec = receivers(srcs, x)
        txs = [r for r in x.sorted() if r["kind"] == "tx"]
        nxt = {}
        for i, r in enumerate(txs):
            for j in range(i + 1, len(txs)):
                if txs[j]["tag"] == r["tag"] and txs[j]["peer"] == r["peer"]:
                    nxt[id(r)] = txs[j]["_t"]
                    break
        for r in txs:
            for y in rec:
                if r["tag"] not in y.hdr["tracked"]:
                    continue
                if r["peer"] >= 0 and y.hdr["net_pid"] >= 0 and any(z.hdr["net_pid"] == r["peer"] for z in rec) and y.hdr["net_pid"] != r["peer"]:
                    continue
                t = r["_t"]
                if nxt.get(id(r), 1e18) - t < persist:
                    continue
                if not first_in(idx.get((y.idx, "rx_snap")), t, t + window):
                    continue
                rr = first_in(idx.get((y.idx, "rx", r["tag"], r["hash"])), t - 0.5, t + window)
                if not rr:
                    out.append({"query": "sent, never received", "tag": r["tag"], "from": x.label, "to": y.label, "t": t, "seq": r["seq"], "hash": "%08x" % r["hash"], "text": r["text"]})
                    continue
                hd = first_in(idx.get((y.idx, "handled", r["tag"], r["hash"])), rr["_t"] - 0.5, rr["_t"] + window)
                if not hd:
                    out.append({"query": "sent, never handled", "tag": r["tag"], "from": x.label, "to": y.label, "t": t, "t_rx": rr["_t"], "seq": r["seq"],
                                "hash": "%08x" % r["hash"], "text": r["text"]})
    out.sort(key=lambda q: q["t"])
    return out


def all_events(srcs):
    ev = []
    for s in srcs:
        for r in s.sorted():
            ev.append(r)
    ev.sort(key=lambda r: (r["_t"], r["_src"].idx, r["n"]))
    return ev


def line_of(r, t0):
    k = r["kind"]
    kk = {"tx": "TX", "rx": "RX", "tx_snap": "TXS", "rx_snap": "RXS", "handled": "HND", "log": "LOG", "state": "ST", "trigger": "TRG", "meta": "META", "drop": "DROP"}.get(k, k)
    extra = ""
    if k in ("tx", "rx", "tx_snap", "rx_snap", "handled"):
        extra = "seq=%d h=%08x %dB " % (r["seq"], r["hash"], r["size"])
        if r["peer"] >= 0:
            extra += "peer=%d " % r["peer"]
        if r["aux"] and k in ("tx", "rx"):
            extra += "coalesced=%d " % r["aux"]
    return "%12.6f  %-28s %-9s %-4s %-8s %s%s" % (r["_t"] - t0, r["_src"].label[:28], r["fam"], kk, r["tag"], extra, json.dumps(r["text"]))


def chrome_trace(srcs, ev, flows, queries, t0):
    out = []
    qtid = 100
    for s in srcs:
        out.append({"ph": "M", "name": "process_name", "pid": s.idx + 1, "tid": 0, "args": {"name": s.label}})
        out.append({"ph": "M", "name": "process_sort_index", "pid": s.idx + 1, "tid": 0, "args": {"sort_index": s.idx}})
        for i, f in enumerate(FAMS):
            out.append({"ph": "M", "name": "thread_name", "pid": s.idx + 1, "tid": i, "args": {"name": f}})
        out.append({"ph": "M", "name": "thread_name", "pid": s.idx + 1, "tid": qtid, "args": {"name": "queries"}})

    def us(t):
        return round((t - t0) * 1e6, 3)

    for r in ev:
        s = r["_src"]
        name = r["kind"] + (" " + r["tag"] if r["tag"] else "")
        out.append({"ph": "X", "name": name, "cat": r["kind"], "ts": us(r["_t"]), "dur": 20, "pid": s.idx + 1, "tid": FAMS.index(r["fam"]),
                    "args": {"seq": r["seq"], "hash": "%08x" % r["hash"], "size": r["size"], "peer": r["peer"], "text": r["text"], "n": r["n"]}})
    for i, (x, r, y, rr) in enumerate(flows):
        out.append({"ph": "s", "id": i + 1, "name": "msg " + r["tag"], "cat": "flow", "ts": us(r["_t"]), "pid": x.idx + 1, "tid": FAMS.index(r["fam"])})
        out.append({"ph": "f", "bp": "e", "id": i + 1, "name": "msg " + r["tag"], "cat": "flow", "ts": us(rr["_t"]), "pid": y.idx + 1, "tid": FAMS.index(rr["fam"])})
    lab = {s.label: s for s in srcs}
    for q in queries:
        s = lab[q["to"]]
        out.append({"ph": "i", "s": "p", "name": "%s %s" % (q["query"], q["tag"]), "cat": "query", "ts": us(q.get("t_rx", q["t"])), "pid": s.idx + 1, "tid": qtid, "args": q})
    return {"traceEvents": out, "displayTimeUnit": "ms"}


def prepare(inputs, window):
    srcs = load(inputs)
    if not srcs:
        raise SystemExit("no flight recorder files in " + " ".join(inputs))
    info = align(srcs)
    idx = build_index(srcs)
    return srcs, info, idx


def cmd_merge(a):
    srcs, info, idx = prepare(a.inputs, a.window)
    ev = all_events(srcs)
    t0 = ev[0]["_t"] if ev else 0.0
    flows = match_flows(srcs, idx, a.window)
    qs = never_handled(srcs, idx, a.window, a.persist)
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "timeline.json"), "w", encoding="utf-8") as fh:
        json.dump(chrome_trace(srcs, ev, flows, qs, t0), fh)
    with open(os.path.join(a.out, "timeline.txt"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("# NOLA merged timeline, t=0 at %.6f (unix s), %d sources, alignment %s (%d pairs)\n" % (t0, len(srcs), info["method"], info["pairs"]))
        for s in srcs:
            o = info["offsets"].get(s.label, {})
            fh.write("# %s files=%d records=%d torn=%d tracked=%s offset=%+.6f s (%s) reason=%s\n" % (s.label, len(s.files), len(s.recs), s.torn, ",".join(s.hdr["tracked"]) or "-",
                                                                                                s.offset, o.get("how", "reference"), s.hdr["reason"] or "-"))
        for r in ev:
            fh.write(line_of(r, t0) + "\n")
    with open(os.path.join(a.out, "queries.txt"), "w", encoding="utf-8", newline="\n") as fh:
        for q in qs:
            fh.write(query_line(q, t0) + "\n")
    summ = {"sources": [{"label": s.label, "files": s.files, "records": len(s.recs), "torn": s.torn, "offset_s": s.offset, "tracked": s.hdr["tracked"], "role": s.hdr["role"],
                         "state": s.hdr["state"], "reason": s.hdr["reason"]} for s in srcs], "align": info, "events": len(ev), "flows": len(flows), "queries": qs, "t0": t0}
    with open(os.path.join(a.out, "summary.json"), "w", encoding="utf-8") as fh:
        json.dump(summ, fh, indent=1)
    print("timeline: %d sources, %d events, %d send->receive flows, alignment %s (%d pairs)" % (len(srcs), len(ev), len(flows), info["method"], info["pairs"]))
    for s in srcs:
        print("  %-34s records=%-7d offset=%+.6f s files=%d" % (s.label, len(s.recs), s.offset, len(s.files)))
    print("queries: %d (%s)" % (len(qs), ", ".join(sorted(set("%s %s" % (q["query"], q["tag"]) for q in qs))) or "none"))
    print("out %s (timeline.json for ui.perfetto.dev, timeline.txt, queries.txt, summary.json)" % a.out)
    return 0


def query_line(q, t0):
    return "%12.6f  %-22s %-6s %s -> %s seq=%d h=%s %s" % (q["t"] - t0, q["query"], q["tag"], q["from"], q["to"], q["seq"], q["hash"], json.dumps(q["text"]))


def cmd_query(a):
    srcs, info, idx = prepare(a.inputs, a.window)
    qs = never_handled(srcs, idx, a.window, a.persist)
    if a.tag:
        qs = [q for q in qs if q["tag"] == a.tag]
    if a.json:
        print(json.dumps({"queries": qs, "align": info}, indent=1))
    else:
        t0 = min((r["_t"] for s in srcs for r in s.recs.values()), default=0.0)
        for q in qs:
            print(query_line(q, t0))
        print("%d finding(s)" % len(qs))
    return 2 if qs else 0


def cmd_events(a):
    srcs, info, idx = prepare(a.inputs, a.window)
    ev = all_events(srcs)
    t0 = ev[0]["_t"] if ev else 0.0
    rx = re.compile(a.grep) if a.grep else None
    sel = []
    for r in ev:
        if a.tag and r["tag"] != a.tag:
            continue
        if a.kind and r["kind"] != a.kind:
            continue
        if a.fam and r["fam"] != a.fam:
            continue
        if a.src and a.src not in r["_src"].label:
            continue
        if rx and not rx.search(r["text"]):
            continue
        if r["_t"] - t0 < a.since:
            continue
        sel.append(r)
    if a.last:
        sel = sel[-a.last:]
    if a.json:
        print(json.dumps([{k: v for k, v in r.items() if not k.startswith("_")} | {"t": r["_t"] - t0, "src": r["_src"].label} for r in sel], indent=1))
    else:
        for r in sel:
            print(line_of(r, t0))
        print("%d event(s)" % len(sel))
    return 0 if sel else 1


def cmd_info(a):
    for p in expand_inputs(a.inputs):
        try:
            h, recs, torn = read_dump(p)
        except ValueError as e:
            print("skip " + str(e))
            continue
        print("%s: role=%s host=%s pid=%d sid=%s state=%s cap=%d next=%d valid=%d torn=%d tracked=%s reason=%s build=%s" % (
            p, h["role"], h["host"], h["pid"], h["sid"] or "-", {1: "open", 2: "closed"}.get(h["state"], h["state"]), h["cap"], h["next"], len(recs), torn,
            ",".join(h["tracked"]) or "-", h["reason"] or "-", h["build"]))
    return 0


def cmd_collect(a):
    os.makedirs(a.out, exist_ok=True)
    got = 0
    for root in a.roots:
        sd = os.path.join(root, "nola_debug", "sessions")
        if not os.path.isdir(sd):
            print("no sessions in %s" % sd)
            continue
        ss = sorted([d for d in os.listdir(sd) if SESSION_RE.match(d) and os.path.isdir(os.path.join(sd, d))], reverse=True)[: a.sessions]
        tag = re.sub(r"[^A-Za-z0-9_-]", "_", os.path.basename(os.path.normpath(root)))
        for d in ss:
            dst = os.path.join(a.out, tag + "_" + d)
            os.makedirs(dst, exist_ok=True)
            for f in os.listdir(os.path.join(sd, d)):
                src = os.path.join(sd, d, f)
                if os.path.isfile(src):
                    with open(src, "rb") as fi, open(os.path.join(dst, f), "wb") as fo:
                        shutil.copyfileobj(fi, fo)
                    got += 1
            print("collected %s -> %s" % (os.path.join(sd, d), dst))
    print("%d file(s) into %s" % (got, a.out))
    return 0 if got else 1


def main(argv=None):
    ap = argparse.ArgumentParser(prog="flightmerge", description="NOLA flight recorder: merge per-PC dumps into one timeline (Chrome/Perfetto JSON + text) and run queries")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def common(p):
        p.add_argument("inputs", nargs="+", help=".frd dumps, .ring files or folders (searched recursively)")
        p.add_argument("--window", type=float, default=10.0, help="seconds a receive/handle may lag the send")
        p.add_argument("--persist", type=float, default=1.0, help="a sent value counts only if it stayed this long")

    p = sub.add_parser("merge")
    common(p)
    p.add_argument("--out", required=True)
    p = sub.add_parser("query")
    common(p)
    p.add_argument("--tag", default="")
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("events")
    common(p)
    p.add_argument("--tag", default="")
    p.add_argument("--kind", default="", choices=[""] + list(KIND_IDS))
    p.add_argument("--fam", default="", choices=[""] + FAMS)
    p.add_argument("--src", default="")
    p.add_argument("--grep", default="")
    p.add_argument("--since", type=float, default=-1e18)
    p.add_argument("--last", type=int, default=0)
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("info")
    p.add_argument("inputs", nargs="+")
    p = sub.add_parser("collect")
    p.add_argument("roots", nargs="+", help="bridge folders, e.g. C:\\nola_inst\\C")
    p.add_argument("--out", required=True)
    p.add_argument("--sessions", type=int, default=1)
    a = ap.parse_args(argv)
    return {"merge": cmd_merge, "query": cmd_query, "events": cmd_events, "info": cmd_info, "collect": cmd_collect}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
