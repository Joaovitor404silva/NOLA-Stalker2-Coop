import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(os.path.dirname(HERE)), "src")

THREADS = {
    "dllmain.cpp": "buildchk message box, detached, never touches mod state",
    "s2m_lshold.cpp": "detached one-shot world splice worker, runs to its end in the old image (old images stay mapped)",
    "s2m_menu.cpp": "detached one-shot menu workers, run to their end in the old image",
    "s2n_join.cpp": "detached one-shot join world worker, runs to its end in the old image",
    "s2r_net.cpp": "relay thread, joined by s2r::x0_stop in hot::shutdown_all",
    "s2r_steam.cpp": "Steam link thread, stopped by s2r::steam_stop_exit in hot::shutdown_all",
}

PATCHES = {
    "s2m_life_x0.cpp": "one byte code patch, not restored on reload (review item 5, queued)",
    "s2r_steam.cpp": "Steam interface vtable slot swap, not restored on reload (review item 5, queued)",
}

fails = 0


def check(ok, what):
    global fails
    print("%s %s" % ("PASS" if ok else "FAIL", what))
    if not ok:
        fails += 1


def files():
    for f in sorted(os.listdir(SRC)):
        if f.endswith((".cpp", ".h", ".inc")):
            yield f, open(os.path.join(SRC, f), encoding="utf-8", errors="replace").read()


def line(t, i):
    return t.count("\n", 0, i) + 1


raw_det, raw_cb, raw_hook, thr, vp, uncovered = [], [], [], {}, {}, {}
for f, t in files():
    if f in ("s2x_hot.cpp", "s2x_hot.h"):
        continue
    for m in re.finditer(r"new\s+PLH::\w+Detour\s*\(", t):
        raw_det.append("%s:%d" % (f, line(t, m.start())))
    for m in re.finditer(r"Hook::Register\w+Callback\s*\(", t):
        if "x0::hot::cb(" not in t[max(0, m.start() - 40):m.start()]:
            raw_cb.append("%s:%d" % (f, line(t, m.start())))
    for m in re.finditer(r"(?<![A-Za-z0-9_])RegisterHook\s*\(", t):
        if "x0::hot::ufn(" not in t[max(0, m.start() - 60):m.start()]:
            raw_hook.append("%s:%d" % (f, line(t, m.start())))
    sites = [line(t, m.start()) for m in re.finditer(r"\b(VirtualProtect(Ex)?|WriteProcessMemory)\s*\(", t)]
    if sites:
        vp[f] = sites
        offs = [line(t, m.start()) for m in re.finditer(r"x0::hot::on_shutdown\(", t)]
        for ln in sites:
            if not any(abs(o - ln) <= 15 for o in offs):
                uncovered.setdefault(f, []).append(ln)
    n = len(re.findall(r"std::thread\s*\(|std::thread\s*\[|CreateThread\s*\(|_beginthreadex\s*\(", t))
    if n:
        thr[f] = n

check(not raw_det, "every PolyHook detour is a tracked x0::hot::Detour (unhooked on reload) %s" % raw_det)
check(not raw_cb, "every UE4SS Hook::Register*Callback id goes through x0::hot::cb (unregistered on reload) %s" % raw_cb)
check(not raw_hook, "every RegisterHook (qualified or bare) goes through x0::hot::ufn (unregistered on reload) %s" % raw_hook)
src_text = dict(files())
bad_vp = sorted("%s:%s" % (f, ",".join(map(str, l))) for f, l in uncovered.items() if f not in PATCHES)
check(not bad_vp, "every raw VirtualProtect/WriteProcessMemory (code or vtable write) call site has x0::hot::on_shutdown within 15 lines or its file is listed (new: %s)" % bad_vp)
for f in sorted(vp):
    how = PATCHES.get(f) or ("restored by x0::hot::on_shutdown" if f not in uncovered else "UNKNOWN")
    print("  patch site %s x%d: %s" % (f, len(vp[f]), how))
unknown = sorted(set(thr) - set(THREADS))
check(not unknown, "every thread site has a known stop path on reload (new: %s)" % unknown)
for f in sorted(thr):
    print("  thread site %s x%d: %s" % (f, thr[f], THREADS.get(f, "UNKNOWN")))
hot = open(os.path.join(SRC, "s2x_hot.cpp"), encoding="utf-8").read()
for need in ("s2r::x0_stop()", "s2r::steam_stop_exit()", "s2m_downview_unload()", "UnregisterCallback", "UnregisterHook", "unHook()"):
    check(need in hot, "hot::shutdown_all calls %s" % need)
cm = open(os.path.join(SRC, "CMakeLists.txt"), encoding="utf-8").read()
blocks = re.findall(r"if\(NOT S2X_RELEASE\)(.*?)endif\(\)", cm, re.S)
check(any("s2x_hot.cpp" in b for b in blocks), "s2x_hot.cpp is compiled only without S2X_RELEASE")
rest = cm
for b in blocks:
    rest = rest.replace(b, "")
check("s2x_hot.cpp" not in rest, "s2x_hot.cpp is not in the release source lists")
dm = open(os.path.join(SRC, "dllmain.cpp"), encoding="utf-8").read()
check(re.search(r"#ifndef S2X_RELEASE\s*\n\s*__declspec\(dllexport\) RC::CppUserModBase\* nola_hot_start", dm) is not None, "nola_hot_start export is dev only")
hh = open(os.path.join(SRC, "s2x_hot.h"), encoding="utf-8").read()
rel = hh.split("#ifdef S2X_RELEASE", 1)[1].split("#else", 1)[0]
check("using Detour = PLH::x64Detour;" in rel and "return m;" in rel, "release: Detour is plain PLH::x64Detour and wrap returns the mod itself")
print("%s hot reload coverage: %d failures" % ("FAIL" if fails else "PASS", fails))
sys.exit(1 if fails else 0)
