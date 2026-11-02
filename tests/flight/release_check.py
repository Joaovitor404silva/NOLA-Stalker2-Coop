import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
SRC = REPO / "src"
CB = REPO / "cppbuild"
REL = CB / "OutputRel" / "NolaNative" / "Game__Shipping__Win64" / "NolaNative.dll"
DEV = CB / "Output" / "NolaNative" / "Game__Shipping__Win64" / "NolaNative.dll"
BUILDER = Path(r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\ModWork\s2coop\tools\build_release_cpp.py")
fails = []
passes = []


def check(ok, name, detail=""):
    (passes if ok else fails).append(name)
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else " " + str(detail)))


def newest_src():
    t = 0.0
    for p in list(SRC.rglob("*.cpp")) + list(SRC.rglob("*.h")) + list(SRC.rglob("*.inc")) + list(SRC.rglob("CMakeLists.txt")):
        t = max(t, p.stat().st_mtime)
    return t


cm = (SRC / "CMakeLists.txt").read_text(encoding="utf-8")
m = re.search(r'if\(NOT S2X_RELEASE\)\s*list\(APPEND S2X_SOURCES "s2x_fr\.cpp"\)\s*endif\(\)', cm)
check(bool(m), "CMake adds s2x_fr.cpp only when S2X_RELEASE is off")
check(not re.search(r'^\s*"?s2x_fr\.cpp"?\s*$', cm.replace(m.group(0), "") if m else cm, re.M), "s2x_fr.cpp is not in the always-built source list")
defs = re.findall(r"S2X_FR\b", cm)
check(len(defs) == 1, "S2X_FR is defined in one place (the non-release branch)", len(defs))
fr = (SRC / "s2x_fr.cpp").read_text(encoding="utf-8").lstrip()
check(fr.startswith("#if defined(S2X_FR) && !defined(S2X_RELEASE)") and fr.rstrip().endswith("#endif"), "s2x_fr.cpp body is guarded by S2X_FR && !S2X_RELEASE")
hd = (SRC / "s2x_fr.h").read_text(encoding="utf-8")
check("#define S2FR(x) ((void)0)" in hd and "inline void fr_trigger(std::string_view) {}" in hd, "release macro S2FR and fr_trigger are no-ops")
users = [p.name for p in SRC.glob("*.cpp") if p.name != "s2x_fr.cpp" and re.search(r"\bfr::(?!.*S2FR)", "\n".join(l for l in p.read_text(encoding="utf-8", errors="replace").splitlines() if "fr::" in l and "S2FR(" not in l))]
check(not users, "every recorder call outside s2x_fr.cpp goes through S2FR(...)", users)

if not BUILDER.exists():
    check(False, "release builder present", BUILDER)
else:
    if not REL.exists() or REL.stat().st_mtime < newest_src():
        r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(REPO / "tools" / "build_rel.ps1")], capture_output=True, text=True, stdin=subprocess.DEVNULL)
        print(r.stdout[-800:])
        check(r.returncode == 0 and REL.exists(), "release DLL builds with -DS2X_RELEASE=ON", r.stdout[-400:] + r.stderr[-400:])
    if REL.exists():
        r = subprocess.run([sys.executable, str(BUILDER), "--check-recorder", str(REL)], capture_output=True, text=True)
        print(r.stdout.strip())
        check(r.returncode == 0, "release DLL: build_release_cpp.py --check-recorder passes", r.stdout[-400:])
    if DEV.exists():
        r = subprocess.run([sys.executable, str(BUILDER), "--check-recorder", str(DEV)], capture_output=True, text=True)
        check(r.returncode == 1 and "FAIL" in r.stdout, "debug DLL: the same check finds the recorder (the check can fail)", r.stdout[-400:])
    else:
        print("SKIP debug DLL not built, negative control skipped")

print("%d passed, %d failed" % (len(passes), len(fails)))
sys.exit(1 if fails else 0)
