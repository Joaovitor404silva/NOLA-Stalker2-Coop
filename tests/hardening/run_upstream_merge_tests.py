"""Portable upstream v1.3.2 regression checks retained by the custom merge."""
from pathlib import Path
import subprocess, tempfile
R=Path(__file__).resolve().parents[2]
def run(args, **kw):
    return subprocess.run([str(a) for a in args], check=True, **kw)
with tempfile.TemporaryDirectory() as d:
    t=Path(d)
    for name, sources in [('lockbehind', ['tests/nat/test_nat_lockbehind.cpp']),
                          ('text', ['tests/text/test_text.cpp']),
                          ('world', ['tests/m5/test_world.cpp', 'src/s2m_world.cpp'])]:
        run(['g++', '-std=c++20', '-O1', '-I'+str(R/'src'), *[R/s for s in sources], '-o', t/name])
        if name != 'world': run([t/name])
    (t/'flags.txt').write_text('')
    run([t/'world', 'run', R/'tests/m5/scenarios/lock_wire_defuse.scn', t/'flags.txt', R/'data'])
    run(['python3', R/'tests/lockbehind/test_gen_lockbehind.py'])
    run(['python3', R/'tools/gen_texts.py', '--check'])
