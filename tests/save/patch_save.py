import argparse
import hashlib
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE.parent.parent / "src"

SAVE_API = r'''
extern "C" bool s2n_save_api_info(std::string* dir, std::string* stamp, std::string* plat, std::string* newest)
{
    std::wstring d = s2n_save::saved_dir();
    if (d.empty()) return false;
    std::string st;
    std::wstring pl = s2n_save::scan(d, st);
    *dir = s2n_save::narrow(d);
    *stamp = st;
    *plat = s2n_save::narrow(pl);
    *newest = s2n_save::data(d, pl, false);
    return true;
}

extern "C" bool s2n_save_api_list(std::string* out)
{
    std::wstring d = s2n_save::saved_dir();
    std::string st;
    std::wstring pl = d.empty() ? std::wstring() : s2n_save::scan(d, st);
    *out = s2n_save::data(d, pl, true);
    return true;
}
'''

JOIN_API = r'''
extern "C" bool s2n_join_api_host(const std::wstring* br, int kh, double nowS, std::string* out)
{
    try { *out = br->empty() ? std::string("sem bridge") : s2n_join::host_tick(*br, kh, nowS); return true; }
    catch (...) { *out = "excecao"; return false; }
}

extern "C" bool s2n_join_api_get(const std::wstring* br, std::string* state, std::string* id, double* money, std::string* items, std::string* qs, std::string* hand)
{
    try
    {
        s2n_join::Snap s;
        if (!s2n_join::snap_read(*br, s)) { *state = "none"; id->clear(); *money = 0; items->clear(); qs->clear(); hand->clear(); return true; }
        s2n_join::get_text(s, *items);
        qs->clear();
        for (auto& q : s2n_join::quest_set(*br)) { if (!qs->empty()) *qs += ","; *qs += q; }
        *state = s.state;
        *id = s.id;
        *money = s.money;
        *hand = s.hand;
        return true;
    }
    catch (...) { *state = "none"; return false; }
}

extern "C" bool s2n_join_api_mark(const std::wstring* br, const std::string* id, const std::string* st)
{
    try { return s2n_join::mark(*br, *id, *st); }
    catch (...) { return false; }
}

extern "C" bool s2n_join_api_want(const std::wstring* br, const std::string* id)
{
    try
    {
        if (br->empty()) return false;
        if (id->empty()) { DeleteFileW((*br + L"sx_want.txt").c_str()); return true; }
        if (!s2n_join::tok_ok(*id)) return false;
        DeleteFileW((*br + L"sx_in.pkg").c_str());
        return s2n_join::write_atomic(*br + L"sx_want.txt", *id + "\n");
    }
    catch (...) { return false; }
}

extern "C" bool s2n_join_api_in(const std::wstring* br, const std::string* id, std::string* state, std::string* detail)
{
    try { *state = s2n_join::in_state(*br, *id, *detail); return true; }
    catch (...) { *state = "none"; return false; }
}

extern "C" bool s2n_join_api_install(const std::wstring* br, const std::string* id, std::string* guid, int* kh, std::string* cid, std::string* err)
{
    try
    {
        *kh = -1;
        if (!s2n_join::install(*br, *id, *guid, *kh, *cid, *err)) { s2n_join::note("instalacao recusada: " + *err); return false; }
        return true;
    }
    catch (...) { *err = "excecao"; return false; }
}

extern "C" bool s2n_join_api_undo(const std::wstring* br, const std::string* guid)
{
    try { return s2n_join::undo(*br, *guid); }
    catch (...) { return false; }
}

#ifndef S2N_JOIN_TEST
extern "C" bool s2n_join_api_snap(const std::wstring* br, uint64_t addr, const std::string* sids, std::string* id, int* n, double* money, std::string* hand, bool* reused, std::string* err)
{
    try
    {
        s2n_join::Snap old;
        if (s2n_join::snap_read(*br, old) && old.state == "installed")
        {
            int tot = 0;
            for (auto& it : old.items) tot += it.second;
            *id = old.id; *n = tot; *money = old.money; *hand = old.hand; *reused = true;
            return true;
        }
        UObject* pw = s2n_join::live_pawn((double)addr);
        if (!pw) { *err = "pawn invalido"; return false; }
        s2n_join::Snap s;
        std::string e;
        if (!s2n_join::take_snapshot(*br, pw, *sids, s, e)) { s2n_join::note("retrato falhou: " + e); *err = e; return false; }
        int tot = 0;
        for (auto& it : s.items) tot += it.second;
        *id = s.id; *n = tot; *money = s.money; *hand = s.hand; *reused = false;
        return true;
    }
    catch (...) { *err = "excecao"; return false; }
}
#else
extern "C" void s2n_join_api_test_dir(const std::wstring* d) { s2n_join::g_testDir = *d; }
#endif
'''


def strip_comments(src):
    out = []
    i = 0
    n = len(src)
    removed = 0
    while i < n:
        c = src[i]
        if c == 'R' and src.startswith('R"', i) and (i == 0 or not (src[i - 1].isalnum() or src[i - 1] == '_')):
            j = src.index('(', i)
            delim = src[i + 2:j]
            end = src.index(')' + delim + '"', j) + len(delim) + 2
            out.append(src[i:end])
            i = end
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == '\\' else 1
            out.append(src[i:j + 1])
            i = j + 1
        elif src.startswith('//', i):
            j = src.find('\n', i)
            j = n if j < 0 else j
            removed += 1
            i = j
        elif src.startswith('/*', i):
            j = src.index('*/', i + 2) + 2
            removed += 1
            out.append('\n' * src.count('\n', i, j))
            i = j
        else:
            out.append(c)
            i += 1
    text = ''.join(out)
    return '\n'.join(ln.rstrip() for ln in text.split('\n')), removed


def patch(name, api, anchor):
    p = SRC / name
    raw = p.read_bytes()
    crlf = b'\r\n' in raw
    text = raw.decode('utf-8').replace('\r\n', '\n')
    if 'extern "C" bool ' + api.strip().split('extern "C" bool ', 1)[1].split('(', 1)[0] in text:
        return None, 'already patched', 0, hashlib.md5(raw).hexdigest()
    body, removed = strip_comments(text)
    k = body.rfind(anchor)
    if k < 0:
        sys.exit('%s: anchor not found: %s' % (name, anchor))
    body = body[:k] + api.lstrip('\n') + '\n' + body[k:]
    if crlf:
        body = body.replace('\n', '\r\n')
    return body.encode('utf-8'), 'patched', removed, hashlib.md5(raw).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--out', default=str(HERE / 'out' / 'patched'))
    a = ap.parse_args()
    jobs = [('s2n_save.cpp', SAVE_API, '#ifndef S2N_SAVE_TEST\nvoid s2n_save_register'),
            ('s2n_join.cpp', JOIN_API, '#ifndef S2N_JOIN_TEST\nvoid s2n_join_register')]
    outd = Path(a.out)
    outd.mkdir(parents=True, exist_ok=True)
    for name, api, anchor in jobs:
        data, st, removed, md5 = patch(name, api, anchor)
        print('%s md5 %s: %s, %d comments stripped' % (name, md5, st, removed))
        if data is None:
            continue
        target = (SRC / name) if a.apply else (outd / name)
        target.write_bytes(data)
        print('  ->', target)
    return 0


if __name__ == '__main__':
    sys.exit(main())
