import os
import sys


def digit_sep(src, i):
    if i == 0 or i + 1 >= len(src) or not src[i + 1].isalnum():
        return False
    j = i - 1
    while j >= 0 and (src[j] in "0123456789abcdefABCDEFxXbB.'"):
        j -= 1
    start = j + 1
    if start >= i or not src[start].isdigit():
        return False
    return j < 0 or not (src[j].isalnum() or src[j] == '_')


def strip_c(src):
    out = []
    i = 0
    n = len(src)
    st = 0
    while i < n:
        c = src[i]
        if st == 0:
            if c == '/' and i + 1 < n and src[i + 1] == '/':
                j = src.find('\n', i)
                if j < 0:
                    j = n
                while j > i and src[j - 1] == '\\':
                    k = src.find('\n', j + 1)
                    j = n if k < 0 else k
                i = j
                continue
            if c == '/' and i + 1 < n and src[i + 1] == '*':
                j = src.find('*/', i + 2)
                if j < 0:
                    raise ValueError('unterminated block comment')
                body = src[i:j + 2]
                nl = body.count('\n')
                out.append('\n' * nl if nl else ' ')
                i = j + 2
                continue
            if c == 'R' and i + 1 < n and src[i + 1] == '"' and (i == 0 or not (src[i - 1].isalnum() or src[i - 1] == '_') or src[i - 1] in 'uU8L'):
                k = src.find('(', i + 2)
                delim = src[i + 2:k]
                end = src.find(')' + delim + '"', k)
                if end < 0:
                    raise ValueError('unterminated raw string')
                e = end + len(delim) + 2
                out.append(src[i:e])
                i = e
                continue
            if c == '"':
                st = 1
            elif c == "'" and not digit_sep(src, i):
                st = 2
            out.append(c)
            i += 1
            continue
        if c == '\\':
            out.append(src[i:i + 2])
            i += 2
            continue
        if (st == 1 and c == '"') or (st == 2 and c == "'"):
            st = 0
        out.append(c)
        i += 1
    return ''.join(out)


def strip_hash(src):
    lines = []
    for line in src.split('\n'):
        res = []
        st = None
        i = 0
        while i < len(line):
            c = line[i]
            if st:
                if c == '\\':
                    res.append(line[i:i + 2])
                    i += 2
                    continue
                if c == st:
                    st = None
            elif c in '"\'':
                st = c
            elif c == '#':
                break
            res.append(c)
            i += 1
        lines.append(''.join(res))
    return '\n'.join(lines)


def tidy(orig, new):
    ol = orig.split('\n')
    nl = new.split('\n')
    if len(ol) != len(nl):
        raise ValueError('line count changed')
    keep = []
    for a, b in zip(ol, nl):
        cr = b.endswith('\r')
        core = b.rstrip('\r')
        if core.strip() == '' and a.rstrip('\r').strip() != '':
            continue
        s = core.rstrip()
        keep.append(s + ('\r' if cr else ''))
    return '\n'.join(keep)


C_EXT = {'.cpp', '.h', '.hpp', '.inc', '.c', '.cc', '.rc'}


def process(path, check):
    ext = os.path.splitext(path)[1].lower()
    base = os.path.basename(path)
    with open(path, 'rb') as f:
        raw = f.read()
    src = raw.decode('utf-8')
    if ext in C_EXT:
        new = strip_c(src)
    elif ext == '.py' or base == 'CMakeLists.txt' or ext == '.cmake':
        new = strip_hash(src)
    else:
        return False
    new = tidy(src, new)
    if new == src:
        return False
    if not check:
        with open(path, 'wb') as f:
            f.write(new.encode('utf-8'))
    return True


def main():
    check = '--check' in sys.argv
    paths = [a for a in sys.argv[1:] if not a.startswith('--')]
    changed = []
    for p in paths:
        if os.path.isdir(p):
            for root, _, files in os.walk(p):
                for fn in files:
                    fp = os.path.join(root, fn)
                    if process(fp, check):
                        changed.append(fp)
        elif process(p, check):
            changed.append(p)
    for c in changed:
        print(('would strip ' if check else 'stripped ') + c)
    print(len(changed))


if __name__ == '__main__':
    main()
