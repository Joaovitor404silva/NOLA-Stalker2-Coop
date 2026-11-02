import sys
import lupa

HERE = 'D:/modding/cpp/S2CoopPort/'
SRC = HERE + 'tests/orphans/s2core_0b56e1b5.lua'
OUT = HERE + 'data/s2u_wshot.tsv'


def esc(s):
    return s.replace('\\', '\\\\').replace('\t', '\\t').replace('\r', '\\r').replace('\n', '\\n')


def val(v):
    if isinstance(v, bool):
        return 'b', 'true' if v else 'false'
    if isinstance(v, int):
        return 'i', str(v)
    if isinstance(v, float):
        r = repr(v)
        if '.' not in r and 'e' not in r and 'inf' not in r and 'nan' not in r:
            r += '.0'
        return 'f', r
    return 's', esc(str(v))


def key(k):
    return ('i' + str(k)) if isinstance(k, int) else ('s' + esc(str(k)))


def walk(L, t, path, rows):
    if lupa.lua_type(t) == 'table':
        ks = list(t.keys())
        if not ks:
            rows.append((path, 'e', ''))
        for k in ks:
            walk(L, t[k], path + [key(k)], rows)
    else:
        ty, v = val(t)
        rows.append((path, ty, v))


def build():
    lines = open(SRC, encoding='utf-8').read().split('\n')
    chunk = '\n'.join(lines[13673:13716])
    L = lupa.LuaRuntime(unpack_returned_tuples=True)
    L.execute('S2L = {}\n' + chunk)
    S = L.globals().S2L
    rows = []
    for name in ('WSHOT', 'WSFAM', 'WSMZ', 'WSPFX'):
        walk(L, S[name], [key(name)], rows)
    rows.sort(key=lambda r: r[0])
    depth = 3
    out = ['k1\tk2\tk3\tt\tv']
    for p, ty, v in rows:
        cells = p + [''] * (depth - len(p))
        out.append('\t'.join(cells + [ty, v]))
    return '\n'.join(out) + '\n'


def main():
    text = build()
    if '--check' in sys.argv:
        cur = open(OUT, encoding='utf-8', newline='').read()
        if cur != text:
            print('gen_wshot_tsv: FAIL s2u_wshot.tsv differs from the Lua table')
            return 1
        print('gen_wshot_tsv: OK %d rows' % (text.count('\n') - 1))
        return 0
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print('gen_wshot_tsv: wrote %d rows' % (text.count('\n') - 1))
    return 0


sys.exit(main())
