import sys
import json


def edit(p, pairs):
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    for a, b in pairs:
        if crlf:
            a = a.replace('\r\n', '\n').replace('\n', '\r\n')
            b = b.replace('\r\n', '\n').replace('\n', '\r\n')
        assert s.count(a) == 1, (p, s.count(a), a)
        s = s.replace(a, b)
    open(p, 'w', encoding='utf-8', newline='').write(s)


if __name__ == '__main__':
    spec = json.load(open(sys.argv[1], encoding='utf-8'))
    for p, pairs in spec.items():
        edit(p, pairs)
