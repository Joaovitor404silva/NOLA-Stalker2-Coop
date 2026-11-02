"""install_coop.py - batch 13 (3): install the co-op mod for ONE role from a dist folder made by build_paks.py.
DRY-RUN BY DEFAULT: it only prints the plan. Nothing is copied, moved or written without --apply.

    python install_coop.py --role host                      # show what would be done on the host PC
    python install_coop.py --role client --game "D:\\Games\\S.T.A.L.K.E.R. 2 Heart of Chornobyl"
    python install_coop.py --role host --apply              # do it (backup first)        python install_coop.py --undo latest [--apply]
Options: --repak <repak.exe, optional: lets it see that an installed pak has the same content>  --dist <folder>  --game <game root>  --bridge <bridge folder>  --scripts <NolaAlife/Scripts folder>  --flags (overwrite flags.txt
         with the role's default set; otherwise flags.txt is only written when it does not exist)  --backup-root <folder>
What goes where:
  lua/main.lua, lua/s2core.lua  -> the Scripts folder (host: <game>/ModWork/s2coop/Mods/NolaAlife/Scripts when UE4SS-settings.ini points its
                                   ModsFolderPath there, else <game>/Stalker2/Binaries/Win64/ue4ss/Mods/NolaAlife/Scripts; client: the latter)
  paks of the role              -> <game>/Stalker2/Content/Paks/~mods      (paks of the OTHER role and old builds of ours found there are moved out)
  tables                        -> bridge: s2u_cont.tsv (both), s2c_skip.txt (client)
  role.txt, flags.txt           -> bridge (host: <game>/ModWork/s2coop/bridge, client: C:/nola_bridge)
  mods.txt                      -> "NolaAlife : 1" is added when the Mods folder's mods.txt lacks it
Backup + undo: every file that is replaced or moved out is first copied to <backup-root>/<timestamp>/files/..., and manifest.json there lists
each action; --undo puts replaced files back and deletes the ones this install created. Saves are never touched. It never starts or stops the game.
"""
import argparse, hashlib, io, json, os, shutil, sys, time
ROLE_PAKS = {"host": ("host", "both"), "client": ("client", "both")}
OURS = "zzzzzz_Nola"
# paks of ours that this script does not build and leaves alone (test rigs chosen by hand)
UNMANAGED = ("zzzzzz_NolaRig", "zzzzzz_NolaGod", "zzzzzz_NolaClientNoAutosave", "zzzzzz_NolaClientNoSave_B")
def other_role(name, role):
    """batch 25: a pak whose NAME marks it for the other role (zzzzzz_NolaClient*, *_ClientOnly_*, *_HostOnly_*)"""
    client = name.startswith("zzzzzz_NolaClient") or "ClientOnly" in name
    return (role == "host" and client) or (role == "client" and "HostOnly" in name)

def md5(p):
    h = hashlib.md5()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""): h.update(b)
    return h.hexdigest()

def same_pak(a, man, src, dst):
    """repak does not write a multi-file pak byte-identically twice: compare the CONTENT of an installed pak with the manifest"""
    info = man["paks"].get(os.path.basename(src))
    if not (info and dst.endswith(".pak") and os.path.exists(a.repak) and os.path.getsize(dst) == info["bytes"]): return False
    import subprocess, tempfile
    tmp = tempfile.mkdtemp(prefix="s2inst_")
    try:
        subprocess.run([a.repak, "unpack", "-q", "-o", os.path.join(tmp, "u"), dst], capture_output=True)
        root = os.path.join(tmp, "u", "Stalker2", "Content", "GameLite", "GameData")
        lines = []
        for r, d, fs in os.walk(root):
            for f in fs:
                q = os.path.join(r, f); lines.append("%s %s" % (os.path.relpath(q, root).replace(chr(92), "/"), md5(q)))
        return hashlib.sha1(chr(10).join(sorted(lines)).encode()).hexdigest() == info["content"]
    finally: shutil.rmtree(tmp, ignore_errors=True)

def plan(a):
    game = os.path.abspath(a.game); dist = os.path.abspath(a.dist)
    man = json.load(io.open(os.path.join(dist, "manifest.json"), encoding="utf-8"))
    ue = os.path.join(game, "Stalker2", "Binaries", "Win64", "ue4ss")
    scripts = a.scripts
    if not scripts:
        scripts = os.path.join(ue, "Mods", "NolaAlife", "Scripts")
        ini = os.path.join(ue, "UE4SS-settings.ini")
        if a.role == "host" and os.path.exists(ini):
            for ln in io.open(ini, encoding="utf-8", errors="replace"):
                if ln.strip().startswith("ModsFolderPath") and "=" in ln and ln.split("=", 1)[1].strip():
                    scripts = os.path.join(ln.split("=", 1)[1].strip(), "NolaAlife", "Scripts")
    bridge = a.bridge or (os.path.join(game, "ModWork", "s2coop", "bridge") if a.role == "host" else "C:/nola_bridge")
    mods = os.path.join(game, "Stalker2", "Content", "Paks", "~mods")
    acts, notes = [], []
    def copy(src, dst):
        if not os.path.exists(src): notes.append("MISSING in dist, skipped: " + src); return
        if os.path.exists(dst) and (md5(src) == md5(dst) or same_pak(a, man, src, dst)): acts.append(("same", src, dst))
        else: acts.append(("replace" if os.path.exists(dst) else "create", src, dst))
    for f in ("main.lua", "s2core.lua"): copy(os.path.join(dist, "lua", f), os.path.join(scripts, f))
    want = set()
    for name, info in sorted(man["paks"].items()):
        if other_role(name, a.role) and info["role"] in ROLE_PAKS[a.role]:      # batch 25: the name says the other role, whatever the manifest says
            sys.exit("REFUSED: %s is a %s-only pak but the manifest gives it role '%s'; it must never go to a %s. Fix the dist." % (name, "client" if a.role == "host" else "host", info["role"], a.role))
        if info["role"] in ROLE_PAKS[a.role]: want.add(name); copy(os.path.join(dist, "paks", name), os.path.join(mods, name))
    if os.path.isdir(mods):
        for f in sorted(os.listdir(mods)):
            if f.startswith(OURS) and f.endswith(".pak") and f not in want:
                if other_role(f, a.role):                # batch 25: e.g. a client save-limiting pak on the host would stop the host from saving
                    acts.append(("remove", None, os.path.join(mods, f))); notes.append("WARNING: %s is a pak of the OTHER role and was found in this %s's ~mods: it is moved out" % (f, a.role))
                elif f.startswith(UNMANAGED): notes.append("left alone (not managed by this script): " + f)
                else: acts.append(("remove", None, os.path.join(mods, f)))
    copy(os.path.join(dist, "tables", "s2u_cont.tsv"), os.path.join(bridge, "s2u_cont.tsv"))
    if a.role == "client": copy(os.path.join(dist, "tables", "s2c_skip.txt"), os.path.join(bridge, "s2c_skip.txt"))
    rolef = os.path.join(bridge, "role.txt")
    cur = io.open(rolef).read().strip() if os.path.exists(rolef) else None
    acts.append(("same" if cur == a.role else ("replace" if cur is not None else "create"), "text:" + a.role, rolef))
    flagsf, src = os.path.join(bridge, "flags.txt"), os.path.join(dist, "flags", "flags_default_%s.txt" % a.role)
    if a.flags or not os.path.exists(flagsf): copy(src, flagsf)
    else: notes.append("flags.txt kept as it is (use --flags to overwrite it with flags_default_%s.txt)" % a.role)
    mt = os.path.join(os.path.dirname(os.path.dirname(scripts)), "mods.txt")
    txt = io.open(mt, encoding="utf-8", errors="replace").read() if os.path.exists(mt) else ""
    if "NolaAlife" not in txt: acts.append(("replace" if txt else "create", "text:" + (txt.rstrip("\n") + "\n" if txt else "") + "NolaAlife : 1\n", mt))
    else: notes.append("mods.txt already lists NolaAlife")
    if not os.path.isdir(ue): notes.append("WARNING: UE4SS folder not found: " + ue)
    return acts, notes, dict(game=game, dist=dist, scripts=scripts, bridge=bridge, mods=mods, lua=man.get("lua"), built=man.get("built"))

def show(acts, notes, info, apply):
    print("%s for role %s" % ("INSTALL" if apply else "DRY RUN (nothing is changed; add --apply to install)", info["role"]))
    for k in ("dist", "game", "scripts", "mods", "bridge"): print("  %-8s %s" % (k, info[k]))
    print("  dist built %s" % info["built"])
    for kind, src, dst in acts:
        print("  %-8s %s%s" % (kind.upper(), dst, "" if kind in ("remove", "same") or src.startswith("text:") else "   <-  " + os.path.relpath(src, info["dist"])))
    for n in notes: print("  note: " + n)
    c = {k: sum(1 for x in acts if x[0] == k) for k in ("create", "replace", "remove", "same")}
    print("  total: %(create)d to create, %(replace)d to replace, %(remove)d to move out, %(same)d already identical" % c)

def do(acts, info, broot):
    stamp = time.strftime("%Y%m%d_%H%M%S"); bdir = os.path.join(broot, stamp)
    os.makedirs(os.path.join(bdir, "files"))
    log = []
    json.dump({"info": info, "actions": log}, io.open(os.path.join(bdir, "manifest.json"), "w", encoding="utf-8"), indent=1)
    for i, (kind, src, dst) in enumerate(acts):
        if kind == "same": continue
        bak = None
        if os.path.exists(dst):
            bak = os.path.join(bdir, "files", "%03d_%s" % (i, os.path.basename(dst))); shutil.copy2(dst, bak)
        if kind == "remove": os.remove(dst)
        else:
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if src.startswith("text:"): io.open(dst, "w", encoding="utf-8", newline="\n").write(src[5:])
            else:
                tmp = dst + ".s2tmp"; shutil.copy2(src, tmp); os.replace(tmp, dst)
        log.append({"kind": kind, "dst": dst, "backup": bak})
        json.dump({"info": info, "actions": log}, io.open(os.path.join(bdir, "manifest.json"), "w", encoding="utf-8"), indent=1)   # after every step: undo works after a crash
    print("done. backup + undo data: " + bdir)

def undo(a):
    broot = os.path.abspath(a.backup_root)
    which = a.undo
    if which == "latest":
        ds = sorted(d for d in os.listdir(broot) if os.path.exists(os.path.join(broot, d, "manifest.json"))) if os.path.isdir(broot) else []
        if not ds: sys.exit("no backup found under " + broot)
        which = os.path.join(broot, ds[-1])
    man = json.load(io.open(os.path.join(which, "manifest.json"), encoding="utf-8"))
    print("%s of %s (role %s)" % ("UNDO" if a.apply else "DRY RUN undo (add --apply)", which, man["info"].get("role")))
    for x in reversed(man["actions"]):
        if x["backup"]:
            print("  RESTORE  " + x["dst"])
            if a.apply: os.makedirs(os.path.dirname(x["dst"]), exist_ok=True); shutil.copy2(x["backup"], x["dst"])
        else:
            print("  DELETE   " + x["dst"] + "   (created by the install)")
            if a.apply and os.path.exists(x["dst"]): os.remove(x["dst"])
    if a.apply: os.rename(os.path.join(which, "manifest.json"), os.path.join(which, "manifest.undone.json")); print("undone.")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--role", choices=("host", "client")); ap.add_argument("--apply", action="store_true"); ap.add_argument("--undo", default=None)
    ap.add_argument("--dist", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "dist"))
    ap.add_argument("--game", default="C:/Program Files (x86)/Steam/steamapps/common/S.T.A.L.K.E.R. 2 Heart of Chornobyl")
    ap.add_argument("--bridge", default=None); ap.add_argument("--scripts", default=None); ap.add_argument("--flags", action="store_true")
    ap.add_argument("--backup-root", default=None); ap.add_argument("--repak", default="D:/modding/tools/repak/repak.exe")
    a = ap.parse_args()
    a.backup_root = a.backup_root or os.path.join(os.path.abspath(a.game), "ModWork", "nola_install_backup")
    if a.undo: return undo(a)
    if not a.role: sys.exit("--role host or --role client is required")
    acts, notes, info = plan(a); info["role"] = a.role
    show(acts, notes, info, a.apply)
    if a.apply: do(acts, info, a.backup_root)

if __name__ == "__main__":
    main()
