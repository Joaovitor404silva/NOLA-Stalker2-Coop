import argparse
import json
import os

ROOT = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(ROOT, ".."))
OUT = os.path.join(PORT, "data", "s2u_lockbrk.tsv")
HARMFUL = {"BP_Trap_Grenade_C", "BP_Trap_Shotgun_C"}
WIRE_DEFUSE = "BndEvt__BP_trap_wire_SignalReceiver_Deactivation_K2Node_ComponentBoundEvent_4_SignalReceiverComponentReceiveSignature__DelegateSignature"
FN_OVERRIDE = {"BP_Wire_C": WIRE_DEFUSE}


def strip(s):
    s = s.split("'", 1)[1].rstrip("'") if "'" in s else s
    return s.replace(":", ".").rsplit(".", 1)[-1]


def walk(d):
    for dp, _, fs in os.walk(d):
        for f in sorted(fs):
            if f.endswith(".json"):
                yield os.path.join(dp, f)


def scan(d):
    sup, bind = {}, {}
    for p in walk(d):
        try:
            data = json.load(open(p, encoding="utf-8"))
        except (OSError, ValueError):
            continue
        for o in data if isinstance(data, list) else []:
            if o.get("Type") == "BlueprintGeneratedClass":
                s = o.get("SuperStruct") or o.get("Super")
                sup[o["Name"]] = strip(s["ObjectName"]) if s else ""
            if o.get("Type") == "ComponentDelegateBinding":
                c = strip(o["Outer"]["ObjectName"])
                for b in (o.get("Properties") or {}).get("ComponentDelegateBindings") or []:
                    bind.setdefault(c, []).append(b)
    return sup, bind


def chain(sup, c):
    out = []
    while c and c not in out:
        out.append(c)
        c = sup.get(c, "")
    return out


def rows(sup, bind):
    out = []
    for c in sorted(sup):
        ch = chain(sup, c)
        if any(x in HARMFUL for x in ch):
            continue
        fn = FN_OVERRIDE.get(c) or next((b.get("FunctionNameToBind", "") for k in ch for b in bind.get(k, []) if b.get("DelegatePropertyName") == "OnDamageAccumulated"), "")
        if fn:
            out.append((c, "brk", fn))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exports", help="folder of s2assets exportmatch JSON files (InteractiveObjects blueprints)")
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args()
    sup, bind = scan(a.exports)
    rs = rows(sup, bind)
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("k1\tk2\tt\tv\n")
        for c, field, fn in rs:
            f.write("s%s\ts%s\ts\t%s\n" % (c, field, fn))
    print("%d rows, %d classes -> %s" % (len(rs), len({r[0] for r in rs}), a.out))


if __name__ == "__main__":
    main()
