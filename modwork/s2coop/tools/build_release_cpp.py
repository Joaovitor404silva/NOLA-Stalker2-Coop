import argparse, getpass, hashlib, os, re, shutil, struct, subprocess, sys, zipfile

GAME = "C:/Program Files (x86)/Steam/steamapps/common/S.T.A.L.K.E.R. 2 Heart of Chornobyl"
TOOLS = os.path.dirname(os.path.abspath(__file__)).replace("\\", "/")
TREE = os.path.normpath(TOOLS + "/../../..").replace("\\", "/")
REPO = os.environ.get("S2_REPO", TREE if os.path.isdir(TREE + "/src") else "D:/modding/cpp/S2CoopPort").replace("\\", "/")
DLL_SRC = REPO + "/cppbuild/Release/NolaNative.dll"
RELEASE_DOC = REPO + "/RELEASE.md"
DATA_DIR = REPO + "/data"
TITLE_SRC = REPO + "/assets/nola_title.png"
TITLE_REL = "ue4ss/Mods/NolaNative/assets/nola_title.png"
TITLE_INFO = {}
UE4SS_ZIP = "D:/modding/tools/ue4ss/UE4SS_v3.0.1-1164-g5e627997.zip"
GAMECFG = "D:/modding/tools/ue4ss/cfg/x/Stalker2/steam"
REPAK = "D:/modding/tools/repak/repak.exe"
INSTALLER_SRC = TOOLS + "/installer/nola_install.ps1"
INSTALLER_BATS = ("install.bat", "uninstall.bat")
AUTO_ROOT = "NOLA-Auto"
SHIPPED_SCRIPT_NAME = "nola_install.ps1"
EXE_EXT = (".exe", ".com", ".scr", ".msi")
SCRIPT_FORBIDDEN_RE = re.compile(r"-enc(odedcommand)?\b|frombase64|invoke-webrequest|invoke-restmethod|downloadstring|downloadfile|net\.webclient|bitstransfer|\bcurl\b|\bwget\b|\biex\b|invoke-expression|windowstyle\s+hidden|-w\s+hidden", re.I)
MANUAL_TEMPLATE = TOOLS + "/installer/MANUAL_INSTALL.md.in"
DOC_TEMPLATES = ("README.md", "INSTALL.md")
DOC_FORBIDDEN_RE = re.compile(r"discord|fps|relay|python|lua", re.I)
ZIP_ROOT = "NOLA-Manual"
WIN64_REL = "Stalker2/Binaries/Win64"
MODS_REL = "Stalker2/Content/Paks/~mods"
KEEP_IF_PRESENT = ("ue4ss/UE4SS-settings.ini", "ue4ss/Mods/mods.txt")
LIVE_UE4SS = GAME + "/Stalker2/Binaries/Win64/ue4ss"
LIVE_DWMAPI = GAME + "/Stalker2/Binaries/Win64/dwmapi.dll"
LIVE_FLAGS = {"host": "C:/s2coop_inst/A/flags.txt", "client": "C:/s2coop_inst/B/flags.txt"}
DEFAULT_TABLES = ["flags_s2core.tsv", "flags_s2core_tail.tsv", "flags_main.tsv", "flags_main_extra.tsv", "flags_x0.tsv"]
ROLE_TABLE = "flags_role_%s.tsv"
PAK_NAME = "zzzzzz_Nola_P.pak"
PAK_MOUNT = "../../../"
PAKSRC = os.path.normpath(TOOLS + "/../paksrc").replace("\\", "/")
GAMEDATA_REL = "Stalker2/Content/GameLite/GameData/"
DEV_ONLY_SRC = {"client_nosave": "old client no-save difficulty patch, superseded by the s2m_solo save rule"}
PAKSRC_INFO = {}
NOT_SHIPPED = "God_KeepFall test rig; QuestMute*, ClientAutoSaveOnly, HostNoAutoTypeSave (replaced by the s2m_solo hooks in the DLL)"
MOD_NODE_RE = re.compile(r"^S2[A-Z0-9]*_|_S2[A-Z0-9]*$|^(S2Coop|Nola)")
TOP_NODE_RE = re.compile(r"^(\S+)\s*:\s*struct\.begin")
STOCK_FILE_RE = re.compile(r"(^|/)(DifficultyPrototypes/|AutoSaveVariables\.cfg)", re.I)
SIGNATURES = ["FName_ToString", "GMalloc", "GNatives", "GUObjectArray", "GUObjectHashTables", "ProcessLocalScriptFunction"]

TEST_OFF = {"luacmd", "playergod", "godtest", "debugkeys", "autowalk", "autocontinue", "cfollow", "pupmvlog", "cinetest", "cptest",
            "ndtest", "emtest", "iprobe", "cinelog", "autoload", "emreset", "dmgtest", "tgtdiag", "pupmvcr", "devcmd"}
REQUIRE_ALL = {"luacmd": "0", "natnpc": "1", "csave": "1", "n7": "1", "n7sv": "1", "sxjoin": "1", "cloot": "1", "clmir": "1",
               "dmgin": "1", "qrollback": "1", "cpp_port": "1", "cpp_net": "1", "lead": "1", "qsync": "1", "vendmir": "1", "dobjgen": "1", "ientab": "1",
               "cjoin": "1", "anomsync": "1", "kzsafe": "1", "tgtnoint": "1", "meshsafe": "1", "lootshare": "1", "nopause": "1", "sleepsync": "1",
               "b164off": "0", "godown": "1", "qitem": "0", "qirm": "0", "qdrop": "1", "clammo": "2", "maxclients": "4"}
REQUIRE_ROLE = {"host": {"repsync": "1", "qmrmhost": "1"}, "client": {"dmgin": "1"}}
DROP = {"nscap"}
NET_KEYS = {"net", "netpeer","netlisten", "netpeerport", "netdx", "netloss", "netrecord", "netfiles", "netloop"}
REASON = {"luacmd": "test switch", "dmgin": "client damage intake must be on", "qrollback": "release default", "cpp_port": "C++ modules on, no mod Lua",
          "cpp_net": "relay inside the DLL", "b164off": "quest items to both players (b164) in s2m_items", "qitem": "b164: quest items for both", "qirm": "b164: quest items for both",
          "qdrop": "hold D on a quest item drops it (default ON)", "maxclients": "release default: host takes up to 4 clients", "clammo": "b164: corpse loot for both", "qmrmhost": "b164: quest items for both",
          "godown": "god hold during cine freeze and emission (G1 s244)"}
VALUE_RE = re.compile(r"[A-Za-z0-9_.\-]+")

NEEDLES_ALL = ["d:\\modding", "c:\\users", getpass.getuser().lower(), "modwork", "s2dev", "s2coop_inst", "nola_inst", "s2devroot"]
NEEDLES_THIRD = ["d:\\modding", getpass.getuser().lower(), "modwork", "s2dev", "s2coop_inst", "nola_inst"]
RECORDER_NEEDLES = ["nolafr1", "nola fr:", "flightrec", "flightn", "flight recorder", "nola_debug", "dbgsessions", "dbglogcopy", "frlogcopy",
                    "fr_trigger", "s2x_fr", "fr_%", ".ring", ".frd", "ctrl+f11"]
RECORDER_PDB_NEEDLES = [b"s2x_fr.cpp", b"s2x_fr_core", b"x0::fr::", b"frc::", b"fr_trigger", b"mirror_log", b"prune_sessions"]
CENSUS_NEEDLES = ["nola inv ", "nola inv table", "nola wh ", "nola census", "census.json", "invariants.tsv", "nolainv", "frtrig", "lootclose",
                  "unpaired_visible_npc", "one_puppet_per_peer", "orphan_nola_body", "devinvsec", "devwhsec", "census runs=", "census ok "]
CENSUS_PDB_NEEDLES = [b"census_nat", b"census_hk", b"nola_cen_fr_none", b"InvState", b"census_json", b"s2m_world_cen", b"CenNat"]
BENCH_NEEDLES = ["nbot", "bench\\io", "bench\\rec", "bench warn", "rec_%04d", "nbot-script", "s2pluginTick".lower(), "safeteleport", "breadcrumb"]
HOT_NEEDLES = ["nola hot reload", "nola_hot_", "hotstub", "hotreload", "next.dll", "hot_*.dll"]
HOT_PDB_NEEDLES = [b"s2x_hot.cpp", b"x0::hot::Reg", b"x0::hot::shutdown_all", b"nola_hot_start", b"nola_hot_shutdown"]
ALLOWED_EXT = {".dll", ".ini", ".txt", ".pak", ".lua", ".png", ""}
PNG_SIG = b"\x89PNG\r\n\x1a\n"

WARMUP_RE = re.compile(r"nowarmupfailmessage|nopsowarmup|shaderpipelinecache|psoprecach|shaderwarmup|skip\w*warmup|warmup\w*\s*=\s*(0|false|off)\b", re.I)
WARMUP_TEXT_EXT = {".cfg", ".ini", ".txt", ".json", ".lua", ".md", ".in", ".cs", ""}
DOCS = ("README.md", "INSTALL.md", "MANUAL_INSTALL.md")

AUDIT = []
SRC_DLL = []
LINT = []

sys.path.insert(0, TOOLS)
import check_pak_nodes
import check_release_tables


def warmup_hits(data):
    text = data.decode("utf-8", "replace") if isinstance(data, bytes) else data
    return sorted(set(m.group(0) for m in WARMUP_RE.finditer(text)))


def warmup_tree_hits(root):
    hits = {}
    for d, _, fs in os.walk(root):
        for f in fs:
            if os.path.splitext(f)[1].lower() in WARMUP_TEXT_EXT:
                h = warmup_hits(open(os.path.join(d, f), "rb").read())
                if h:
                    hits[os.path.relpath(os.path.join(d, f), root).replace("\\", "/")] = h
    return hits


def md5b(b):
    return hashlib.md5(b).hexdigest()


def md5(p):
    h = hashlib.md5()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(1 << 20), b""):
            h.update(c)
    return h.hexdigest()


def die(msg):
    sys.exit("REFUSED: " + msg)


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        die("command failed: %s\n%s%s" % (" ".join(map(str, cmd)), r.stdout[-800:], r.stderr[-800:]))
    return r.stdout


def recorder_hits(dll, pdb=None):
    hits = needle_hits(dll, RECORDER_NEEDLES)
    if pdb:
        for n in RECORDER_PDB_NEEDLES:
            if n in pdb:
                hits.setdefault("pdb:" + n.decode(), []).append("symbol in the PDB")
    return hits


def census_hits(dll, pdb=None):
    hits = needle_hits(dll, CENSUS_NEEDLES)
    if pdb:
        for n in CENSUS_PDB_NEEDLES:
            if n in pdb:
                hits.setdefault("pdb:" + n.decode(), []).append("symbol in the PDB")
    return hits


def check_recorder(path):
    dll = open(path, "rb").read()
    pdbp = os.path.splitext(path)[0] + ".pdb"
    pdb = open(pdbp, "rb").read() if os.path.exists(pdbp) else None
    ch = census_hits(dll, pdb)
    print("census check %s: %d needles, %d PDB symbol needles" % (path, len(CENSUS_NEEDLES), len(CENSUS_PDB_NEEDLES) if pdb else 0))
    for k, v in sorted(ch.items()):
        print("  hit %s: %s" % (k, v[:3]))
    print("%s no world census, family hash or invariant check (NOLA INV) code in the DLL" % ("PASS" if not ch else "FAIL"))
    hits = recorder_hits(dll, pdb)
    print("recorder check %s (md5 %s, pdb %s): %d needles, %d PDB symbol needles" % (path, md5b(dll), "yes" if pdb else "no", len(RECORDER_NEEDLES), len(RECORDER_PDB_NEEDLES) if pdb else 0))
    for k, v in sorted(hits.items()):
        print("  hit %s: %s" % (k, v[:3]))
    print("%s no flight recorder / debug session code in the DLL" % ("PASS" if not hits else "FAIL"))
    bh = needle_hits(dll, BENCH_NEEDLES)
    print("bench check %s: %d needles" % (path, len(BENCH_NEEDLES)))
    for k, v in sorted(bh.items()):
        print("  hit %s: %s" % (k, v[:3]))
    print("%s no bench bot / route recorder code in the DLL" % ("PASS" if not bh else "FAIL"))
    hh = hot_hits(dll, pdb)
    print("hot reload check %s: %d needles, %d PDB symbol needles" % (path, len(HOT_NEEDLES), len(HOT_PDB_NEEDLES) if pdb else 0))
    for k, v in sorted(hh.items()):
        print("  hit %s: %s" % (k, v[:3]))
    print("%s no hot reload stub / payload shutdown code in the DLL" % ("PASS" if not hh else "FAIL"))
    return not hits and not ch and not bh and not hh


def hot_hits(dll, pdb=None):
    hits = needle_hits(dll, HOT_NEEDLES)
    if pdb:
        for n in HOT_PDB_NEEDLES:
            if n in pdb:
                hits.setdefault("pdb:" + n.decode(), []).append("symbol in the PDB")
    return hits


def ascii_and_wide(data):
    out = [m.group().decode("ascii") for m in re.finditer(rb"[\x20-\x7e]{5,}", data)]
    out += [m.group().decode("utf-16le") for m in re.finditer(rb"(?:[\x20-\x7e]\x00){5,}", data)]
    return out


def needle_hits(data, needles):
    hits = {}
    low = [s.lower().replace(SHIPPED_SCRIPT_NAME, "") for s in ascii_and_wide(data)]
    for n in needles:
        h = sorted(set(s for s in low if n in s))
        if h:
            hits[n] = h
    return hits


def pe_pdb_patch(data):
    b = bytearray(data)
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    nsec = struct.unpack_from("<H", b, pe + 6)[0]
    osz = struct.unpack_from("<H", b, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", b, opt)[0]
    ddoff = opt + (112 if magic == 0x20B else 96)
    rva, size = struct.unpack_from("<II", b, ddoff + 6 * 8)
    secs = []
    so = opt + osz
    for i in range(nsec):
        vsz, vad, rsz, rptr = struct.unpack_from("<IIII", b, so + 40 * i + 8)
        secs.append((vad, max(vsz, rsz), rptr))

    def off(r):
        for vad, sz, rptr in secs:
            if vad <= r < vad + sz:
                return r - vad + rptr
        die("bad RVA in PE")

    done = 0
    base = off(rva)
    for i in range(size // 28):
        e = base + 28 * i
        typ = struct.unpack_from("<I", b, e + 12)[0]
        ptr = struct.unpack_from("<I", b, e + 24)[0]
        if typ == 2 and bytes(b[ptr:ptr + 4]) == b"RSDS":
            s = ptr + 24
            end = b.index(0, s)
            new = b"NolaNative.pdb"
            b[s:end] = new + b"\x00" * (end - s - len(new))
            done += 1
    return bytes(b), done


def role_tables(role):
    return DEFAULT_TABLES + [ROLE_TABLE % role]


def load_tables(files):
    d = {}
    t = {}
    for f in files:
        for i, ln in enumerate(open(DATA_DIR + "/" + f, encoding="utf-8").read().split("\n")):
            if i == 0 or not ln:
                continue
            p = ln.split("\t")
            if len(p) == 3 and p[0].startswith("s"):
                v = p[2]
                if p[1] == "b":
                    v = "1" if v == "true" else "0"
                d[p[0][1:]] = v
                t[p[0][1:]] = p[1]
    return d, t


def same_val(a, b):
    try:
        return float(a) == float(b)
    except ValueError:
        return a == b


def release_flags(role):
    f = "flags_%s.txt" % role
    defaults, types = load_tables(role_tables(role))
    kv = {}
    for k, v in defaults.items():
        if k.startswith("fps"):
            AUDIT.append("%s: %s=%s not written (frame cap is a dev tool, never shipped)" % (f, k, v))
        elif k in DROP:
            AUDIT.append("%s: %s=%s not written (dev/test key; the DLL default applies)" % (f, k, v))
        elif k in NET_KEYS:
            AUDIT.append("%s: %s=%s not written (the installer writes the net keys; the DLL default applies otherwise)" % (f, k, v))
        elif v == "":
            AUDIT.append("%s: %s not written (empty string default; flags.txt cannot carry it, the DLL default applies)" % (f, k))
        elif not VALUE_RE.fullmatch(v):
            die("%s: %s=%r has characters the DLL flags.txt parser does not read" % (f, k, v))
        else:
            kv[k] = v
    want = {k: "0" for k in TEST_OFF}
    want.update(REQUIRE_ALL)
    want.update(REQUIRE_ROLE[role])
    bad = ["%s=%s (needs %s: %s)" % (k, defaults.get(k, "(no default)"), v, REASON.get(k, "test switch off" if k in TEST_OFF else "core feature on"))
           for k, v in sorted(want.items()) if (k in defaults or k not in TEST_OFF) and not same_val(defaults.get(k, ""), v)]
    if bad:
        die("the %s defaults (%s) do not hold the release values: %s" % (role, " < ".join(role_tables(role)), "; ".join(bad)))
    lines = ["%s=%s" % (k, v) for k, v in kv.items()]
    return ("\r\n".join(lines) + "\r\n").encode("ascii"), len(lines), defaults


def role_split_keys():
    _, _, dh = release_flags("host")
    _, _, dc = release_flags("client")
    return sorted(k for k in set(dh) | set(dc) if not same_val(dh.get(k, ""), dc.get(k, "")))


def shared_flags():
    dh, nh, _ = release_flags("host")
    dc, nc, _ = release_flags("client")
    h = [l for l in dh.decode("ascii").split("\r\n") if l]
    c = dict(l.split("=", 1) for l in dc.decode("ascii").split("\r\n") if l)
    split = role_split_keys()
    lines = []
    for l in h:
        k, v = l.split("=", 1)
        if k in split:
            continue
        if k not in c or not same_val(c[k], v):
            split.append(k)
            continue
        lines.append(l)
    for k in sorted(set(split)):
        AUDIT.append("flags.txt: %s not written (the value differs by role; the DLL applies the role defaults of the role picked in the main menu)" % k)
    return ("\r\n".join(lines) + "\r\n").encode("ascii"), len(lines), sorted(set(split))


def read_template(path):
    kv = {}
    raw = {}
    for ln in open(path, encoding="ascii").read().splitlines():
        if "=" in ln and ln.strip() and not ln.lstrip().startswith("#"):
            k, v = ln.split("=", 1)
            kv[k.strip()] = v.strip()
            raw.setdefault(k.strip(), []).append(v.strip())
    dup = sorted("%s=%s" % (k, ",".join(vs)) for k, vs in raw.items() if len(vs) > 1)
    return kv, dup


def dev_diff(role, defaults):
    kv, dup = read_template(LIVE_FLAGS[role])
    differ = []
    ignored = []
    for k, v in kv.items():
        if k not in defaults:
            ignored.append("%s=%s" % (k, v))
        elif not same_val(v, defaults[k]):
            differ.append("%s dev=%s release=%s" % (k, v, defaults[k]))
    return sorted(differ), sorted(ignored), dup, len(kv)


def runtime_tables(dll):
    fails, info = check_release_tables.check(dll, DATA_DIR, os.path.join(os.path.dirname(DATA_DIR), "src"), sorted(TEST_OFF))
    return fails, info


def check_embedded(dll, role):
    missing = [t for t in role_tables(role) if open(DATA_DIR + "/" + t, "rb").read() not in dll]
    if missing:
        die("%s not embedded byte-identical in the DLL: rebuild the DLL" % ", ".join(missing))


def shipped_sources():
    out = {}
    for name in sorted(os.listdir(PAKSRC)):
        d = PAKSRC + "/" + name
        if not os.path.isdir(d) or name in DEV_ONLY_SRC:
            continue
        files = {}
        for root, _, fs in os.walk(d):
            for f in fs:
                p = os.path.join(root, f)
                files[os.path.relpath(p, d).replace("\\", "/")] = p
        if not files:
            die("pak source folder is empty: " + d)
        out[name] = files
    if not out:
        die("no pak source folders under " + PAKSRC)
    return out


def source_hashes(srcs):
    seen = {}
    out = {}
    for name, files in srcs.items():
        for rel, p in files.items():
            key = GAMEDATA_REL + rel
            if key.lower() in seen:
                die("same path in two pak source folders: %s (%s and %s)" % (key, seen[key.lower()], name))
            seen[key.lower()] = name
            out[key] = hashlib.sha256(open(p, "rb").read()).hexdigest()
    return out


def content_hash(hashes):
    return hashlib.sha1("\n".join("%s %s" % (k, hashes[k]) for k in sorted(hashes)).encode()).hexdigest()


def pak_hashes(pak, strip):
    out = {}
    for ln in run([REPAK, "hash-list", "-s", strip, pak]).splitlines():
        if ln.strip():
            h, p = ln.split(" ", 1)
            out[p.strip()] = h
    return out


def verify_sources_in(pak):
    want = source_hashes(shipped_sources())
    got = pak_hashes(pak, PAK_MOUNT)
    missing = sorted(p for p in want if p not in got)
    extra = sorted(p for p in got if p not in want)
    diff = sorted(p for p in want if p in got and got[p] != want[p])
    if missing or diff or extra:
        die("shipped pak %s differs from paksrc: missing %s hash-diff %s not in paksrc %s" % (pak, missing[:5], diff[:5], extra[:5]))
    return len(want)


def stock_edit_hits(stage):
    hits = []
    for root, _, fs in os.walk(stage):
        for f in fs:
            p = os.path.join(root, f)
            rel = os.path.relpath(p, stage).replace("\\", "/")
            if STOCK_FILE_RE.search(rel):
                hits.append(rel)
                continue
            if "QuestNodePrototypes/" not in rel or not f.lower().endswith(".cfg"):
                continue
            depth = 0
            for ln in open(p, encoding="utf-8-sig", errors="replace"):
                t = ln.strip()
                if depth == 0:
                    m = TOP_NODE_RE.match(t)
                    if m:
                        depth = 1
                        if not MOD_NODE_RE.search(m.group(1)):
                            hits.append(rel + ":" + m.group(1))
                    continue
                if t.endswith("struct.begin"):
                    depth += 1
                elif t == "struct.end":
                    depth -= 1
    return hits


def build_merged_pak(work, dest):
    srcs = shipped_sources()
    hashes = source_hashes(srcs)
    base = os.path.join(work, "both")
    stage = os.path.join(base, "zzzzzz_Nola_P")
    shutil.rmtree(base, ignore_errors=True)
    os.makedirs(stage)
    for name, files in srcs.items():
        for rel, p in files.items():
            d = os.path.join(stage, GAMEDATA_REL, rel)
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copyfile(p, d)
    se = stock_edit_hits(stage)
    if se:
        die("the pak replaces stock save settings or stock quest nodes (the session switches those at runtime): %s" % se[:5])
    wh = warmup_tree_hits(stage)
    if wh:
        die("the pak config disables or skips the shader warmup / PSO cache: %s" % dict(list(wh.items())[:5]))
    run([REPAK, "pack", "-q", "--version", "V3", "--mount-point", PAK_MOUNT, stage])
    merged = stage + ".pak"
    verify_sources_in(merged)
    nodes, fails, _ = check_pak_nodes.check_tree([stage], DATA_DIR)
    if fails:
        die("the merged pak lacks node sets the DLL needs (%d nodes): %s" % (len(nodes), "; ".join(fails)))
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    shutil.copy2(merged, dest)
    shutil.rmtree(base)
    PAKSRC_INFO.update({"folders": {n: len(f) for n, f in srcs.items()}, "files": len(hashes), "content": content_hash(hashes)})
    return len(srcs), len(hashes)


def png_size(data):
    if len(data) < 33 or data[:8] != PNG_SIG or data[12:16] != b"IHDR":
        return None
    w, h = struct.unpack_from(">II", data, 16)
    return (w, h) if 16 <= w <= 4096 and 16 <= h <= 4096 else None


def title_src(dll, title):
    # The title art ships from the same repo checkout as the DLL: <repo>/cppbuild/... -> <repo>/assets/nola_title.png
    if title:
        return title.replace("\\", "/")
    d = dll.replace("\\", "/")
    i = d.lower().find("/cppbuild/")
    return (d[:i] + "/assets/nola_title.png") if i > 0 else TITLE_SRC


def title_image(path):
    if not os.path.isfile(path):
        die("title image %s not found: the release ships the main menu title art (without it the DLL leaves the stock main menu as is)" % path)
    data = open(path, "rb").read()
    size = png_size(data)
    if not size:
        die("title image %s is not a PNG of 16..4096 px" % path)
    TITLE_INFO.update({"src": path, "md5": md5b(data), "w": size[0], "h": size[1], "bytes": len(data)})
    return data


def strip_lua_comments(text):
    lines = [l for l in text.replace("\r\n", "\n").split("\n") if not l.lstrip().startswith("--")]
    return ("\n".join(lines).lstrip("\n")).encode("ascii")


def stage_payload(out, a):
    pl = out + "/payload"
    shutil.rmtree(pl, ignore_errors=True)
    os.makedirs(pl + "/ue4ss/Mods/NolaNative/dlls")
    os.makedirs(pl + "/ue4ss/UE4SS_Signatures")
    os.makedirs(pl + "/paks")
    os.makedirs(pl + "/flags")

    with zipfile.ZipFile(UE4SS_ZIP) as z:
        files = {
            "dwmapi.dll": z.read("dwmapi.dll"),
            "ue4ss/UE4SS.dll": z.read("ue4ss/UE4SS.dll"),
            "ue4ss/UE4SS-settings.ini": z.read("ue4ss/UE4SS-settings.ini"),
            "ue4ss/LICENSE": z.read("ue4ss/LICENSE"),
        }
    for rel, live in (("ue4ss/UE4SS.dll", LIVE_UE4SS + "/UE4SS.dll"), ("dwmapi.dll", LIVE_DWMAPI)):
        if os.path.exists(live) and md5b(files[rel]) != md5(live):
            die("%s in the 5e627997 zip differs from the live copy" % rel)
    vt = open(GAMECFG + "/VTableLayout.ini", "rb").read()
    if os.path.exists(LIVE_UE4SS + "/VTableLayout.ini") and md5b(vt) != md5(LIVE_UE4SS + "/VTableLayout.ini"):
        die("VTableLayout.ini differs from the live copy")
    files["ue4ss/VTableLayout.ini"] = vt
    for s in SIGNATURES:
        src = open(GAMECFG + "/UE4SS_Signatures/%s.lua" % s, "rb").read()
        live = LIVE_UE4SS + "/UE4SS_Signatures/%s.lua" % s
        if os.path.exists(live) and md5(live) not in (md5b(src), md5b(strip_lua_comments(src.decode("ascii")))):
            die("signature %s differs from the live copy" % s)
        files["ue4ss/UE4SS_Signatures/%s.lua" % s] = strip_lua_comments(src.decode("ascii"))
    ini = files["ue4ss/UE4SS-settings.ini"]
    for key in (b"MajorVersion", b"MinorVersion"):
        ini, n = re.subn(rb"(?m)^" + key + rb"[ \t]*=[^\r\n]*", key + b" = 5", ini, count=1)
        if n != 1:
            die("UE4SS-settings.ini has no %s line to set to 5" % key.decode())
    files["ue4ss/UE4SS-settings.ini"] = ini
    files["ue4ss/Mods/mods.txt"] = b"NolaNative : 1\r\n"

    dll = open(a.dll, "rb").read()
    SRC_DLL[:] = [a.dll]
    src_md5 = md5b(dll)
    if a.keep_pdb:
        patched = 0
    else:
        dll, patched = pe_pdb_patch(dll)
        if patched != 1:
            die("expected one CodeView entry to neutralise, found %d" % patched)
    files["ue4ss/Mods/NolaNative/dlls/main.dll"] = dll
    os.makedirs(pl + "/" + os.path.dirname(TITLE_REL))
    files[TITLE_REL] = title_image(title_src(a.dll, a.title))

    flag_info = {}
    for role in ("host", "client"):
        check_embedded(dll, role)
    rt_fails, rt_info = runtime_tables(dll)
    if rt_fails:
        die("runtime tables / dev inputs (see %s):\n  " % RELEASE_DOC + "\n  ".join(rt_fails))
    for x in rt_info:
        print("ok     runtime tables: " + x)
    data, n, split = shared_flags()
    files["flags/flags.txt"] = data
    flag_info["both"] = (n, split)

    pak_info = build_merged_pak(a.work, pl + "/paks/" + PAK_NAME)

    for rel, data in files.items():
        with open(pl + "/" + rel, "wb") as fh:
            fh.write(data)
    return src_md5, patched, flag_info, pak_info


def payload_rows(pl):
    rows = []
    for root, _, fs in os.walk(pl):
        for f in sorted(fs):
            p = os.path.join(root, f)
            rows.append((os.path.relpath(p, pl).replace("\\", "/"), os.path.getsize(p), md5(p)))
    return sorted(rows)


def lint(out, pl, rows, src_md5, patched):
    res = []

    def check(name, ok, detail=""):
        res.append((name, ok, detail))

    names = [r[0] for r in rows]
    exts = sorted(set(os.path.splitext(n)[1].lower() for n in names))
    check("only dll/ini/txt/pak/lua(signatures)/png(title) files shipped", set(exts) <= ALLOWED_EXT, str(exts))
    pngs = [n for n in names if n.lower().endswith(".png")]
    check("one PNG: the main menu title image %s" % TITLE_REL, pngs == [TITLE_REL], str(pngs))
    tdata = open(pl + "/" + TITLE_REL, "rb").read() if os.path.exists(pl + "/" + TITLE_REL) else b""
    check("title image is a 16..4096 px PNG, byte-identical to %s" % TITLE_INFO.get("src", TITLE_SRC), png_size(tdata) is not None and md5b(tdata) == TITLE_INFO.get("md5"), str(png_size(tdata)))
    isrc = open(INSTALLER_SRC, encoding="utf-8").read()
    check("installer script installs %s" % TITLE_REL, ("($Ue4ssRel + '/%s')" % TITLE_REL[len("ue4ss/"):]) in isrc)
    res.append(("INFO title image %s %dx%d, %d bytes, md5 %s" % (TITLE_REL, TITLE_INFO.get("w", 0), TITLE_INFO.get("h", 0), len(tdata), md5b(tdata)), True, ""))
    lua = [n for n in names if n.lower().endswith(".lua")]
    check("no mod Lua: only the 6 UE4SS signature files", sorted(lua) == sorted("ue4ss/UE4SS_Signatures/%s.lua" % s for s in SIGNATURES), str(len(lua)))
    for banned in (".py", ".ps1", ".bat", ".cmd", ".js", ".bak", ".pdb", ".log", ".exe", ".tsv"):
        check("no %s file" % banned, not [n for n in names if n.lower().endswith(banned)])
    bak = []
    for root, _, fs in os.walk(out):
        bak += [os.path.join(root, f) for f in fs if ".bak" in f.lower() or f.lower().endswith(".pdb")]
    check("no .bak/.pdb anywhere in the candidate", not bak, str(bak))
    check("no fps*/relay/python names", not [n for n in names if re.search(r"fps|relay|python", n, re.I)])
    if "ue4ss/VTableLayout.ini" in names:
        ini_txt = open(pl + "/ue4ss/UE4SS-settings.ini", "rb").read().decode("latin-1")
        evo = re.search(r"(?ms)^\[EngineVersionOverride\][^\[]*", ini_txt)
        evo_t = evo.group(0) if evo else ""
        check("VTableLayout.ini shipped => UE4SS-settings.ini [EngineVersionOverride] MajorVersion = 5 and MinorVersion = 5",
              bool(re.search(r"(?m)^MajorVersion\s*=\s*5\s*$", evo_t)) and bool(re.search(r"(?m)^MinorVersion\s*=\s*5\s*$", evo_t)), evo_t[:80])
    mods = open(pl + "/ue4ss/Mods/mods.txt", "rb").read().decode().strip()
    check("mods.txt holds only 'NolaNative : 1'", mods == "NolaNative : 1", mods)
    check("one flags.txt for both roles (no flags_host/flags_client, no role.txt)", sorted(n for n in names if n.startswith("flags/")) == ["flags/flags.txt"], str([n for n in names if n.startswith("flags/")]))
    txt = open(pl + "/flags/flags.txt", "rb").read().decode()
    ls = [l for l in txt.split("\r\n") if l]
    keys = [l.split("=")[0] for l in ls]
    split = role_split_keys()
    check("flags.txt: no duplicate keys", len(keys) == len(set(keys)))
    check("flags.txt: no comment lines", not [l for l in ls if l[0] in "#;"])
    check("flags.txt: no fps*/net*/test-on keys",
          not [k for k in keys if k.startswith("fps") or k in NET_KEYS or (k in TEST_OFF and dict(l.split("=") for l in ls)[k] != "0")])
    check("flags.txt: no key whose default differs by role (%d split keys follow the menu role)" % len(split), not [k for k in keys if k in split], str([k for k in keys if k in split]))
    d = dict(l.split("=") for l in ls)
    check("flags.txt: qrollback/cpp_port/cpp_net/sxjoin/dmgin/joinmenu/menurole=1", all(d.get(k) == "1" for k in ("qrollback", "cpp_port", "cpp_net", "sxjoin", "dmgin", "joinmenu", "menurole")))
    sig_comments = [n for n in lua if [l for l in open(pl + "/" + n).read().split("\n") if l.lstrip().startswith("--")]]
    check("no comments in shipped Lua (signature files)", not sig_comments, str(sig_comments))
    for n in names:
        data = open(pl + "/" + n, "rb").read()
        third = n in ("ue4ss/UE4SS.dll", "dwmapi.dll", "ue4ss/UE4SS-settings.ini", "ue4ss/LICENSE", "ue4ss/VTableLayout.ini")
        hits = needle_hits(data, NEEDLES_THIRD if third else NEEDLES_ALL)
        check("no dev paths/names in " + n, not hits, str({k: v[:2] for k, v in hits.items()}))
    for n in names:
        if os.path.splitext(n)[1].lower() in WARMUP_TEXT_EXT:
            h = warmup_hits(open(pl + "/" + n, "rb").read())
            check("no shader warmup / PSO cache switch in " + n, not h, str(h))
    for doc in DOCS:
        if os.path.exists(out + "/" + doc):
            h = warmup_hits(open(out + "/" + doc, "rb").read())
            check("no shader warmup / PSO cache switch in " + doc, not h, str(h))
    for src in (INSTALLER_SRC, MANUAL_TEMPLATE):
        h = warmup_hits(open(src, "rb").read())
        check("no shader warmup / PSO cache switch in " + os.path.basename(src), not h, str(h))
    dll = open(pl + "/ue4ss/Mods/NolaNative/dlls/main.dll", "rb").read()
    srcpdb = os.path.splitext(SRC_DLL[0])[0] + ".pdb" if SRC_DLL else ""
    rh = recorder_hits(dll, open(srcpdb, "rb").read() if srcpdb and os.path.exists(srcpdb) else None)
    check("no debug tools/telemetry in main.dll: no flight recorder, session archive or their PDB symbols (S2X_RELEASE build)", not rh, str({k: v[:2] for k, v in rh.items()}))
    chh = census_hits(dll, open(srcpdb, "rb").read() if srcpdb and os.path.exists(srcpdb) else None)
    check("no debug tools/telemetry in main.dll: no world census, family hashes (NOLA WH) or invariant checks (NOLA INV) (S2X_RELEASE build)", not chh, str({k: v[:2] for k, v in chh.items()}))
    hth = hot_hits(dll, open(srcpdb, "rb").read() if srcpdb and os.path.exists(srcpdb) else None)
    check("no dev tools in main.dll: no hot reload stub, payload shutdown or their PDB symbols (S2X_RELEASE build)", not hth, str({k: v[:2] for k, v in hth.items()}))
    pdbs = [s for s in ascii_and_wide(dll) if s.lower().endswith(".pdb")]
    check("main.dll embedded PDB path is neutral", all(":" not in s and "\\" not in s and "/" not in s for s in pdbs), str(pdbs))
    stock_runner = len(needle_hits(open(pl + "/ue4ss/UE4SS.dll", "rb").read(), ["c:\\users\\runneradmin"]).get("c:\\users\\runneradmin", []))
    res.append(("INFO UE4SS.dll (third party) holds %d GitHub-runner paths c:\\users\\runneradmin, none of ours" % stock_runner, True, ""))
    res.append(("INFO main.dll source md5 %s, shipped md5 %s, PDB path entries patched %d" % (src_md5, md5b(dll), patched), True, ""))
    res.append(("INFO main.dll wide dev string", True, str(needle_hits(dll, ["c:\\program files"]))))
    try:
        n = verify_sources_in(pl + "/paks/" + PAK_NAME)
        check("shipped pak holds exactly the %d paksrc files, byte-identical" % n, True)
    except SystemExit as e:
        check("shipped pak holds exactly the paksrc files", False, str(e))
    nodes, fails, _ = check_pak_nodes.check_pak(pl + "/paks/" + PAK_NAME, DATA_DIR)
    check("shipped pak holds every node set the DLL needs (%d nodes; check_pak_nodes.py)" % len(nodes), not fails, "; ".join(fails))
    rt_fails, rt_info = runtime_tables(dll)
    check("every runtime-read table is embedded, no dev input or dev flag reachable (check_release_tables.py, %s)" % RELEASE_DOC, not rt_fails, " | ".join(rt_fails))
    return res


def installer_consts():
    src = open(INSTALLER_SRC, encoding="utf-8").read()

    def grab(pat):
        m = re.search(pat, src)
        if not m:
            die("installer constant not found: " + pat)
        return m.group(1)

    return {"HOST_PORT": grab(r"\$HostPort = (\d+)"), "CLIENT_PORT": grab(r"\$ClientPort = (\d+)"),
            "GAME_FOLDER": grab(r"\$GameFolderName = '([^']+)'"), "EXE": grab(r"\$ExeName = '([^']+)'"),
            "BRIDGE": grab(r"\$DefaultBridge = '([^']+)'")}


def manual_entries(pl):
    ents = []
    for root, _, fs in os.walk(pl):
        for f in sorted(fs):
            rel = os.path.relpath(os.path.join(root, f), pl).replace("\\", "/")
            src = pl + "/" + rel
            if rel.startswith("paks/"):
                ents.append(("game/" + MODS_REL + "/" + rel.split("/", 1)[1], src, None))
            elif rel == "flags/flags.txt":
                ents.append(("C/nola_bridge/flags.txt", src, None))
            elif rel in KEEP_IF_PRESENT:
                ents.append(("if-missing/game/" + WIN64_REL + "/" + rel, src, None))
            else:
                ents.append(("game/" + WIN64_REL + "/" + rel, src, None))
    return sorted(ents, key=lambda e: e[0])


def render_doc(name, consts, version):
    text = open(TOOLS + "/installer/" + name + ".in", encoding="utf-8").read()
    subs = dict(consts)
    subs["VERSION"] = version
    for k, v in subs.items():
        text = text.replace("{{%s}}" % k, v)
    left = re.findall(r"\{\{[A-Z_0-9]+\}\}", text)
    if left:
        die("unreplaced placeholders in %s: %s" % (name, left))
    if not text.isascii():
        die(name + " holds a non-ASCII character")
    if needle_hits(text.encode("ascii"), NEEDLES_ALL):
        die("dev string inside " + name)
    if warmup_hits(text):
        die("%s disables or skips the shader warmup / PSO cache: %s" % (name, warmup_hits(text)))
    bad = DOC_FORBIDDEN_RE.findall(text)
    if bad:
        die("forbidden word in %s: %s" % (name, sorted(set(bad))))
    return text.replace("\r\n", "\n").encode("ascii")


def build_manual(out, version):
    pl = out + "/payload"
    consts = installer_consts()
    ents = manual_entries(pl)
    data = {}
    for name, src, raw in ents:
        data[name] = raw if raw is not None else open(src, "rb").read()
    g64 = "game/" + WIN64_REL + "/"
    m64 = "if-missing/game/" + WIN64_REL + "/"
    win64_files = sorted([n[len(g64):] for n in data if n.startswith(g64)] + [n[len(m64):] for n in data if n.startswith(m64)])
    copy_list = "\n".join("   - `GAME\\%s`" % n[len("game/"):].replace("/", "\\") for n in sorted(data) if n.startswith("game/"))
    backup_win64 = "\n".join(["- `%s`" % f.replace("/", "\\") for f in win64_files] +
                             ["- the whole folders `ue4ss\\Mods\\S2CoopAlife`, `ue4ss\\Mods\\S2CoopNative` and `ue4ss\\Mods\\NolaAlife`, with their sub-folders (older versions of the mod)"])
    delete_list = "\n".join(["   - `WIN64\\%s`" % f.replace("/", "\\") for f in win64_files] +
                            ["   - `GAME\\Stalker2\\Content\\Paks\\~mods\\zzzzzz_Nola_P.pak`",
                             "   - `%s\\role.txt` and `%s\\flags.txt`" % (consts["BRIDGE"], consts["BRIDGE"])])
    text = open(MANUAL_TEMPLATE, encoding="utf-8").read()
    subs = dict(consts)
    subs.update({"VERSION": version, "COPY_LIST": copy_list, "BACKUP_WIN64": backup_win64, "DELETE_LIST": delete_list})
    for k, v in subs.items():
        text = text.replace("{{%s}}" % k, v)
    left = re.findall(r"\{\{[A-Z_0-9]+\}\}", text)
    if left:
        die("unreplaced placeholders in the manual: %s" % left)
    if needle_hits(text.encode("ascii"), NEEDLES_ALL):
        die("dev string inside MANUAL_INSTALL.md")
    if warmup_hits(text):
        die("MANUAL_INSTALL.md disables or skips the shader warmup / PSO cache: %s" % warmup_hits(text))
    isrc = open(INSTALLER_SRC, encoding="utf-8").read()
    for need in ("`net=steam`", "`net=ip`", "`S2NET route: steam`"):
        if need not in text:
            die("MANUAL_INSTALL.md does not tell the player about " + need)
    for need in ("[string]$Net", "Append('net=')"):
        if need not in isrc:
            die("installer source does not write the net= line: " + need)
    for n in data:
        if re.search(r"fps|relay|python", n, re.I):
            die("forbidden name in the manual zip: " + n)
    doc = text.replace("\r\n", "\n").encode("ascii")
    extra_docs = [(n, render_doc(n, consts, version)) for n in DOC_TEMPLATES]
    sums = "".join("%s  %s\n" % (md5b(d), n) for n, d in sorted(data.items())).encode("ascii")
    zpath = out + "/NOLA-Manual.zip"
    if os.path.exists(zpath):
        os.remove(zpath)
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        z.writestr(ZIP_ROOT + "/MANUAL_INSTALL.md", doc)
        for n, d in extra_docs:
            z.writestr(ZIP_ROOT + "/" + n, d)
        z.writestr(ZIP_ROOT + "/CHECKSUMS.txt", sums)
        for name, d in sorted(data.items()):
            z.writestr(ZIP_ROOT + "/" + name, d)
    with open(out + "/MANUAL_INSTALL.md", "wb") as fh:
        fh.write(doc)
    for n, d in extra_docs:
        with open(out + "/" + n, "wb") as fh:
            fh.write(d)
    rows = {r[0]: r[2] for r in payload_rows(pl)}
    with zipfile.ZipFile(zpath) as z:
        got = {i.filename[len(ZIP_ROOT) + 1:]: md5b(z.read(i)) for i in z.infolist() if not i.filename.endswith("/")}
    bad = 0
    for name, src, raw in ents:
        want = md5b(raw) if raw is not None else rows[os.path.relpath(src, pl).replace("\\", "/")]
        if got.get(name) != want:
            bad += 1
    if bad or len(got) != len(ents) + 2 + len(extra_docs):
        die("manual zip does not match the payload md5s (%d bad, %d entries)" % (bad, len(got)))
    used = set(os.path.relpath(src, pl).replace("\\", "/") for _, src, _ in ents if src)
    if used != set(rows):
        die("manual zip does not cover every installer payload file: %s" % sorted(set(rows) ^ used))
    return zpath, len(got), sorted(got), data, sums, extra_docs


def render_script(name, version):
    raw = open(TOOLS + "/installer/" + name, "rb").read()
    if not raw.isascii():
        die(name + " holds a non-ASCII character")
    text = raw.decode("ascii").replace("\r\n", "\n").replace("{{VERSION}}", version)
    left = re.findall(r"\{\{[A-Z_0-9]+\}\}", text)
    if left:
        die("unreplaced placeholders in %s: %s" % (name, left))
    if needle_hits(text.encode("ascii"), NEEDLES_ALL):
        die("dev string inside %s: %s" % (name, needle_hits(text.encode("ascii"), NEEDLES_ALL)))
    if warmup_hits(text):
        die("%s disables or skips the shader warmup / PSO cache: %s" % (name, warmup_hits(text)))
    if SCRIPT_FORBIDDEN_RE.search(text):
        die("%s holds an encoded command, a download or a hidden window: %s" % (name, SCRIPT_FORBIDDEN_RE.search(text).group(0)))
    return text.replace("\n", "\r\n").encode("ascii")


def build_auto(out, version, data, sums, extra_docs):
    ps1_name = os.path.basename(INSTALLER_SRC)
    scripts = [(n, render_script(n, version)) for n in INSTALLER_BATS + (ps1_name,)]
    ps1 = dict(scripts)[ps1_name].decode("ascii")
    sig = "/ue4ss/UE4SS_Signatures/"
    miss = [n for n in data if sig not in n and os.path.basename(n) not in ps1]
    if miss or "UE4SS_Signatures/" not in ps1:
        die("%s does not install: %s" % (ps1_name, miss))
    for n, d in scripts:
        if n.endswith(".bat") and ('-ExecutionPolicy Bypass -File "%%~dp0%s"' % ps1_name) not in d.decode("ascii"):
            die(n + " does not start " + ps1_name + " from its own folder")
    zpath = out + "/NOLA-Auto.zip"
    if os.path.exists(zpath):
        os.remove(zpath)
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for n, d in scripts:
            z.writestr(AUTO_ROOT + "/" + n, d)
        for n, d in extra_docs:
            z.writestr(AUTO_ROOT + "/" + n, d)
        z.writestr(AUTO_ROOT + "/files/CHECKSUMS.txt", sums)
        for name, d in sorted(data.items()):
            z.writestr(AUTO_ROOT + "/files/" + name, d)
    with zipfile.ZipFile(zpath) as z:
        got = {i.filename[len(AUTO_ROOT) + 1:]: md5b(z.read(i)) for i in z.infolist() if not i.filename.endswith("/")}
    want = {n: md5b(d) for n, d in scripts + extra_docs + [("files/CHECKSUMS.txt", sums)] + [("files/" + n, d) for n, d in data.items()]}
    bad = sorted(n for n in set(got) | set(want) if got.get(n) != want.get(n))
    if bad:
        die("auto zip does not match the payload: %s" % bad[:5])
    return zpath, len(got), sorted(got)


def no_exe_anywhere(out, zips):
    hits = []
    for root, _, fs in os.walk(out):
        for f in fs:
            p = os.path.join(root, f)
            if f.lower().endswith(EXE_EXT) or (not f.lower().endswith(".dll") and open(p, "rb").read(2) == b"MZ"):
                hits.append(p)
    for zp in zips:
        with zipfile.ZipFile(zp) as z:
            for i in z.infolist():
                n = i.filename.lower()
                if n.endswith(EXE_EXT) or (not n.endswith(".dll") and z.read(i)[:2] == b"MZ"):
                    hits.append("%s:%s" % (os.path.basename(zp), i.filename))
    if hits:
        die("an executable would ship: %s" % hits[:5])


def write_manifest(out, rows, extra=()):
    allrows = [(x, os.path.getsize(out + "/" + x), md5(out + "/" + x)) for x in extra] + [("payload/" + r, s, h) for r, s, h in rows]
    with open(out + "/MANIFEST.md", "w", newline="\n") as fh:
        fh.write("# Manifest\n\n`NOLA-Auto.zip` holds `install.bat`, `uninstall.bat` and the plain text script `nola_install.ps1`, with the `payload/` files laid out like the game folder in its `files` folder. `NOLA-Manual.zip` holds the same files with the same layout, for installing by hand. Both PCs must use the same version.\n\n| File | Bytes | md5 |\n|---|---|---|\n")
        for r, s, h in allrows:
            fh.write("| `%s` | %d | %s |\n" % (r, s, h))
        if PAKSRC_INFO:
            fh.write("\nPak: `zzzzzz_Nola_P.pak` is built only from the `paksrc` folders (%s) with repak (V3), %d files, content hash %s (sha1 over the sorted path and sha256 lines; repak does not write the same bytes twice, so the content hash is the stable id). The shipped pak was checked file by file against the sources: no file missing, none extra, none different.\n" % (", ".join("%s %d" % (n, c) for n, c in sorted(PAKSRC_INFO["folders"].items())), PAKSRC_INFO["files"], PAKSRC_INFO["content"]))


def finish(out, version, rows):
    for stale in ("NOLA-Setup.exe",):
        if os.path.exists(out + "/" + stale):
            os.remove(out + "/" + stale)
    zpath, zn, znames, data, sums, extra_docs = build_manual(out, version)
    apath, an, anames = build_auto(out, version, data, sums, extra_docs)
    no_exe_anywhere(out, (zpath, apath))
    print("ok     no %s file and no PE file other than the shipped .dll files in %s, NOLA-Manual.zip or NOLA-Auto.zip" % ("/".join(EXE_EXT), out))
    write_manifest(out, rows, ("NOLA-Auto.zip", "NOLA-Manual.zip", "README.md", "INSTALL.md", "MANUAL_INSTALL.md"))
    print("manual zip %s: %d entries, %d bytes, md5 %s" % (zpath, zn, os.path.getsize(zpath), md5(zpath)))
    print("auto zip %s: %d entries, %d bytes, md5 %s" % (apath, an, os.path.getsize(apath), md5(apath)))
    for n in anames:
        print("  auto  " + n)


def check(a):
    dll =open(a.dll, "rb").read() if os.path.exists(a.dll) else b""
    fails = 0
    for role in ("host", "client"):
        na = len(AUDIT)
        data, n, defaults = release_flags(role)
        miss = [t for t in role_tables(role) if open(DATA_DIR + "/" + t, "rb").read() not in dll]
        fails += len(miss)
        print("%s: release flags %d keys from %s (md5 %s); embedded in %s: %s" % (role, n, " < ".join(role_tables(role)), md5b(data)[:8], a.dll,
                                                                              "all" if not miss else "MISSING " + ", ".join(miss)))
        for x in AUDIT[na:]:
            print("  audit " + x)
        if a.check_out:
            os.makedirs(a.check_out, exist_ok=True)
            with open(a.check_out + "/flags_%s.txt" % role, "wb") as fh:
                fh.write(data)
        differ, ignored, dup, nkeys = dev_diff(role, defaults)
        print("  dev template %s: %d keys, %d differ from the release defaults, %d have no DLL default (the DLL ignores them), %d duplicate" %
              (LIVE_FLAGS[role], nkeys, len(differ), len(ignored), len(dup)))
        for x in differ:
            print("    differs " + x)
        for x in ignored:
            print("    ignored " + x)
        for x in dup:
            print("    duplicate " + x)
    if dll and not check_recorder(a.dll):
        fails += 1
    rt_fails, rt_info = runtime_tables(dll)
    for x in rt_info:
        print("runtime tables: " + x)
    for x in rt_fails:
        print("FAIL runtime tables: " + x)
    if rt_fails:
        print("runtime tables: see %s" % RELEASE_DOC)
    fails += len(rt_fails)
    print("check %s" % ("PASS" if not fails else "FAIL: rebuild the DLL so it embeds the current tables and has no debug recorder (S2X_RELEASE)"))
    if fails:
        sys.exit(1)


def main():
    global DATA_DIR
    ap = argparse.ArgumentParser(description="Builds the NOLA release from the S2X_RELEASE DLL (wt.ps1 build <name> -Release). Every file the DLL reads at runtime must be embedded in it or shipped; dev inputs and dev flags must be compiled out. Rules and the list to edit: " + RELEASE_DOC)
    ap.add_argument("out", nargs="?", default="D:/modding/offline/w4prep/candidate")
    ap.add_argument("--dll", default=DLL_SRC)
    ap.add_argument("--title", default="", help="title PNG; default <repo of --dll>/assets/nola_title.png")
    ap.add_argument("--work", default="D:/modding/offline/w4prep/work")
    ap.add_argument("--version", default="")
    ap.add_argument("--keep-pdb", action="store_true")
    ap.add_argument("--waive", action="append", default=[])
    ap.add_argument("--manual-only", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--check-pak", default="")
    ap.add_argument("--check-out", default="")
    ap.add_argument("--data", default=DATA_DIR)
    ap.add_argument("--pak-only", default="")
    ap.add_argument("--check-recorder", default="")
    a = ap.parse_args()
    if a.check_recorder:
        sys.exit(0 if check_recorder(a.check_recorder) else 1)
    DATA_DIR = a.data.replace("\\", "/").rstrip("/")
    if a.check_pak:
        n = verify_sources_in(a.check_pak.replace("\\", "/"))
        nodes, fails, _ = check_pak_nodes.check_pak(a.check_pak, DATA_DIR)
        if fails:
            die("%s lacks node sets the DLL needs: %s" % (a.check_pak, "; ".join(fails)))
        print("PASS: %s holds exactly the %d paksrc files, byte-identical, and every node set the DLL needs (%d nodes)" % (a.check_pak, n, len(nodes)))
        return
    if a.pak_only:
        dest = a.pak_only.replace("\\", "/").rstrip("/") + "/" + PAK_NAME
        if os.path.normcase(os.path.abspath(dest)).startswith(os.path.normcase(os.path.abspath(GAME + "/Stalker2"))) or "~mods" in dest.lower():
            die("--pak-only must write outside every game folder: " + dest)
        os.makedirs(a.work, exist_ok=True)
        nf, nfiles = build_merged_pak(a.work, dest)
        print("pak %s: %d paksrc folders, %d files, content hash %s, md5 %s, %d bytes" % (dest, nf, nfiles, PAKSRC_INFO["content"], md5(dest), os.path.getsize(dest)))
        for n, c in sorted(PAKSRC_INFO["folders"].items()):
            print("  %-28s %d" % (n, c))
        return
    if a.check:
        check(a)
        return
    out = a.out.replace("\\", "/").rstrip("/")
    os.makedirs(out, exist_ok=True)
    if a.manual_only:
        finish(out, a.version or "candidate", payload_rows(out + "/payload"))
        return
    shutil.rmtree(a.work, ignore_errors=True)
    os.makedirs(a.work)
    src_md5, patched, flag_info, pak_info = stage_payload(out, a)
    version = a.version or "candidate (DLL %s)" % src_md5[:8]
    pl = out + "/payload"
    rows = payload_rows(pl)
    res = lint(out, pl, rows, src_md5, patched)
    waived = [r for r in res if not r[1] and any(w in r[0] for w in a.waive)]
    failed = [r for r in res if not r[1] and r not in waived]
    for name, ok, detail in res:
        tag = "ok" if ok else ("WAIVED" if (name, ok, detail) in waived else "FAIL")
        print("%-6s %s %s" % (tag, name, detail if (detail and not ok) or name.startswith("INFO") else ""))
    if failed:
        die("lint failed: %d checks" % len(failed))
    finish(out, version, rows)
    shutil.rmtree(a.work, ignore_errors=True)
    print("payload files %d" % len(rows))
    print("one pak for both roles: %d paksrc folders packed into 1 pak (%d files); not shipped: %s" % (pak_info[0], pak_info[1], NOT_SHIPPED))
    print("flags.txt for both roles: %d keys; %d keys left to the DLL role defaults of the menu role: %s" % (flag_info["both"][0], len(flag_info["both"][1]), ", ".join(flag_info["both"][1])))
    print("flag audit (%d entries):" % len(AUDIT))
    for x in AUDIT:
        print("  " + x)


if __name__ == "__main__":
    main()
