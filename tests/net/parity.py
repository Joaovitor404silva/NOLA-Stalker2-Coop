import importlib.util
import json
import math
import os
import random
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "out", "net_tool.exe")
RELAY = os.path.join(HERE, "fixtures", "relay_v2.py")
SAMPLES = [os.path.join(HERE, "fixtures", n) for n in ("A_out.json", "B_out.json", "A_in.json", "B_in.json")]
WORK = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "out", "work")
os.makedirs(WORK, exist_ok=True)


def load_relay():
    spec = importlib.util.spec_from_file_location("relay_v2", RELAY)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


R = load_relay()
fails = []
report = []


def tool(*args):
    r = subprocess.run([TOOL] + list(args), capture_output=True, text=True)
    return r.returncode, r.stdout


def check(name, ok, detail=""):
    report.append("%s %s %s" % ("PASS" if ok else "FAIL", name, detail))
    if not ok:
        fails.append(name)


def json_corpus():
    c = [
        '{"a":1.0}', "1e16", "1e15", "1e-5", "0.0001", "0.00001", "123456789012345678901234567890", "-0", "-0.0", "1E400", "-1e400",
        "NaN", "Infinity", "-Infinity", '"\\u00e9\\ud83d\\ude00\\ud800x\\udc00"', '"\x7f"', '"\u00e9\u4e2d\U0001f600"', '{"a":1,"a":2,"b":3}',
        "[1,2,]", "01", '"\t"', ' {"x" : [ true , false , null ] } ', "1.5e+3", "2.5E-3", "1e5", "3.141592653589793", "0.1", "100.0",
        "1e22", "5e-324", "1.7976931348623157e308", '"\\/"', '"\\b\\f\\n\\r\\t\\"\\\\"', "[]", "{}", "[[[[[]]]]]", '{"":""}', "1.", ".5",
        "-", "tru", "nul", '"abc', '{"a" 1}', "1 2", "12345678901234567890", "-9223372036854775809", "1.0e0", "9007199254740993",
        "0.30000000000000004", "1e-7", "123e-2", "1e+2", "-1.5E-10", '"\\u0000\\u001f\\u007f\\u0080\\u00ff"', '"\\uD834\\uDD1E"',
        '"\\ud834"', '"\\uzzzz"', '"\\x"', "\ufeff1", '{"a":[1,{"b":null}],"c":"d"}', "  \n\r\t 5 \n",
    ]
    rnd = random.Random(7)
    for _ in range(3000):
        b = rnd.getrandbits(64)
        x = struct.unpack("<d", struct.pack("<Q", b))[0]
        c.append(json.dumps(x))
    for _ in range(2000):
        c.append(json.dumps(rnd.uniform(-1e6, 1e6)))
        c.append(json.dumps(round(rnd.uniform(-1e6, 1e6), rnd.randint(0, 4))))

    def rv(d):
        k = rnd.randint(0, 7 if d < 4 else 4)
        if k == 0:
            return None
        if k == 1:
            return rnd.random() < 0.5
        if k == 2:
            return rnd.randint(-10**rnd.randint(1, 25), 10**rnd.randint(1, 25))
        if k == 3:
            return rnd.uniform(-1e9, 1e9) * 10 ** rnd.randint(-20, 20)
        if k == 4:
            return "".join(chr(rnd.choice([rnd.randint(0, 0x7f), rnd.randint(0x80, 0xd7ff), rnd.randint(0xe000, 0x10ffff)])) for _ in range(rnd.randint(0, 12)))
        if k == 5:
            return [rv(d + 1) for _ in range(rnd.randint(0, 4))]
        return {rv(9) if False else "k%d" % rnd.randint(0, 5): rv(d + 1) for _ in range(rnd.randint(0, 5))}

    for _ in range(1500):
        v = rv(0)
        c.append(json.dumps(v, ensure_ascii=rnd.random() < 0.5, separators=rnd.choice([(",", ":"), (", ", ": ")])))
    for p in SAMPLES:
        if os.path.exists(p):
            c.append(open(p, encoding="utf-8").read())
    bad = [b"\xff", b'"\xc3"', b'"\xed\xa0\x80"', b'"\xc0\xaf"', b'"\xf4\x90\x80\x80"']
    return [s.encode("utf-8", "surrogatepass") for s in c] + bad


def test_json():
    corpus = json_corpus()
    src = os.path.join(WORK, "json_in.txt")
    dst = os.path.join(WORK, "json_out.txt")
    with open(src, "w") as f:
        for b in corpus:
            f.write(b.hex() + "\n")
    rc, _ = tool("json", src, dst)
    lines = open(dst).read().split("\n")
    bad = rbad = 0
    first = []
    for b, line in zip(corpus, lines):
        try:
            v = json.loads(b.decode("utf-8"))
            want = (json.dumps(v, separators=(",", ":")), json.dumps(v))
        except (ValueError, RecursionError):
            want = None
        if want is None:
            got = None if line == "ERR" else line
        else:
            if line == "ERR":
                got = "ERR"
            else:
                p = line.split("\t")
                got = (bytes.fromhex(p[0]).decode("utf-8", "surrogatepass"), bytes.fromhex(p[1]).decode("utf-8", "surrogatepass"))
                rp = bytes.fromhex(p[2]).decode("utf-8", "surrogatepass")
                if rp != repr(v):
                    rbad += 1
        if got != want:
            bad += 1
            if len(first) < 5:
                first.append((b[:80], want and (want[0][:80],), got if got is None or got == "ERR" else (got[0][:80],)))
    check("json dumps parity", bad == 0, "%d docs, %d mismatches %s" % (len(corpus), bad, first))
    report.append("INFO json repr(v) mismatches (only used in the v= warn text): %d" % rbad)


def test_zlib():
    rnd = random.Random(3)
    blobs = [b"", b"a", b"ab" * 3, bytes(range(256)) * 40, os.urandom(5000), b"x" * 70000, os.urandom(70000)]
    for p in SAMPLES:
        if os.path.exists(p):
            blobs.append(open(p, "rb").read())
    for _ in range(40):
        n = rnd.randint(0, 3000)
        blobs.append(bytes(rnd.choice(b"abcdefgh {}\":,0123456789") for _ in range(n)))
    ok = True
    sizes = []
    for i, b in enumerate(blobs):
        src = os.path.join(WORK, "z_%d.bin" % i)
        open(src, "wb").write(b)
        rc, _ = tool("zc", src, src + ".z")
        c = open(src + ".z", "rb").read()
        try:
            if zlib.decompress(c) != b:
                ok = False
        except zlib.error as e:
            ok = False
            report.append("INFO zlib.decompress failed on blob %d: %s" % (i, e))
        open(src + ".pz", "wb").write(zlib.compress(b, 6))
        rc, out = tool("zd", src + ".pz", src + ".pd")
        if rc != 0 or open(src + ".pd", "rb").read() != b:
            ok = False
        if len(b) >= 500:
            sizes.append((len(b), len(zlib.compress(b, 6)), len(c)))
    check("zlib cross (C++ deflate -> zlib.decompress, zlib.compress -> C++ inflate)", ok, "%d blobs" % len(blobs))
    tz = sum(s[1] for s in sizes)
    tc = sum(s[2] for s in sizes)
    report.append("INFO deflate size vs zlib-6 on %d blobs >=500 B: zlib %d B, C++ %d B (%.2f%%)" % (len(sizes), tz, tc, 100.0 * tc / max(1, tz)))
    real = [s for s in sizes if s[0] > 15000 and s[0] < 30000]
    for s in real:
        report.append("INFO sample %d B: zlib-6 %d B, C++ %d B" % s)
    bad = [b"", b"\x78\x9c", b"\x78\x9c\x03\x00\x00\x00\x00\x02", b"\x78\x9c\x03\x00\x00\x00\x00\x01trailing", b"\x78\x9d\x03\x00", zlib.compress(b"hello")[:-1]]
    agree = True
    for i, b in enumerate(bad):
        src = os.path.join(WORK, "zb_%d.bin" % i)
        open(src, "wb").write(b)
        rc, _ = tool("zd", src, src + ".d")
        try:
            zlib.decompress(b)
            pyok = True
        except zlib.error:
            pyok = False
        if pyok != (rc == 0):
            agree = False
            report.append("INFO zd disagrees on bad blob %d" % i)
    check("zlib error cases agree (ok/fail, trailing data ignored)", agree)


def frames(seed, base_path, n=300, apps_on=True, need_on=True):
    rnd = random.Random(seed)
    base = json.load(open(base_path, encoding="utf-8"))
    npcs = base.get("npcs", [])
    if not npcs:
        npcs = [{"id": "BP_AI_Human_C_%d" % (100 + i), "x": 1000.0 + i, "y": 2000.0, "z": 50.0, "yaw": 10.0, "a": "%08x" % (i * 7919), "s": i, "pose": "0,1,2"} for i in range(6)]
    pool = [dict(n) for n in npcs] + [{"id": "extra_%d" % i, "x": 5.0 * i, "y": 1.5, "z": -3.0, "yaw": 0.0, "a": "%08x" % (i + 99), "s": i, "wp": "w%d" % i} for i in range(5)]
    apps = [{"a": "%08x" % (k * 7919), "mesh": "/Game/Mesh_%d.Mesh_%d" % (k, k), "parts": "p%d;q%d" % (k, k)} for k in range(10)]
    out = []
    now = 1000.0
    f = base.get("f", 0)
    for i in range(n):
        now += rnd.choice([0.1, 0.1, 0.1, 0.05, 0.3, 1.2])
        f += 1
        m = dict(base)
        m["f"] = f
        m["t"] = round(now - 900, 3)
        m["x"] = round(m["x"] + i * 3.3, 1)
        if i % 97 == 50:
            m["sid"] = "restart-%d" % i
        live = [dict(p) for p in pool if rnd.random() < 0.85]
        for p in live:
            if rnd.random() < 0.4:
                p["x"] = round(p["x"] + rnd.uniform(-50, 50), 1)
                p["yaw"] = round(rnd.uniform(-180, 180), 1)
        m["npcs"] = live
        m["apps"] = [a for a in apps if rnd.random() < 0.2] if apps_on else []
        m["need"] = ",".join("%08x" % (k * 7919) for k in range(10) if rnd.random() < 0.3) if need_on else ""
        sep = (",", ":") if rnd.random() < 0.8 else (", ", ": ")
        txt = json.dumps(m, separators=sep)
        out.append((now, txt))
    out.append((now + 0.1, '{"v":1,"f":3}'))
    out.append((now + 0.2, '{"v":2,"f":'))
    out.append((now + 0.3, '{"v":2.0,"sid":"s","f":1,"x":1,"npcs":[],"apps":[],"need":""}'))
    out.append((now + 0.4, '{"v":true,"f":1}'))
    return out


def py_split(snd, asm, txt, now):
    try:
        return 0, snd.split(txt, now, asm.apps)
    except R.ProtoError:
        return 2, []
    except ValueError:
        return 1, []
    except Exception:
        return 3, []


def py_feed(asm, data, now):
    try:
        asm.feed(data, now)
        return 0
    except R.ProtoError:
        return 1
    except (ValueError, KeyError, zlib.error):
        return 2


def view(dg):
    if len(dg) >= 4 and dg[:2] == b"S2" and dg[3] != 6:
        try:
            return dg[:4].hex() + ":" + zlib.decompress(dg[4:]).hex()
        except zlib.error:
            pass
    return dg.hex()


def run_script(lines):
    src = os.path.join(WORK, "script_in.txt")
    dst = os.path.join(WORK, "script_out.txt")
    open(src, "w", newline="\n").write("\n".join(lines) + "\n")
    tool("script", src, dst)
    return open(dst).read().split("\n")


def test_sender_assembler(seed, loss, reorder, dx):
    rnd = random.Random(seed + 1000)
    fa = frames(seed, SAMPLES[0])
    fb = frames(seed + 1, SAMPLES[1], apps_on=False)
    sa, aa = R.Sender(), R.Assembler()
    sb, ab = R.Sender(), R.Assembler()
    scriptA, pyA = [], []
    py_dgs = []
    for now, txt in fa:
        e, dgs = py_split(sa, aa, txt, now)
        scriptA.append("S\t%r\t%s" % (now, txt.encode().hex()))
        pyA.append(("S", e, len(dgs)))
        for d in dgs:
            pyA.append(("D", view(d)))
            py_dgs.append((now, d))
    outA = run_script(scriptA)
    cpp_dgs = []
    mism = 0
    k = 0
    first = None
    for ln in outA:
        if not ln:
            continue
        p = ln.split("\t")
        if p[0] == "S":
            got = ("S", int(p[1]), int(p[2]))
        else:
            got = ("D", p[1])
            cpp_dgs.append(bytes.fromhex(p[2]))
        want = pyA[k] if k < len(pyA) else None
        if got != want:
            mism += 1
            if first is None:
                first = (k, str(want)[:200], str(got)[:200])
        k += 1
    check("sender parity seed=%d" % seed, mism == 0 and k == len(pyA), "%d records, %d mismatches %s" % (len(pyA), mism, first))
    times = [t for t, _ in py_dgs]
    pairs = list(zip(times, [d for _, d in py_dgs], cpp_dgs))
    deliver = []
    for t, pd, cd in pairs:
        if rnd.random() < loss:
            continue
        deliver.append([t + (rnd.uniform(0, 0.3) if reorder else 0.0), pd, cd])
    deliver.sort(key=lambda x: x[0])
    events = []
    bi = 0
    for d in deliver:
        while bi < len(fb) and fb[bi][0] <= d[0]:
            events.append(("S", fb[bi][0], fb[bi][1]))
            bi += 1
        events.append(("F", d[0], d[1], d[2]))
        if rnd.random() < 0.5:
            events.append(("R", d[0]))
    for x in fb[bi:]:
        events.append(("S", x[0], x[1]))
    events.append(("F", events[-1][1] + 0.01, b"{\"v\":1}", b"{\"v\":1}"))
    events.append(("F", events[-1][1] + 0.01, b"S2\x03\x01xx", b"S2\x03\x01xx"))
    events.append(("F", events[-1][1] + 0.01, b"S2\x02\x01" + b"garbage", b"S2\x02\x01" + b"garbage"))
    events.append(("F", events[-1][1] + 0.01, b"S2\x02\x09" + zlib.compress(b"{}"), b"S2\x02\x09" + zlib.compress(b"{}")))
    events.append(("F", events[-1][1] + 0.01, b"S2\x02\x03" + zlib.compress(b'{"mesh":1}'), b"S2\x02\x03" + zlib.compress(b'{"mesh":1}')))
    events.append(("R", events[-1][1] + 3.0))
    events.append(("R", events[-1][1] + 30.0))

    def py_side(which):
        sb2, ab2 = R.Sender(), R.Assembler()
        res = []
        for ev in events:
            if ev[0] == "S":
                e, dgs = py_split(sb2, ab2, ev[2], ev[1])
                res.append(("S", e, len(dgs)))
                for d in dgs:
                    res.append(("D", view(d)))
            elif ev[0] == "F":
                e = py_feed(ab2, ev[2] if which == "py" else ev[3], ev[1])
                res.append(("F", e, ab2.stale, len(ab2.apps), len(ab2.npcs)))
            else:
                m = ab2.render(sb2.local_need, ev[1], dx)
                res.append(("R", "None" if m is None else json.dumps(m)))
        return res

    want = py_side("py")
    want_cpp_dgs = py_side("cpp")
    script = []
    for ev in events:
        if ev[0] == "S":
            script.append("S\t%r\t%s" % (ev[1], ev[2].encode().hex()))
        elif ev[0] == "F":
            script.append("F\t%r\t%s" % (ev[1], ev[2].hex()))
        else:
            script.append("R\t%r\t%r" % (ev[1], dx))
    outB = run_script(script)
    got = []
    for ln in outB:
        if not ln:
            continue
        p = ln.split("\t")
        if p[0] == "S":
            got.append(("S", int(p[1]), int(p[2])))
        elif p[0] == "D":
            got.append(("D", p[1]))
        elif p[0] == "F":
            got.append(("F", int(p[1]), int(p[2]), int(p[3]), int(p[4])))
        else:
            got.append(("R", "None" if p[1] == "None" else bytes.fromhex(p[1]).decode("utf-8")))
    mism = [i for i in range(max(len(want), len(got))) if i >= len(want) or i >= len(got) or want[i] != got[i]]
    nR = sum(1 for w in want if w[0] == "R")
    first = None
    if mism:
        i = mism[0]
        first = (i, str(want[i] if i < len(want) else None)[:300], str(got[i] if i < len(got) else None)[:300])
    check("assembler+render parity seed=%d loss=%.2f reorder=%s dx=%r (py datagrams into C++)" % (seed, loss, reorder, dx), not mism,
          "%d records (%d renders, final stale=%s), %d mismatches %s" % (len(want), nR, [w for w in want if w[0] == "F"][-1][2], len(mism), first))
    m2 = [i for i in range(len(want)) if want[i] != want_cpp_dgs[i]]
    check("C++ datagrams into the Python assembler give the same in.json seed=%d" % seed, not m2, "%d mismatches" % len(m2))


def main():
    if not os.path.exists(TOOL):
        print("net_tool.exe missing: run build_test.bat")
        return 2
    test_json()
    test_zlib()
    test_sender_assembler(1, 0.0, False, 0.0)
    test_sender_assembler(2, 0.2, True, 0.0)
    test_sender_assembler(3, 0.5, True, 250.0)
    test_sender_assembler(4, 0.05, True, 0.0)
    print("\n".join(report))
    print("RESULT %s (%d fail)" % ("PASS" if not fails else "FAIL", len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
