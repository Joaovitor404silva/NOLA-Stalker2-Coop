import sys,os;sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import sys,re
from odump import load
d=load()
pat=re.compile(sys.argv[1],re.I)
for p,f in d["funcs"].items():
    if pat.search(p):
        out=[]
        for n,k,o,x in f["params"]:
            extra=""
            if x and x in d["byaddr"]: extra="<"+d["byaddr"][x].rsplit(".",1)[-1]+">"
            out.append(f"{n}:{k[:-8]}{extra}@{o:x}" if o is not None else f"{n}:{k}")
        print(p,"(",", ".join(out),")")
