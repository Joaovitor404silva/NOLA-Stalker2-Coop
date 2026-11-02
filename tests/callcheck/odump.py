import re,sys,os,pickle
DUMP=r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\Stalker2\Binaries\Win64\ue4ss\UE4SS_ObjectDump.txt"
CACHE=os.path.join(os.path.dirname(os.path.abspath(__file__)),"odump.pkl")
line_re=re.compile(r"^\[([0-9A-F]+)\] (\w+) (\S+)(.*)$")
def load():
    if os.path.exists(CACHE) and os.path.getmtime(CACHE)>os.path.getmtime(DUMP):
        return pickle.load(open(CACHE,"rb"))
    funcs={}
    classes={}
    structs={}
    props={}
    byaddr={}
    enums={}
    cur=None
    with open(DUMP,encoding="utf-8",errors="replace") as f:
        for ln in f:
            m=line_re.match(ln)
            if not m:
                m2=re.match(r"^\[[0-9A-F]+\] (E\w+::\w+) \[n: [0-9A-F]+\] \[v: (-?\d+)\]",ln)
                if m2 and cur and cur[0]=="Enum":
                    enums.setdefault(cur[1],[]).append((m2.group(1),int(m2.group(2))))
                continue
            addr,kind,path,rest=m.groups()
            if kind in("Function","DelegateFunction","SignatureFunction"):
                funcs[path]={"kind":kind,"params":[],"addr":addr}
                cur=("Function",path)
            elif kind=="Class" or kind.endswith("GeneratedClass"):
                sp=re.search(r"\[sps: ([0-9A-F]+)\]",rest);classes[path]={"addr":addr,"props":[],"sps":sp.group(1) if sp else None}
                byaddr[addr]=path
                cur=("Class",path)
            elif kind in("ScriptStruct","UserDefinedStruct"):
                structs[path]={"addr":addr,"props":[],"sps":(re.search(r"\[sps: ([0-9A-F]+)\]",rest).group(1) if re.search(r"\[sps: ([0-9A-F]+)\]",rest) else None)}
                byaddr[addr]=path
                cur=("Struct",path)
            elif kind=="Enum":
                enums[path]=[]
                byaddr[addr]=path
                cur=("Enum",path)
            elif kind.endswith("Property"):
                o=re.search(r"\[o: ([0-9A-F]+)\]",rest)
                ss=re.search(r"\[ss: ([0-9A-F]+)\]",rest)
                em=re.search(r"\[em: ([0-9A-F]+)\]",rest)
                pc=re.search(r"\[pc: ([0-9A-F]+)\]",rest)
                off=int(o.group(1),16) if o else None
                owner,_,name=path.rpartition(":")
                if owner in funcs:
                    funcs[owner]["params"].append((name,kind,off,ss.group(1) if ss else (em.group(1) if em else (pc.group(1) if pc else None))))
                elif owner in classes:
                    classes[owner]["props"].append((name,kind,off,ss.group(1) if ss else (em.group(1) if em else (pc.group(1) if pc else None))))
                elif owner in structs:
                    structs[owner]["props"].append((name,kind,off,ss.group(1) if ss else (em.group(1) if em else (pc.group(1) if pc else None))))
                else:
                    pass
            else:
                cur=None
    d={"funcs":funcs,"classes":classes,"structs":structs,"byaddr":byaddr,"enums":enums}
    pickle.dump(d,open(CACHE,"wb"))
    return d
if __name__=="__main__":
    d=load()
    print(len(d["funcs"]),len(d["classes"]),len(d["structs"]),len(d["enums"]))
