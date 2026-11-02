import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(REPO, "src")
fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        fails += 1


r = subprocess.run([sys.executable, "-I", os.path.join(REPO, "tools", "gen_texts.py"), "--check"], capture_output=True, text=True)
check(r.returncode == 0, (r.stdout + r.stderr).strip().splitlines()[-1] if (r.stdout + r.stderr).strip() else "gen_texts --check")

LIT = re.compile(r'"(?:[^"\\n]|\.)*"')
SINKS = [
    (re.compile(r"\bscreen\(\s*(?:[^,()]|\(\))+,\s*(?:\w+\()?\""), "screen(id, \"literal\")"),
    (re.compile(r"\bscreen\([^;]*\?\s*\""), "screen(id, cond ? \"literal\")"),
    (re.compile(r"\bui_msg\(\s*[^,()]+,\s*\""), "ui_msg(id, \"literal\")"),
    (re.compile(r"\bpopup\(\s*\"|\bpopup\([^,()]+,\s*\""), "popup(\"literal\")"),
]
SKIP = {"s2m_text_tab.inc", "s2m_text.h", "s2m_text.cpp"}
sinkfiles = ["s2m_session.cpp", "s2m_save.cpp", "s2m_quest.cpp", "s2m_menu.cpp", "s2m_session_x0.cpp", "s2m_save_x0.cpp", "s2r_x0.cpp", "s2m_steam_logic.h", "s2m_menu_logic.h", "s2x_ui.cpp"]
for fn in sinkfiles:
    p = os.path.join(SRC, fn)
    txt = open(p, encoding="utf-8", errors="replace").read()
    hits = []
    for n, line in enumerate(txt.splitlines(), 1):
        for rx, name in SINKS:
            if rx.search(line):
                hits.append("%s:%d %s" % (fn, n, name))
    check(not hits, "no hard-coded player text in " + fn + (": " + ", ".join(hits) if hits else ""))

legacy = []
for fn in os.listdir(SRC):
    if not fn.endswith((".cpp", ".h", ".inc")) or fn in SKIP:
        continue
    txt = open(os.path.join(SRC, fn), encoding="utf-8", errors="replace").read()
    for pat in ("nola::Texts", "nola::texts(", "nola_steam::Texts", "nola_steam::texts(", "lang_pt(", "virtual bool pt()"):
        if pat in txt:
            legacy.append(fn + " " + pat)
check(not legacy, "no duplicate text table or language probe left" + (": " + ", ".join(legacy) if legacy else ""))

print("%d failed" % fails)
sys.exit(1 if fails else 0)
