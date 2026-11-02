import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PROJ = os.path.join(REPO, "cppbuild", "Output", "NolaNative", "NolaNative.vcxproj")
CONFIG = "Game__Shipping__Win64"
OUT = os.path.join(HERE, "out")


def tag(block, name):
    m = re.search(r"<%s>(.*?)</%s>" % (name, name), block, re.S)
    return m.group(1).strip() if m else ""


def main():
    if not os.path.exists(PROJ):
        print("missing %s: build the worktree first (tools/wt.ps1 build)" % PROJ)
        return 2
    text = open(PROJ, encoding="utf-8").read()
    group = None
    for m in re.finditer(r'<ItemDefinitionGroup Condition="[^"]*%s\|x64\'?"[^>]*>(.*?)</ItemDefinitionGroup>' % CONFIG, text, re.S):
        group = m.group(1)
    if group is None:
        for m in re.finditer(r'<ItemDefinitionGroup Condition="([^"]*)">(.*?)</ItemDefinitionGroup>', text, re.S):
            if CONFIG in m.group(1):
                group = m.group(2)
    if group is None:
        print("no %s group in %s" % (CONFIG, PROJ))
        return 2
    cl = re.search(r"<ClCompile>(.*?)</ClCompile>", group, re.S).group(1)
    ln = re.search(r"<Link>(.*?)</Link>", group, re.S).group(1)
    defs = [d for d in tag(cl, "PreprocessorDefinitions").split(";") if d and not d.startswith("%(") and d != "S2X_FR"]
    incs = [i for i in tag(cl, "AdditionalIncludeDirectories").split(";") if i and not i.startswith("%(")]
    opts = tag(cl, "AdditionalOptions").replace("%(AdditionalOptions)", "").strip()
    os.makedirs(OUT, exist_ok=True)
    rsp = []
    for d in defs:
        rsp.append('/D"%s"' % d.replace('\\"', '"').replace('"', '\\"') if '"' in d else "/D%s" % d)
    for i in incs:
        rsp.append('/I"%s"' % i.rstrip(chr(92) + "."))
    rsp += ["/I" + chr(34) + x + chr(34) for x in re.findall(r"/external:I\s+\"([^\"]+)\"", opts)]
    rsp.append(re.sub(r"/external:I\s+\"[^\"]+\"", "", opts))
    rsp += ["/std:c++latest", "/EHsc", "/MD", "/O1", "/bigobj", "/W1", "/nologo", "/wd4005", "/wd4251", "/wd4068", "/wd4201", "/wd4244", "/wd4267", "/wd4996"]
    open(os.path.join(OUT, "cl.rsp"), "w", encoding="utf-8", newline="\n").write("\n".join(rsp) + "\n")
    libdir = os.path.join(REPO, "cppbuild", "Output", CONFIG, "lib")
    libs = []
    for dep in tag(ln, "AdditionalDependencies").split(";"):
        dep = dep.strip()
        if not dep or dep.startswith("%("):
            continue
        base = os.path.basename(dep.replace("\\", "/"))
        if dep.lower().startswith("..\\"):
            libs.append(os.path.join(libdir, base))
        else:
            libs.append(base)
    seen = []
    for l in libs:
        if l not in seen:
            seen.append(l)
    open(os.path.join(OUT, "link.rsp"), "w", encoding="utf-8", newline="\n").write("\n".join('"%s"' % l if os.sep in l else l for l in seen) + "\n")
    print("rsp ok: %d defines, %d includes, %d libs" % (len(defs), len(incs), len(seen)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
