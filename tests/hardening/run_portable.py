"""Compile actual platform-independent code slices; not a Windows DLL/build test.
Native Unreal relation hooks are stubbed ONLY in the temporary damage test TU.
"""
from pathlib import Path
import subprocess, tempfile
R=Path(__file__).resolve().parents[2]
def run(args,**kw): return subprocess.run([str(a) for a in args],check=True,**kw)
with tempfile.TemporaryDirectory() as td:
    t=Path(td);s=(R/'src/s2r_net.cpp').read_text()
    # Keep production codec/sender/assembler bodies verbatim; omit Windows IO.
    inc=s[s.index('#include <algorithm>'):s.index('#pragma comment')]
    fmt=s[s.index('    static std::string fmt'):s.index('    double clock_s()')]
    codec=s[s.index('    namespace js'):s.index('    namespace fs')]
    helpers=s[s.index('    static bool py_space'):s.index('    static bool read_text_strip')]+s[s.index('    static bool ascii_digits'):s.index('    namespace proto')]
    protocol=s[s.index('    namespace proto'):s.index('        struct SxError')]
    (t/'net.cpp').write_text('#include "s2r_net.h"\n'+inc+'\nnamespace s2r {\n'+fmt+codec+helpers+protocol+'\n}}\n')
    run(['g++','-std=c++20','-O1','-g','-fsanitize=address,undefined','-I'+str(R/'src'),t/'net.cpp',R/'tests/hardening/test_net.cpp','-o',t/'net'])
    run([t/'net'])
    s=(R/'src/s2m_damage.cpp').read_text().replace('#include "s2n_rel_core.inc"','namespace s2rel { inline int get(uint32_t,uint32_t,int32_t&,int32_t&) {return -1;} inline int set(uint32_t,uint32_t,int32_t) {return -1;} }')
    (t/'damage.cpp').write_text(s)
    run(['g++','-std=c++20','-O1','-g','-fsanitize=address,undefined','-I'+str(R/'src'),t/'damage.cpp',R/'tests/damage/test_damage.cpp','-o',t/'damage'])
    for path in sorted((R/'tests/hardening').glob('*.txt')):
        result=run([t/'damage',R/'data',path],capture_output=True,text=True).stdout
        (t/(path.stem+'.log')).write_text(result)
        print(path.name+'\n'+result)
    run(['python3',R/'tests/hardening/check_damage.py',t])
