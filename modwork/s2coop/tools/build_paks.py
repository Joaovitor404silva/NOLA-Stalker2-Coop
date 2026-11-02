"""build_paks.py - batch 14 (batch 13 (3) + tracked paksrc): regenerate EVERY co-op pak from the Zone Kit configs + the generators, into one dist folder.
The repo holds no paks (and no game data): this script is how they come back.

    uv run --no-project python -I build_paks.py                      # build into <offline root>/dist
    uv run --no-project python -I build_paks.py --verify-against "<game>/Stalker2/Content/Paks/~mods"
Options: --root <offline root with b8 b9 b10 b11 b12> (default: the parent of this file's folder)   --out <dist folder>
         --kit <...GameLite/GameData>   --repak <repak.exe>   --repo <s2coop folder: Lua, flags, static pak sources>
         --reindex (redo the 2-3 minute WorldMap_WP placeholder scan; otherwise b10/index_all.tsv is reused when present)
What it builds (generator -> pak):
  b8/gen_s2q_b8.py   -> zzzzzz_NolaQuestObserver_HostOnly_P.pak, zzzzzz_NolaQuestReplay_ClientOnly_P.pak
  b9/gen_s2u.py      -> zzzzzz_NolaItemTakeBack_Player_P.pak
  b10/index_all.py   -> b10/index_all.tsv (placeholder index)          b11/gen_mute.py -> zzzzzz_NolaQuestMute_L2_ClientOnly_P.pak
  b12/gen_s2u12.py   -> zzzzzz_NolaItemTakeBack_C1_lean_P.pak + tables/s2u_cont.tsv        b11 -> tables/s2c_skip.txt
  <repo>/paksrc/avatar (our own prototypes, tracked)                       -> zzzzzz_NolaAvatar_P.pak
  b41/gen_b41.py (DifficultyPrototypes derived from the kit, Auto removed)  -> zzzzzz_NolaHostNoAutoTypeSave_P.pak (host; section 59)
    its cfg is md5-checked against the one probed live in P1 (d83e242a); a different kit gives different content: the build stops
    unless --accept-new-content (then probe P1 again)
  <repo>/paksrc/client_nosave (our own {bpatch}, tracked) + AutoSaveVariables.cfg DERIVED here from the kit's stock file
                                                                           -> zzzzzz_NolaClientNoSave_A_empty_P.pak
Also copied into dist: lua/main.lua + s2core.lua and flags/flags_default_<role>.txt from <repo>.
repak does not write multi-file paks byte-identically twice, so manifest.json records a CONTENT hash per pak (sha1 over the sorted
"<path> <md5>" lines of its files) next to the file md5; --verify-against unpacks the paks of another folder and compares content.
Read only on the Zone Kit. Writes only under --root (generator outputs) and --out.
"""
import argparse, hashlib, io, json, os, shutil, subprocess, sys, tempfile, time
HERE = os.path.dirname(os.path.abspath(__file__))
# OLD_KIT is only the prefix the old generators hardcode (BOOT rewrites it to --kit); the real default is default_gamedata() (env S2_ZONEKIT, then D:/modding/zk_keep)
OLD_KIT = "D:/EpicGames/ZoneKit/STALKER2ZoneKit/Stalker2/Content/GameLite/GameData"
MOUNT = "../../../Stalker2/Content/GameLite/GameData/"
PROBED_NOAUTO = "d83e242a"     # md5 (first 8) of b41's DifficultyPrototypes cfg inside the pak 77cb03cf that passed live probe P1 (2026-10-12)
BOOT = r'''
import sys, os, json, importlib
folder, mod, kit, old, over = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], json.loads(sys.argv[5])
sys.path.insert(0, folder); os.chdir(folder)
m = importlib.import_module(mod)
for M in list(sys.modules.values()):
    f = getattr(M, "__file__", None) or ""
    if os.path.dirname(os.path.abspath(f)) == os.path.abspath(folder):
        for k, v in list(vars(M).items()):
            if isinstance(v, str) and v.startswith(old): setattr(M, k, kit + v[len(old):])
for k, v in over.items(): setattr(m, k, v)
m.main() if hasattr(m, "main") else None
'''

def default_gamedata():
    """GameData dir of the kit copy: $S2_ZONEKIT (kit root, or the GameData dir itself), then D:/modding/zk_keep, then the original Zone Kit."""
    rel = "Stalker2/Content/GameLite/GameData"
    cands = [os.environ.get("S2_ZONEKIT"), "D:/modding/zk_keep", "D:/EpicGames/ZoneKit/STALKER2ZoneKit"]
    for c in cands:
        if not c: continue
        c = c.replace("\\", "/").rstrip("/")
        for g in (c, c + "/" + rel):
            if os.path.isdir(g + "/QuestNodePrototypes"): return g
    return cands[1] + "/" + rel


def md5(p):
    h = hashlib.md5()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""): h.update(b)
    return h.hexdigest()
def content_hash(root):
    lines = []
    for r, d, fs in os.walk(root):
        for f in fs:
            p = os.path.join(r, f); lines.append("%s %s" % (os.path.relpath(p, root).replace("\\", "/"), md5(p)))
    return hashlib.sha1("\n".join(sorted(lines)).encode()).hexdigest(), len(lines)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.dirname(HERE)); ap.add_argument("--out", default=None)
    ap.add_argument("--kit", default=default_gamedata()); ap.add_argument("--repak", default="D:/modding/tools/repak/repak.exe")
    ap.add_argument("--repo", default="C:/Program Files (x86)/Steam/steamapps/common/S.T.A.L.K.E.R. 2 Heart of Chornobyl/ModWork/s2coop")
    ap.add_argument("--reindex", action="store_true"); ap.add_argument("--verify-against", default=None)
    ap.add_argument("--accept-new-content", action="store_true", help="build the b41 host pak even when its cfg differs from the one probed in P1")
    a = ap.parse_args()
    root = os.path.abspath(a.root); out = os.path.abspath(a.out or os.path.join(root, "dist"))
    kit = a.kit.replace("\\", "/").rstrip("/")
    for need in ("b8/gen_s2q_b8.py", "b9/gen_s2u.py", "b9/core_base.lua", "b10/index_all.py", "b11/gen_mute.py", "b12/gen_s2u12.py", "b41/gen_b41.py"):
        if not os.path.exists(os.path.join(root, need)): sys.exit("missing generator file: " + os.path.join(root, need))
    if not os.path.isdir(kit + "/QuestNodePrototypes"): sys.exit("Zone Kit GameData not found: " + kit)
    def gen(folder, mod, **over):
        t0 = time.time()
        r = subprocess.run([sys.executable, "-I", "-c", BOOT, os.path.join(root, folder), mod, kit, OLD_KIT, json.dumps(over)], capture_output=True, text=True)
        tail = (r.stdout.strip().splitlines() or [""])[-1][:150]
        print("  gen %-22s %5.0f s  %s" % (folder + "/" + mod, time.time() - t0, "ok  " + tail if r.returncode == 0 else "FAILED"))
        if r.returncode != 0: sys.exit(r.stderr[-1500:])
    print("generators (Zone Kit: %s)" % kit)
    empty = os.path.join(tempfile.mkdtemp(prefix="s2build_"), "empty.tsv"); io.open(empty, "w").close()
    for d in ("b8/pak_host", "b8/pak_client", "b9/pak_player", "b11/pak_mute_L2", "b12/pak_lean"):      # no stale file survives a rebuild
        shutil.rmtree(os.path.join(root, d), ignore_errors=True)
    gen("b8", "gen_s2q_b8")
    near = os.path.join(root, "b5", "near_all.tsv")
    gen("b9", "gen_s2u", NEAR=near if os.path.exists(near) else empty)          # the probe-container pak is not shipped; only pak_player is used
    idx = os.path.join(root, "b10", "index_all.tsv")
    if a.reindex or not os.path.exists(idx): gen("b10", "index_all")
    else: print("  reuse b10/index_all.tsv (%d placeholders); --reindex scans WorldMap_WP again" % (sum(1 for _ in io.open(idx, encoding="utf-8")) - 1))
    # items10.txt = the 636 loot items of b9 + the blueprints (same rule as b10/gen_s2u10.py)
    sys.path.insert(0, os.path.join(root, "b8")); import gen_s2q as G
    items = set(io.open(os.path.join(root, "b9", "items.txt"), encoding="utf-8").read().split())
    for name, head, lines in G.blocks(io.open(kit + "/ItemPrototypes/BlueprintPrototypes.cfg", encoding="utf-8-sig", errors="replace").read()):
        if "emplate" not in name and G.TOKEN.match(name): items.add(name)
    items10 = os.path.join(root, "b10", "items10.txt")
    io.open(items10, "w", encoding="utf-8", newline="\n").write("\n".join(sorted(items)) + "\n")
    gen("b11", "gen_mute")
    gen("b12", "gen_s2u12", INDEX=idx.replace("\\", "/"), ITEMS=items10.replace("\\", "/"))
    # section 59: the host's "no Auto-type save" pak; gen_b41.py has its own CLI and checks (lists, round trip), so it runs as a script
    t0 = time.time()
    r = subprocess.run([sys.executable, "-I", os.path.join(root, "b41", "gen_b41.py"), "--kit", kit, "--repak", a.repak], capture_output=True, text=True)
    print("  gen %-22s %5.0f s  %s" % ("b41/gen_b41", time.time() - t0, "ok  " + (r.stdout.strip().splitlines() or [""])[-1][:150] if r.returncode == 0 else "FAILED"))
    if r.returncode != 0: sys.exit(r.stdout[-1500:] + r.stderr[-1500:])
    noauto = os.path.join(root, "b41", "pak_host_noauto", "DifficultyPrototypes", "zzzzzz_NolaHostNoAutoTypeSave.cfg")
    if md5(noauto)[:8] != PROBED_NOAUTO:
        msg = "b41 cfg md5 %s differs from the cfg probed in P1 (%s): the kit changed" % (md5(noauto)[:8], PROBED_NOAUTO)
        if not a.accept_new_content: sys.exit(msg + "; rerun with --accept-new-content and probe P1 again (OFFLINE_REPORT section 59)")
        print("  WARNING " + msg + " (accepted: probe P1 again)")
    else: print("  b41 cfg md5 %s = the cfg probed in P1" % PROBED_NOAUTO)
    # ---- stage + pack
    shutil.rmtree(out, ignore_errors=True)
    for d in ("paks", "tables", "lua", "flags", "stage"): os.makedirs(os.path.join(out, d))
    def stage(name, srcs):
        st = os.path.join(out, "stage", name)
        for s in srcs:
            for r, d, fs in os.walk(s):
                for f in fs:
                    rel = os.path.relpath(os.path.join(r, f), s)
                    os.makedirs(os.path.dirname(os.path.join(st, rel)), exist_ok=True); shutil.copy2(os.path.join(r, f), os.path.join(st, rel))
        return st
    lean = sorted(os.path.join(root, "b12", "pak_lean", r) for r in os.listdir(os.path.join(root, "b12", "pak_lean")))
    paks = [("zzzzzz_NolaQuestObserver_HostOnly_P", [os.path.join(root, "b8", "pak_host")], "host"),
            ("zzzzzz_NolaQuestReplay_ClientOnly_P", [os.path.join(root, "b8", "pak_client")], "client"),
            ("zzzzzz_NolaQuestMute_L2_ClientOnly_P", [os.path.join(root, "b11", "pak_mute_L2")], "client"),
            ("zzzzzz_NolaItemTakeBack_Player_P", [os.path.join(root, "b9", "pak_player")], "both"),
            ("zzzzzz_NolaItemTakeBack_C1_lean_P", lean, "both"),
            ("zzzzzz_NolaHostNoAutoTypeSave_P", [os.path.join(root, "b41", "pak_host_noauto")], "host")]   # section 59, P1 PASSED 2026-10-12
    # batch 14: our own sources are tracked in <repo>/paksrc; the one stock-derived file is generated from the kit here
    derived = os.path.join(root, "b14", "derived_client_nosave"); shutil.rmtree(derived, ignore_errors=True); os.makedirs(derived)
    stock = io.open(kit + "/AutoSaveVariables.cfg", encoding="utf-8-sig", errors="replace", newline="").read()
    new, nrep = __import__("re").subn(r"(AutoSaveIntervalTime\s*=\s*)[0-9.]+f?", r"\g<1>0", stock)
    if nrep != 1: sys.exit("AutoSaveVariables.cfg: expected exactly one AutoSaveIntervalTime line in the stock file, found %d" % nrep)
    io.open(os.path.join(derived, "AutoSaveVariables.cfg"), "w", encoding="utf-8", newline="").write(new)
    print("  derived AutoSaveVariables.cfg from the kit (AutoSaveIntervalTime -> 0)")
    for name, srcs, role in (("zzzzzz_NolaAvatar_P", [os.path.join(a.repo, "paksrc", "avatar")], "both"),
                             ("zzzzzz_NolaClientAutoSaveOnly_P", [os.path.join(root, "b23", "pak_autosave")], "client")):   # batch 25: csave is v1; made by b23/gen_b23.py; replaces the NoSave pak
        if all(os.path.isdir(p) for p in srcs): paks.append((name, srcs, role))
        else: sys.exit("tracked pak source missing: %s" % srcs)
    man = {"built": time.strftime("%Y-%m-%d %H:%M:%S"), "kit": kit, "paks": {}, "tables": {}, "lua": {}, "flags": {}}
    print("paks -> " + os.path.join(out, "paks"))
    for name, srcs, role in paks:
        st = stage(name, srcs)
        r = subprocess.run([a.repak, "pack", "-q", "--version", "V3", "--mount-point", MOUNT, st], capture_output=True, text=True)
        if r.returncode != 0: sys.exit("repak failed for %s: %s" % (name, r.stderr[-500:]))
        dst = os.path.join(out, "paks", name + ".pak"); shutil.move(st + ".pak", dst)
        ch, n = content_hash(st)
        man["paks"][name + ".pak"] = {"role": role, "files": n, "bytes": os.path.getsize(dst), "md5": md5(dst), "content": ch}
        print("  %-48s %-6s %3d files %9d bytes  content %s" % (name + ".pak", role, n, os.path.getsize(dst), ch[:8]))
    for dst, src in (("s2u_cont.tsv", os.path.join(root, "b12", "s2u_cont_lean.tsv")), ("s2c_skip.txt", os.path.join(root, "b11", "skip_L2.txt"))):
        shutil.copy2(src, os.path.join(out, "tables", dst)); man["tables"][dst] = md5(src)
    for kind, files, sub in (("lua", ("main.lua", "s2core.lua"), "Mods/NolaAlife/Scripts"), ("flags", ("flags_default_host.txt", "flags_default_client.txt"), "")):
        for f in files:
            p = os.path.join(a.repo, sub, f)
            if os.path.exists(p): shutil.copy2(p, os.path.join(out, kind, f)); man[kind][f] = md5(p)
            else: print("  not found, not copied: " + p)
    shutil.rmtree(os.path.join(out, "stage"))
    json.dump(man, io.open(os.path.join(out, "manifest.json"), "w", encoding="utf-8"), indent=1, sort_keys=True)
    print("manifest: " + os.path.join(out, "manifest.json"))
    if a.verify_against:
        print("verify against " + a.verify_against)
        tmp = tempfile.mkdtemp(prefix="s2verify_")
        for name, info in sorted(man["paks"].items()):
            other = os.path.join(a.verify_against, name)
            if not os.path.exists(other): print("  %-48s not there" % name); continue
            u = os.path.join(tmp, name)
            subprocess.run([a.repak, "unpack", "-q", "-o", u, other], capture_output=True)
            inner = os.path.join(u, "Stalker2", "Content", "GameLite", "GameData")
            ch, n = content_hash(inner if os.path.isdir(inner) else u)
            print("  %-48s %s (%d files there)" % (name, "SAME CONTENT" if ch == info["content"] else "DIFFERENT", n))
        shutil.rmtree(tmp, ignore_errors=True)

if __name__ == "__main__":
    main()
