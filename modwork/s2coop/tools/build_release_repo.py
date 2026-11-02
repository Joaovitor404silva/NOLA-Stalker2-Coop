"""build_release_repo.py - fills the clean release repo (ModWork/release/S2Coop) from the LIVE install on this PC.
  uv run --no-project python -I build_release_repo.py [out_dir]
Copies: mod scripts, paks by role (as installed in the host and client ~mods, except the avatar pak, which is built from paksrc/avatar with repak), pak sources, the bridge tables the mod loads,
default flags, the relay. Writes MANIFEST.md (md5 of every file). Never touches git, never uploads."""
import hashlib, os, shutil, subprocess, sys, tempfile

GAME = "C:/Program Files (x86)/Steam/steamapps/common/S.T.A.L.K.E.R. 2 Heart of Chornobyl"
MW = GAME + "/ModWork"
S2 = MW + "/s2coop"
OUT = sys.argv[1] if len(sys.argv) > 1 else MW + "/release/S2Coop"
HOST_MODS = GAME + "/Stalker2/Content/Paks/~mods"
CLIENT_MODS = "C:/S2DevRoot/Stalker2/Content/Paks/~mods"      # b178rel: the client runs from the dev root now (C:/S2CoopClientRoot is stale)
HOST_NATIVE = S2 + "/Mods/S2CoopNative/dlls/main.dll"          # b178rel: C++ mod, host copy
CLIENT_NATIVE = S2 + "/ModsClient/S2CoopNative/dlls/main.dll"  # client copy (must be byte-identical)
UE4SS_DLL = GAME + "/Stalker2/Binaries/Win64/ue4ss/UE4SS.dll"  # the patched UE4SS 5e627997 the DLL is built against (stock v3.0.1 does not match)
UE4SS_PROXY = GAME + "/Stalker2/Binaries/Win64/dwmapi.dll"
UE4SS_LICENSE = GAME + "/Stalker2/Binaries/Win64/ue4ss/LICENSE"   # b199rel: MIT licence of the redistributed UE4SS build
AUDIT = []                                                         # b199rel: every flag change, printed at the end
TOP_OK = {".git", ".github", ".gitattributes", "bridge", "flags", "mod", "pak_src", "paks", "relay", "tools", "ue4ss_bin",
          "README.md", "INSTALL.md", "CHANGELOG.md", "CONTRIBUTING.md", "Install.bat", "Uninstall.bat", "MANIFEST.md"}
MOD_OK = {"mod/S2CoopAlife/Scripts/main.lua", "mod/S2CoopAlife/Scripts/s2core.lua", "mod/S2CoopNative/dlls/main.dll"}
UE4SS_OK = {"ue4ss_bin/UE4SS.dll", "ue4ss_bin/dwmapi.dll", "ue4ss_bin/LICENSE-UE4SS.txt"}
DEV_NAME = ("s2dev", ".bak", ".pdb", ".exe", ".log", ".ilk", ".obj", ".lib", ".exp", ".orig", "plugin", "debugger", "dump", "crash", "~")
REASON = {"luacmd": "test switch: runs any Lua found in lua_cmd.txt of the bridge folder (main.lua runLuaCmd); never in a release",
          "pupmvlog": "diagnostic: read-only 10 s log line of the puppet (s2core.lua pmDiag)",
          "dmgin": "client damage intake must be on: the code applies it only to role client (or dmgtest=1, forced 0); dmgin=0 on B is the god-mode test line",
          "qdrop": "release default ON (flags_default_*.txt qdrop=1); C++ code default is 1, hold D on a quest item drops it",
          "nscap": "test line, 0 is the code default (s2core.lua nscap = 0)"}
BRIDGE = "C:/s2coop_inst/A"
RELAY = "D:/modding/offline/b3/relay_v2.py"
TEST_OFF = {"luacmd", "playergod", "godtest", "debugkeys", "autowalk", "autocontinue", "mirrortest", "cfollow", "pupmvlog",
            "cinetest", "cptest", "ndtest", "emtest", "iprobe", "cinelog", "autoload", "emreset"}
TEST_OFF |= {"dmgtest", "tgtdiag", "pupmvcr"}                    # b178rel: more diagnostics / tests forced to 0
DROP = {"nscap"}                                                  # b178rel: test line on B, 0 is the code default now (MODDING_PLAN item 5)
FORCE_ALL = {"luacmd": "0", "natnpc": "1", "csave": "1", "n7": "1", "n7sv": "1", "sxjoin": "1", "cloot": "1", "clammo": "1", "clmir": "1", "pslot": "1", "dmgin": "1", "qdrop": "1"}
FORCE_ROLE = {"host": {"repsync": "1"}, "client": {"dmgin": "1"}}   # dmgin=0 on B is the god-mode test line
MUST_ON = ["natnpc", "csave", "n7", "n7sv", "sxjoin", "cloot", "clammo", "clmir", "pslot", "lead", "qsync", "vendmir", "dobjgen", "ientab", "cjoin",
           "anomsync", "kzsafe", "tgtnoint", "meshsafe", "lootshare", "nopause", "sleepsync", "qdrop", "dmgin"]   # asserted ON in both flag files after the build
TEST_ONLY_PAKS ={"zzzzzz_S2CoopGod_KeepFall_P.pak"}          # 99,999 HP test rig, never in a release
# bridge files the mod code loads (checked by name in main.lua / s2core.lua); s2c_skip.txt is client only
BRIDGE_FILES = ["cj68.lua", "dm70.lua", "ia71.lua", "s2u_actions.lua", "s2u_anomaly.lua", "s2u_art.tsv", "s2u_artspawn.tsv",
                "s2u_basekit.txt", "s2u_choice.lua", "s2u_cine.lua", "s2u_cont.tsv", "s2u_contk.tsv", "s2u_destruct.lua", "s2u_dlg.lua",
                "s2u_equip.tsv", "s2u_interact.lua", "s2u_items.lua", "s2u_kit.tsv", "s2u_qdrop.lua", "s2u_qitems.lua", "s2u_qpick.lua",
                "s2u_trade.tsv", "s2u_triggers.tsv", "s2u_vendor.lua", "s2u_questitems.txt"]
CLIENT_BRIDGE = [("C:/s2coop_inst/B/s2c_skip.txt", "s2c_skip.txt")]

def md5(p): return hashlib.md5(open(p, "rb").read()).hexdigest()
def cp(src, rel):
    dst = os.path.join(OUT, rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)

def dev_guard():                                                   # b199rel: nothing dev-only may ship (release = mod only)
    bad = []
    for e in os.listdir(OUT):
        if e not in TOP_OK: bad.append("unexpected top-level entry: " + e)
    files = []
    for root, dirs, fs in os.walk(OUT):
        dirs[:] = [d for d in dirs if d != ".git"]
        for f in fs: files.append(os.path.relpath(os.path.join(root, f), OUT).replace("\\", "/"))
    for rel in files:
        low = rel.lower()
        if any(t in low for t in DEV_NAME): bad.append("dev-looking file: " + rel)
        if rel.startswith("mod/") and rel not in MOD_OK: bad.append("unexpected file in mod/: " + rel)
        if rel.startswith("ue4ss_bin/") and rel not in UE4SS_OK: bad.append("unexpected file in ue4ss_bin/: " + rel)
        if rel.startswith("paks/"):
            n = rel.split("/")[-1]
            if not (n.startswith("zzzzzz_S2Coop") and n.endswith("_P.pak")) or any(t in n.lower() for t in ("god", "test", "debug", "dbg")): bad.append("pak not allowed: " + rel)
    for rel in sorted(MOD_OK | UE4SS_OK):
        if rel not in files: bad.append("missing: " + rel)
    for rel in ("ue4ss_bin/UE4SS.dll", "ue4ss_bin/dwmapi.dll", "mod/S2CoopNative/dlls/main.dll"):
        if os.path.exists(os.path.join(OUT, rel)) and b"s2dev" in open(os.path.join(OUT, rel), "rb").read().lower(): bad.append("S2Dev string inside " + rel)
    for f in ("flags_host.txt", "flags_client.txt"):
        for ln in open(os.path.join(OUT, "flags", f)).read().splitlines():
            if ln.split("=")[0] in TEST_OFF and ln.split("=")[1] != "0": bad.append("test flag on in %s: %s" % (f, ln))
    if bad: sys.exit("REFUSED (dev guard):\n  " + "\n  ".join(bad))
    print("dev guard ok:", len(files), "files, no dev-only content")

REPAK = "D:/modding/tools/repak/repak.exe"
AVATAR_PAK = "zzzzzz_S2CoopAvatar_P.pak"
AVATAR_SRC = S2 + "/paksrc/avatar"
AVATAR_MOUNT = "../../../Stalker2/Content/GameLite/GameData/"

def build_avatar():
    base = tempfile.mkdtemp(prefix="s2avatar_")
    stage = os.path.join(base, "zzzzzz_S2CoopAvatar_P")
    shutil.copytree(AVATAR_SRC, stage)
    r = subprocess.run([REPAK, "pack", "-q", "--version", "V3", "--mount-point", AVATAR_MOUNT, stage], capture_output=True, text=True)
    if r.returncode: sys.exit("avatar pak build failed: " + r.stderr[-400:])
    want = {}
    for root, _, files in os.walk(AVATAR_SRC):
        for f in files:
            q = os.path.join(root, f)
            want[os.path.relpath(q, AVATAR_SRC).replace("\\", "/")] = hashlib.sha256(open(q, "rb").read()).hexdigest()
    out = subprocess.run([REPAK, "hash-list", "-s", AVATAR_MOUNT, stage + ".pak"], capture_output=True, text=True).stdout
    got = dict((ln.split(" ", 1)[1].strip(), ln.split(" ", 1)[0]) for ln in out.splitlines() if ln.strip())
    if got != want: sys.exit("built avatar pak differs from paksrc/avatar")
    return stage + ".pak"

def paks(d): return {f: os.path.join(d, f) for f in os.listdir(d) if f.endswith(".pak") and f not in TEST_ONLY_PAKS}

def main():
    for sub in ("mod", "paks", "pak_src", "bridge", "flags", "relay", "ue4ss_bin"):
        shutil.rmtree(os.path.join(OUT, sub), ignore_errors=True)
    for f in ("main.lua", "s2core.lua"):
        cp(S2 + "/Mods/S2CoopAlife/Scripts/" + f, "mod/S2CoopAlife/Scripts/" + f)
    h, c = paks(HOST_MODS), paks(CLIENT_MODS)
    avatar = build_avatar()
    h.pop(AVATAR_PAK, None); c.pop(AVATAR_PAK, None)
    cp(avatar, "paks/both/" + AVATAR_PAK)
    for name in sorted(set(h) | set(c)):
        if name in h and name in c:
            if md5(h[name]) != md5(c[name]): sys.exit("pak differs between host and client: " + name)
            cp(h[name], "paks/both/" + name)
        elif name in h: cp(h[name], "paks/host/" + name)
        else: cp(c[name], "paks/client/" + name)
    for root, _, files in os.walk(AVATAR_SRC):
        for f in files:
            rel = os.path.relpath(os.path.join(root, f), AVATAR_SRC).replace("\\", "/")
            cp(os.path.join(root, f), "pak_src/S2CoopAvatar/" + rel)
    for f in BRIDGE_FILES: cp(BRIDGE + "/" + f, "bridge/" + f)
    for src, f in CLIENT_BRIDGE: cp(src, "bridge/client_only/" + f)
    for side, f in (("A", "flags_host.txt"), ("B", "flags_client.txt")):     # the live, tested flags with test-only switches off
        role = "host" if side == "A" else "client"
        kv = {}; raw = {}                                             # b178rel: the core reads top to bottom, the last line of a key wins
        for ln in open("C:/s2coop_inst/%s/flags.txt" % side, encoding="ascii").read().splitlines():
            if "=" in ln and ln.strip() and not ln.lstrip().startswith("#"):
                k, v = ln.split("=", 1); kv[k.strip()] = v.strip(); raw.setdefault(k.strip(), []).append(v.strip())
        for k, vs in raw.items():                                     # b199rel audit
            if len(vs) > 1: AUDIT.append("%s: duplicate %s=%s, last wins -> %s" % (f, k, ",".join(vs), vs[-1]))
        for k in TEST_OFF:
            if k in kv:
                if kv[k] != "0": AUDIT.append("%s: %s %s -> 0 (%s)" % (f, k, kv[k], REASON.get(k, "test/diagnostic switch")))
                kv[k] = "0"
        for k, v in list(FORCE_ALL.items()) + list(FORCE_ROLE[role].items()):
            if kv.get(k) != v: AUDIT.append("%s: %s %s -> %s (%s)" % (f, k, kv.get(k, "(absent)"), v, REASON.get(k, "core feature forced on")))
            kv[k] = v
        for k in DROP:
            if k in kv: AUDIT.append("%s: %s=%s dropped (%s)" % (f, k, kv[k], REASON.get(k, "dev/test key")))
            kv.pop(k, None)
        bad = [k for k in MUST_ON if kv.get(k) != "1"]
        if bad: sys.exit("REFUSED: core flags not ON in the %s flag set: %s" % (role, ", ".join(bad)))
        lines = ["%s=%s" % kv_ for kv_ in kv.items()]
        os.makedirs(os.path.join(OUT, "flags"), exist_ok=True)
        with open(os.path.join(OUT, "flags", f), "w", newline="\r\n") as fh: fh.write("\n".join(lines) + "\n")
    if md5(HOST_NATIVE) != md5(CLIENT_NATIVE): sys.exit("S2CoopNative main.dll differs between Mods and ModsClient")   # b178rel
    cp(HOST_NATIVE, "mod/S2CoopNative/dlls/main.dll")
    cp(UE4SS_DLL, "ue4ss_bin/UE4SS.dll"); cp(UE4SS_PROXY, "ue4ss_bin/dwmapi.dll")
    cp(UE4SS_LICENSE, "ue4ss_bin/LICENSE-UE4SS.txt")                   # b199rel
    for lbl, p in (("S2CoopNative main.dll (host=client)", HOST_NATIVE), ("UE4SS.dll", UE4SS_DLL), ("dwmapi.dll", UE4SS_PROXY)):
        print("copied %-36s md5 %s  %d bytes" % (lbl, md5(p), os.path.getsize(p)))
    cp(RELAY, "relay/relay.py")
    dev_guard()                                                       # b199rel
    rows = []
    for root, _, files in os.walk(OUT):
        if ".git" in root.replace("\\", "/").split("/"): continue
        for f in sorted(files):
            p = os.path.join(root, f); rel = os.path.relpath(p, OUT).replace("\\", "/")
            if rel == "MANIFEST.md": continue
            rows.append("| `%s` | %d | %s |" % (rel, os.path.getsize(p), md5(p)))
    with open(os.path.join(OUT, "MANIFEST.md"), "w", newline="\n") as fh:
        fh.write("# Manifest\n\nEvery file of this release with its size and md5. Host and client must have identical files.\n\n| File | Bytes | md5 |\n|---|---|---|\n")
        fh.write("\n".join(sorted(rows)) + "\n")
    print("ok", OUT, len(rows), "files")
    print("flag audit (%d entries):" % len(AUDIT))
    for a in AUDIT: print("  " + a)

main()
