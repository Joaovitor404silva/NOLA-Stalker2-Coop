import sys,os;sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from odump import load
d=load()
def chain(path):
    out=[]
    while path:
        out.append(path)
        c=d["classes"].get(path)
        if not c: break
        path=d["byaddr"].get(c["sps"]) if c["sps"] else None
    return out
if __name__=="__main__":
    for a in sys.argv[1:]:
        ps=[p for p in d["classes"] if p.rsplit(".",1)[-1]==a]
        for p in ps: print(" > ".join(x.rsplit(".",1)[-1] for x in chain(p)))
