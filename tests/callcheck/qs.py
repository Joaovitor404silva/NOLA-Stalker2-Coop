import sys,os;sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import re
from odump import load
d=load()
pat=re.compile(sys.argv[1],re.I)
for p,s in d["structs"].items():
    if pat.search(p):
        print(p)
        for n,k,o,x in s["props"]: print("   ",n,k,hex(o) if o is not None else None, d["byaddr"].get(x,"") if x else "")
for p,s in d["classes"].items():
    if len(sys.argv)>2 and re.search(sys.argv[2],p,re.I):
        print(p)
        for n,k,o,x in s["props"]: print("   ",n,k,hex(o) if o is not None else None, d["byaddr"].get(x,"") if x else "")
for p,s in d["enums"].items():
    if len(sys.argv)>3 and re.search(sys.argv[3],p,re.I):
        print(p,s)
