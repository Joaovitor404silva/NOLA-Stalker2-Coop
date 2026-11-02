import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "data")
SRC = os.path.join(ROOT, "src")
RUNTIME = "nola_runtime.tsv"
RUNTIME_COLS = ("kind", "name", "class", "where", "rows", "marker", "note")

X0_FLAGS = [("cpp_port", "i", "1")] + [("cpp_" + m, "i", "1") for m in
    ["bodies", "damage", "npc", "items", "world", "pop", "quest", "lock", "save", "travel", "session"]] + [
    ("x0ttl", "f", "1.0"), ("x0seh", "i", "0"), ("x0errmax", "i", "5"), ("devcmd", "i", "0"),
    ("cpp_net", "i", "1"), ("netpeer", "s", "127.0.0.1"), ("netlisten", "i", "0"), ("netpeerport", "i", "0"), ("netdx", "f", "0"),
    ("netloss", "f", "0"), ("netrecord", "i", "0"), ("netfiles", "i", "0"), ("netloop", "i", "20"), ("maxclients", "i", "4"), ("netlocal", "i", "0"),
    ("net", "s", "steam"), ("netsteamwait", "f", "60"), ("steaminvite", "i", "1"), ("steamjoin", "i", "1"), ("steamdev", "i", "0"), ("steamtag", "s", "nola-dev"), ("steamexitstop", "i", "1")] + [
    (k, "i", "1") for k in ("sxjoin", "n7", "csave", "qsync", "qrollback", "n7sv")] + [("natdmg", "i", "0"), ("natmc", "i", "1"), ("natgait", "i", "1"), ("natmab", "i", "3"), ("mcworld", "i", "1"), ("uiq", "i", "1"), ("uiqmax", "i", "4"), ("uimaxlife", "f", "120"), ("uitut", "i", "1"), ("uitutwait", "i", "2"), ("n7early", "i", "1"), ("downcam", "i", "1"), ("downhands", "i", "1"), ("downhold", "i", "1"), ("downfx", "i", "1"),
    ("coopsaves", "i", "1"), ("coopsavedir", "s", "SaveGamesNOLA"), ("csexit", "i", "1"), ("csmenu", "i", "1"), ("wpkg", "i", "1"), ("wpkgwait", "i", "180"), ("wsplice", "i", "1"), ("wsplicewait", "i", "120"), ("sxhsave", "i", "1"), ("sxhsavegap", "f", "20"), ("wspair", "i", "1"), ("lshold", "i", "1"), ("lsholdwait", "f", "120"), ("lsgate", "i", "1"), ("lstpcap", "f", "20"), ("lssync", "f", "10"), ("lssilent", "f", "20"), ("n7always", "i", "1"), ("n7samepair", "i", "1"), ("wspos", "i", "1"), ("wstrip", "i", "1"), ("savebe", "i", "1"),
    ("pupyawhold", "i", "1"), ("pupyawmax", "f", "45"), ("pupyawdone", "f", "5"), ("pupyawt", "f", "0.4"), ("paidlee", "i", "0"), ("paaimmv", "i", "1"), ("pacwalk", "i", "0"), ("paladder", "i", "1"), ("paequip", "i", "1"), ("paeqt", "f", "0.8"), ("parl", "i", "1"), ("paml", "i", "1"), ("pahl", "i", "1"), ("paia", "i", "1"), ("pacwin", "f", "2"), ("pacmax", "f", "3.5"), ("paix", "i", "1"), ("pahit", "i", "1"), ("pahitcd", "f", "2"), ("pahitmin", "f", "2"), ("pahitsrc", "i", "1"), ("pahitlt", "i", "1"), ("pahitbig", "f", "30"), ("pahitltcd", "f", "0.4"), ("pahitrate", "f", "1"), ("pahitff", "i", "1"), ("pahitup", "i", "1"), ("pahitslot", "s", "ContextualSlot"), ("pahitbi", "f", "0.1"), ("pahitbo", "f", "0.3"), ("padet", "i", "1"), ("padetsock", "s", "jnt_item"), ("padetx", "f", "0"), ("padety", "f", "0"), ("padetz", "f", "0"), ("padetp", "f", "0"), ("padetw", "f", "0"), ("padetr", "f", "0"), ("padetg", "i", "1"), ("palean", "i", "1"), ("paleandeg", "f", "15"), ("paaimdef", "i", "1"), ("pahrelax", "i", "1"), ("pahrsafe", "i", "1"), ("tgtstill", "i", "1"), ("downwatch", "i", "1"), ("reviveheal", "i", "1"), ("idrop", "i", "1"), ("qshare", "i", "1"), ("meleesync", "i", "1"), ("ldstrip", "i", "1"), ("lockbrk", "i", "1"), ("contall", "i", "1"), ("cbusy", "i", "1"), ("cdeposit", "i", "1"), ("cown", "i", "1"), ("istate", "i", "1"), ("taball", "i", "1"), ("paadd", "i", "1"), ("paaddslot", "s", "ContextualSlot"), ("paaddbi", "f", "0.2"), ("paaddbo", "f", "0.25"), ("pawrl", "i", "1"), ("pawrlchr", "i", "1"), ("pawrlhold", "i", "1"), ("pwskel", "i", "1"),
    ("clkeep", "i", "1"), ("clkeepmax", "i", "12"), ("clnoint", "i", "1"), ("clnat", "i", "1"), ("clhostgc", "i", "1"), ("corpsemc", "i", "1"), ("qdrop", "i", "1"), ("qcatch", "i", "1"), ("qcatchwait", "i", "25"),
    ("qcatchitem", "f", "30"), ("qcatchadd", "i", "1"), ("qbranch", "i", "1"), ("qbgate", "i", "1"), ("qbmirror", "i", "1"), ("npcvis", "i", "1"), ("natqg", "i", "1"), ("natfd", "i", "1"), ("natwide", "i", "1"), ("natrhost", "i", "1"), ("natcpdmg", "i", "2"), ("natcover", "i", "1"), ("natmsec", "i", "1"), ("natmre", "i", "1"), ("natgenmre", "i", "1"), ("natmloop", "i", "1"), ("natmsecq", "i", "1"), ("natmkeep", "i", "1"), ("natmovl", "i", "1"), ("natrmoff", "i", "1"), ("natwpnhid", "i", "1"), ("natnavw", "i", "1"), ("natlooksec", "i", "1"), ("natmutlook", "i", "1"), ("natgenmk", "i", "1"), ("natmidsc", "i", "1"), ("natlookre", "i", "0"), ("natcphp", "i", "1"), ("natcloak", "i", "1"), ("natcopy", "i", "1"), ("natwoundfix", "i", "1"), ("qcatchs2c", "i", "1"), ("qmuteq", "i", "1"), ("qcopyr", "i", "1"), ("qcopyv", "i", "1"), ("qlisten", "i", "1"), ("qmuteact", "i", "1"), ("qhostcopy", "i", "1"), ("qmutespread", "i", "1"), ("qmutebudget", "i", "150"), ("qmutepasses", "i", "3"), ("qmutegap", "i", "2000"), ("qhact", "i", "1"), ("qhstale", "i", "1"), ("qhmax", "i", "100"), ("qhbudget", "i", "20"), ("qhrestart", "i", "1"), ("qcuflags", "i", "1"), ("sxinworld", "i", "1"), ("alifekeepnat", "i", "1"), ("qdmir", "i", "1"), ("qdroll1", "i", "1"), ("trigjoin", "i", "1"), ("palt", "i", "1"), ("palthold", "i", "1"), ("palhgap", "f", "1"), ("palhwait", "f", "4"), ("palhwalk", "f", "150"), ("palhmax", "f", "300"), ("palhstale", "f", "2"), ("pcompass", "i", "1"), ("pcompexpl", "i", "1"), ("pathgap", "f", "2.5"), ("pathold", "i", "1"), ("pathr", "i", "1"), ("thgap", "f", "1"), ("thopen", "f", "8"), ("thr", "i", "1"), ("threset", "f", "3"), ("thrhot", "f", "3"), ("thridle", "f", "0.5"), ("thrkeep", "f", "6"), ("thrlife", "f", "8"), ("thrnear", "f", "400"), ("thrown", "i", "1"), ("thrvel", "i", "1"), ("pabolt", "i", "1"), ("pabarm", "i", "1"), ("pabdet", "i", "1"), ("pabp", "f", "0"), ("pabr", "f", "180"), ("pabs", "f", "1"), ("pabw", "f", "0"), ("pabx", "f", "7"), ("paby", "f", "0"), ("pabz", "f", "-5"), ("pupwfree", "i", "1"), ("padetlslot", "s", "MainActionSlot"), ("padetov", "i", "0"), ("padetovbt", "f", "0.25"), ("padetrslot", "s", "ContextualSlot"), ("paro", "i", "1"), ("paroarm", "i", "0"), ("pwtrig", "i", "1"), ("pwtx", "f", "2.5"), ("pwty", "f", "-1.3"), ("pwtz", "f", "-0.6"), ("parobones", "i", "0"), ("parobt", "f", "0.2"), ("paroclav", "i", "2"), ("parodet", "i", "1"), ("parofist", "i", "1"), ("parosamp", "s", "none"), ("parosampt", "f", "1.5"), ("parosock", "i", "1"), ("pavt", "i", "1"), ("pavtz", "i", "1"), ("pavtzup", "f", "150"), ("pavtzrate", "f", "800"), ("pavtzt", "f", "0.4"), ("pavtzscan", "f", "250"), ("pavtzmin", "f", "25"), ("pavtzr", "f", "40"), ("pavtzramp", "f", "60"), ("pavtzm", "f", "5"), ("paladclip", "i", "1"), ("paladv", "f", "130"), ("paladmin", "f", "15"), ("paladminr", "f", "0.5"), ("paladmaxr", "f", "2"), ("paladlen", "f", "0.5"), ("paladtau", "f", "0.2"), ("paladbi", "f", "0.1"), ("paladhide", "i", "1"), ("paknife", "i", "1"), ("pakx", "f", "8.8"), ("paky", "f", "0.7"), ("pakz", "f", "-5.04"), ("pakp", "f", "10.77"), ("pakw", "f", "23.71"), ("pakr", "f", "-152.91"), ("paknml", "f", "1.5"), ("avuro", "i", "1"), ("pablendearly", "i", "1"), ("pablendin", "f", "0.2"), ("pablendout", "f", "0.25"), ("lshostcap", "f", "90"), ("lshostskip", "i", "1"), ("qdisthold", "i", "1"), ("sxworn", "i", "1"), ("clqsave", "i", "1"), ("clqwait", "f", "12"), ("clqconf", "f", "2"), ("lscrlf", "i", "1"), ("buildchk", "i", "1"), ("natlegs", "i", "1"), ("natmbh", "i", "1"), ("natqgsafe", "i", "1"), ("natlostfix", "i", "1"), ("nateqfix", "i", "1"), ("natfar", "i", "1"), ("natfardead", "f", "4500"), ("natdeadall", "i", "1"), ("nathotkeep", "i", "1"), ("natdeadkeep", "i", "1"), ("natkinmv", "i", "1"), ("nateqsync", "i", "1"), ("natturn", "i", "1"), ("natgaitcp", "i", "1"), ("natwpnlocal", "i", "1"), ("qscene", "i", "1"), ("qscenewait", "f", "8"), ("qscenecine", "i", "1"), ("qscenecap", "f", "600"), ("qscenemax", "f", "180"), ("qscenenpc", "i", "1"), ("qscenevid", "i", "1"), ("qsceneview", "i", "1"), ("qscenefresh", "i", "1"), ("qnrwin", "f", "12"), ("qtrigin", "i", "1"), ("qcondtrig", "i", "1"), ("qcondre", "f", "5"), ("qdist", "i", "1"), ("qtrigx", "i", "0"), ("qdepthguard", "i", "1"), ("qhzrate", "i", "1"), ("qrearm", "i", "1"), ("natloco", "i", "1"), ("avbones", "i", "0"), ("avbonesyaw", "f", "0"), ("avbonesdelay", "f", "0.25"), ("avbonesforce", "i", "1"), ("natdeathfx", "i", "1"), ("natprops", "i", "1"), ("natdupfix", "i", "1"), ("natlocoprm", "i", "1"), ("natlocofl", "i", "1"), ("nattraj", "i", "1"), ("nattrajv", "i", "1"), ("natlocosf", "i", "1"), ("nattrajown", "i", "0"), ("natcpvel", "i", "1"), ("censusobj", "i", "1"), ("natdeadpos", "i", "1"), ("n7reboot", "i", "1"), ("alifemc", "i", "1"), ("qhostonly", "i", "1"), ("qhostonlygap", "f", "3"), ("qobjsync", "i", "1"), ("qdivmute", "i", "1")]

RELEASE_ON = ["natnpc", "csave", "n7", "n7sv", "sxjoin", "cloot", "clmir", "dmgin", "repsync", "lead", "qsync", "vendmir",
              "dobjgen", "ientab", "cjoin", "anomsync", "kzsafe", "tgtnoint", "meshsafe", "lootshare", "nopause", "sleepsync"]
RELEASE_OFF = ["luacmd", "playergod", "godtest", "debugkeys", "autowalk", "autocontinue", "cfollow", "pupmvlog", "cinetest", "cptest",
               "ndtest", "emtest", "iprobe", "cinelog", "autoload", "emreset", "dmgtest", "tgtdiag", "pupmvcr"]
REMOVED = {"fpscap", "fpsenemy", "fpsother", "fpsver", "fpsfe",
           "headfix", "headmax", "headsafe", "hipz", "maxnpcs", "mbhold", "mbhys", "mblook", "mbms", "mbpark", "mbstay", "mirrors", "mirrorspread", "mirrortest", "mshys", "mslot", "mslotsec", "mslr", "msstay", "npcsync", "npsai", "npsbud", "npsd", "npsdlghold", "npsdlgr", "npsidle", "npskey", "npslist", "npslog", "npslost", "npsmatch", "npsmax", "npsmode", "npsmr", "npsmstop", "npsr", "npssnap", "npsstill", "npsv", "nsmax", "pb", "pbeps", "pbms", "pbn", "pbtmax", "pbtmin", "pslot", "pslotms", "pslotn", "scan", "scanage", "scanmove", "scanms", "scanper", "scanr", "scanself", "wpnhand", "wpnmat", "wpnskel", "npsnat"}


def esc(v):
    return v.replace("\\", "\\\\").replace("\t", "\\t").replace("\r", "\\r").replace("\n", "\\n")


def lua_value(tok):
    tok = tok.strip()
    if re.fullmatch(r"-?\d+", tok):
        return "i", str(int(tok))
    if re.fullmatch(r"-?(\d+\.\d*|\.\d+|\d+)([eE][-+]?\d+)?", tok):
        return "f", tok
    m = re.fullmatch(r'"([^"\\]*)"', tok)
    if m:
        return "s", m.group(1)
    if tok in ("true", "false"):
        return "b", tok
    return None


def write_tsv(path, rows):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("k1\tt\tv\n")
        for k, t, v in sorted(rows):
            if k in REMOVED:
                continue
            f.write("s%s\t%s\t%s\n" % (esc(k), t, esc(v)))


def main_extra(main_lua):
    lines = open(main_lua, encoding="utf-8", errors="replace").read().split("\n")
    start = next(i for i, l in enumerate(lines) if l.startswith("FL = {"))
    end = next(i for i, l in enumerate(lines) if i > start and l.startswith("S2_loadFlags()"))
    rows = {}
    for l in lines[start + 1:end]:
        m = re.match(r"^FL\.([A-Za-z0-9_]+)\s*=\s*([^-\s][^\s]*)", l)
        if not m:
            continue
        val = lua_value(m.group(2))
        if val is None:
            raise SystemExit("unparsed main.lua FL line: " + l)
        rows[m.group(1)] = val
    return [(k, t, v) for k, (t, v) in rows.items()]


def strip_lua(text):
    out = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("--[[", i):
            j = text.find("]]", i + 4)
            seg = text[i:j + 2]
            out.append("".join("\n" if c == "\n" else " " for c in seg))
            i = j + 2
            continue
        if text.startswith("--", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
            continue
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def core_dups(core_lua):
    text = open(core_lua, encoding="utf-8", errors="replace").read()
    s = strip_lua(text)
    a = s.find("M.FLAGS = {")
    if a < 0:
        raise SystemExit("M.FLAGS not found")
    i = s.find("{", a)
    depth = 0
    keys = {}
    j = i
    while j < len(s):
        c = s[j]
        if c in "\"'":
            k = j + 1
            while k < len(s) and s[k] != c:
                k += 2 if s[k] == "\\" else 1
            j = k + 1
            continue
        if c in "{[(":
            depth += 1
        elif c in "}])":
            depth -= 1
            if depth == 0:
                break
        elif depth == 1:
            m = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*=(?!=)\s*", s[j:])
            if m and (j == 0 or not (s[j - 1].isalnum() or s[j - 1] == "_")):
                vstart = j + m.end()
                k = vstart
                d2 = 0
                while k < len(s):
                    ch = s[k]
                    if ch in "{[(":
                        d2 += 1
                    elif ch in "}])":
                        if d2 == 0:
                            break
                        d2 -= 1
                    elif ch == "," and d2 == 0:
                        break
                    k += 1
                line = s.count("\n", 0, j) + 1
                keys.setdefault(m.group(1), []).append((line, s[vstart:k].strip()))
                j = k
                continue
        j += 1
    return [(k, "s", ", ".join("line %d = %s" % lv for lv in v)) for k, v in keys.items() if len(v) > 1]


def core_tail(core_lua):
    rows = []
    for l in open(core_lua, encoding="utf-8", errors="replace").read().split("\n"):
        m = re.match(r"^M\.FLAGS\.([A-Za-z0-9_]+)\s*=\s*M\.FLAGS\.\1\s+or\s+([^\s]+)", l)
        if m:
            val = lua_value(m.group(2))
            if val is None:
                raise SystemExit("unparsed s2core M.FLAGS line: " + l)
            rows.append((m.group(1), val[0], val[1]))
    return rows


def read_tsv(path):
    rows = {}
    for l in open(path, encoding="utf-8").read().split("\n")[1:]:
        if l:
            k, t, v = l.split("\t")
            rows[k[1:]] = (t, v)
    return rows


def drop_removed(path):
    lines = open(path, encoding="utf-8", newline="").read().split("\n")
    keep = [l for l in lines if not (l[:1] == "s" and l[1:].split("\t")[0] in REMOVED)]
    if len(keep) != len(lines):
        open(path, "w", encoding="utf-8", newline="").write("\n".join(keep))


def rc_name(fn):
    return re.sub(r"[^A-Z0-9]", "_", fn.upper())


def runtime_rows():
    lines = open(os.path.join(DATA, RUNTIME), encoding="utf-8", newline="").read().split("\n")
    head = lines[0].rstrip("\r").split("\t")
    rows = [dict(zip(head, l.rstrip("\r").split("\t") + [""] * len(head))) for l in lines[1:] if l.strip()]
    return head, rows


def write_rc():
    head, rows = runtime_rows()
    if tuple(head) != RUNTIME_COLS:
        raise SystemExit("%s columns %s, expected %s" % (RUNTIME, head, RUNTIME_COLS))
    seen = set()
    bad = []
    for r in rows:
        r["rows"] = ""
        if r["kind"] not in ("file", "flag") or r["class"] not in ("a", "b", "c"):
            bad.append("%s %s: kind/class" % (r["kind"], r["name"]))
        if (r["kind"], r["name"]) in seen:
            bad.append("duplicate row %s %s" % (r["kind"], r["name"]))
        seen.add((r["kind"], r["name"]))
        if r["kind"] == "file" and r["class"] == "a":
            src = os.path.join(DATA, r["name"])
            if not os.path.isfile(src):
                bad.append("class a table %s is not in data/" % r["name"])
                continue
    dev = {r["name"] for r in rows if r["kind"] == "flag" and r["class"] == "c"}
    bad += ["release-off flag %s is not a class c flag row" % k for k in RELEASE_OFF if k not in dev]
    if bad:
        raise SystemExit("; ".join(bad))
    with open(os.path.join(DATA, RUNTIME), "w", encoding="utf-8", newline="\n") as f:
        f.write("\t".join(head) + "\n")
        for r in rows:
            f.write("\t".join(r[c] for c in head) + "\n")
    emb = sorted(r["name"] for r in rows if r["kind"] == "file" and r["class"] == "a")
    with open(os.path.join(SRC, "s2x_data.rc"), "w", encoding="ascii", newline="\n") as f:
        for t in emb:
            f.write('%s RCDATA "..\\\\data\\\\%s"\n' % (rc_name(t), t))
    for t in emb:
        print(t, rc_name(t))


def main():
    if sys.argv[1:] == ["--rc"]:
        write_rc()
        return
    main_lua, core_lua = sys.argv[1], sys.argv[2]
    write_tsv(os.path.join(DATA, "flags_main_extra.tsv"), main_extra(main_lua))
    write_tsv(os.path.join(DATA, "flags_dups.tsv"), core_dups(core_lua))
    for fn in sorted(os.listdir(DATA)):
        if fn.startswith("flags_") and fn.endswith(".tsv"):
            drop_removed(os.path.join(DATA, fn))
    write_tsv(os.path.join(DATA, "flags_s2core_tail.tsv"), core_tail(core_lua))
    lua = read_tsv(os.path.join(DATA, "flags_s2core.tsv"))
    lua.update({k: (t, v) for k, t, v in core_tail(core_lua)})
    lua.update(read_tsv(os.path.join(DATA, "flags_main.tsv")))
    lua.update(read_tsv(os.path.join(DATA, "flags_main_extra.tsv")))
    rel = [(k, "i", "1") for k in RELEASE_ON] + [(k, "i", "0") for k in RELEASE_OFF]
    for k, t, v in rel:
        if lua.get(k) != (t, v):
            print("release default %s=%s differs from the Lua default %s" % (k, v, "%s %s" % lua[k] if k in lua else "(none)"))
    x0 = {k: (t, v) for k, t, v in X0_FLAGS}
    x0.update({k: (t, v) for k, t, v in rel})
    bad = [k for k in x0 if k in REMOVED]
    if bad:
        raise SystemExit("removed flags in the x0 table: " + ", ".join(bad))
    write_tsv(os.path.join(DATA, "flags_x0.tsv"), [(k, t, v) for k, (t, v) in x0.items()])
    write_rc()


if __name__ == "__main__":
    main()
