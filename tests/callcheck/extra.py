import os, re, csv, collections

SRC = None


def srcline(g, base, rx, nth=0):
    path = os.path.join(g.SRC, base)
    if not os.path.exists(path):
        return 0
    k = 0
    for i, ln in enumerate(open(path, encoding="utf-8", errors="replace").read().split("\n")):
        if re.search(rx, ln):
            if k == nth:
                return i + 1
            k += 1
    return 0


FN_ROWS = [
    ("M5 world", "s2m_world.cpp", r'"ForceOpenDoor"', "DoorView", "ForceOpenDoor", [], []),
    ("M5 world", "s2m_world.cpp", r'"ForceOpenDoor"', "DoorView", "ForceCloseDoor", [], []),
    ("M5 world", "s2m_world.cpp", r'"GetCurrentPhaseIndex"', "DestructibleObject", "GetCurrentPhaseIndex", [], [("ri", None)]),
    ("M5 world", "s2m_world.cpp", r'"StartDestructActions"', "DestructibleObject", "StartDestructActions", [], []),
    ("M5 world", "s2m_world.cpp", r'"IsEnabled"', "InteractableObject", "IsEnabled", [], [("rb", None)]),
    ("M5 world", "s2m_world.cpp", r'"Toggle"', "InteractableObject", "Toggle", [], []),
    ("M5 world", "s2m_world.cpp", r'"LightOn"', "BP_Light_Base_C", "LightOn", [], []),
    ("M5 world", "s2m_world.cpp", r'"LightOn"', "BP_Light_Base_C", "LightOff", [], []),
    ("M5 world", "s2m_world.cpp", r'"ForceActivateOnce"', "Anomaly", "ForceActivateOnce", [], []),
    ("M5 world", "s2m_world.cpp", r'"GetActorScale3D"', "Anomaly", "GetActorScale3D", [], [("rv", None)]),
    ("M5 world", "s2m_world.cpp", r'"K2_GetComponentRotation"', "StaticMeshComponent", "K2_GetComponentRotation", [], [("rr", None)]),
    ("M5 world", "s2m_world.cpp", r'"IsVisible"', "PointLightComponent", "IsVisible", [], [("rb", None)]),
    ("M5 world", "s2m_world_x0.cpp", r'"SetEnabled", L"bInEnabled"', "InteractableObject", "SetEnabled", [("b", "bInEnabled")], []),
    ("M5 world", "s2m_world_x0.cpp", r'"SetBroken", L"bInBroken"', "InteractableObject", "SetBroken", [("b", "bInBroken")], []),
    ("M5 world", "s2m_world_x0.cpp", r'"PauseSkeletalComponent", L"bPause"', "InteractableObject", "PauseSkeletalComponent", [("b", "bPause")], []),
    ("M5 world", "s2m_world_x0.cpp", r'"SetInteractionActive", L"bActive"', "InteractionComponent", "SetInteractionActive", [("b", "bActive")], []),
    ("M5 world", "s2m_world_x0.cpp", r'"SetActorHiddenInGame", L"bNewHidden"', "DestructibleObject", "SetActorHiddenInGame", [("b", "bNewHidden")], []),
    ("M5 world", "s2m_world_x0.cpp", r'"SetActorEnableCollision", L"bNew', "DestructibleObject", "SetActorEnableCollision", [("b", "bNewActorEnableCollision")], []),
    ("M5 world", "s2m_world_x0.cpp", r'"SetSimulatePhysics", L"bSimulate"', "StaticMeshComponent", "SetSimulatePhysics", [("b", "bSimulate")], []),
    ("M5 world", "s2m_world_x0.cpp", r'"SetStaticMesh", L"NewMesh"', "StaticMeshComponent", "SetStaticMesh", [("o", "NewMesh")], [("rb", None)]),
    ("M5 world", "s2m_world_x0.cpp", r'"K2_GetComponentRotation"', "SceneComponent", "K2_GetComponentRotation", [], [("rr", None)]),
    ("M5 world", "s2m_world_x0.cpp", r'"GetOwner"', "InteractionComponent", "GetOwner", [], [("ro", None)]),
    ("M5 world", "s2m_world_x0.cpp", r'"GetAnimInstance"', "SkeletalMeshComponent", "GetAnimInstance", [], [("ro", None)]),
    ("M6 pop", "s2m_pop.cpp", r'XDeactivateAIFeature|aiFeatures\(', "CustomConsoleManagerAM", "XDeactivateAIFeature", [("i", "Feature")], []),
    ("M6 pop", "s2m_pop.cpp", r'XDeactivateAIFeature|aiFeatures\(', "CustomConsoleManagerAM", "XActivateAIFeature", [("i", "Feature")], []),
    ("M8 lock", "s2m_lock.cpp", r'"UIDialogClose"', "W_DialogueView_C", "UIDialogClose", [], []),
    ("M8 lock", "s2m_lock.cpp", r'"UIDialogOpenTrade"', "W_DialogueView_C", "UIDialogOpenTrade", [], []),
    ("M8 lock", "s2m_lock.cpp", r'"UIDialogOpenTrade"', "W_DialogueView_C", "UIDialogOpenUpgrade", [], []),
    ("M8 lock", "s2m_lock.cpp", r'"SetVisibility"', "ScrollBox", "SetVisibility", [("i", "InVisibility")], []),
    ("M8 lock", "s2m_lock.cpp", r'"GetVisibility"', "ScrollBox", "GetVisibility", [], [("ri", None)]),
    ("M8 lock", "s2m_lock.cpp", r'"GetVisibility"', "UserWidget", "GetVisibility", [], [("ri", None)]),
    ("M10 travel", "s2m_travel_x0.cpp", r'L"EnableInput"', "BP_Stalker2Character_C", "EnableInput", [("o", "PlayerController")], []),
    ("M10 travel", "s2m_travel_x0.cpp", r'L"DisableInput"', "BP_Stalker2Character_C", "DisableInput", [("o", "PlayerController")], []),
    ("M11 session", "s2m_session.cpp", r'XResumeScheduledEmission', "CustomConsoleManagerSM", "XResumeScheduledEmission", [], []),
    ("M11 session", "s2m_session.cpp", r'XGetEmissionState', "CustomConsoleManagerSM", "XGetEmissionState", [], []),
    ("M11 session", "s2m_session.cpp", r'XPauseScheduledEmission', "CustomConsoleManagerSM", "XPauseScheduledEmission", [], []),
    ("M11 session", "s2m_session.cpp", r'XStartEmission', "CustomConsoleManagerSM", "XStartEmission", [], []),
    ("M11 session", "s2m_session.cpp", r'XStopEmission', "CustomConsoleManagerSM", "XStopEmission", [], []),
    ("M11 session", "s2m_session_x0.cpp", r'"GetHP"', "BP_Stalker2Character_C", "GetHP", [], [("rf", None)]),
    ("M11 session", "s2m_session_x0.cpp", r'"GetCurrentSleepinessPoints"', "BP_Stalker2Character_C", "GetCurrentSleepinessPoints", [], [("rf", None)]),
    ("M11 session", "s2m_session_x0.cpp", r'"GetCurrentHungerPoints"', "BP_Stalker2Character_C", "GetCurrentHungerPoints", [], [("rf", None)]),
]

PROP_ROWS = [
    ("M5 world", "s2m_world.cpp", r'"FirstLeafMeshComponent"', "DoorView", "Ref", "FirstLeafMeshComponent"),
    ("M5 world", "s2m_world.cpp", r'"StaticMeshComponent", comp', "DestructibleObject", "Ref", "StaticMeshComponent"),
    ("M5 world", "s2m_world.cpp", r'"bHidden"', "DestructibleObject", "bool", "bHidden"),
    ("M5 world", "s2m_world.cpp", r'"bEnabled"', "InteractableObject", "bool", "bEnabled"),
    ("M5 world", "s2m_world.cpp", r'"bBroken"', "InteractableObject", "bool", "bBroken"),
    ("M5 world", "s2m_world.cpp", r'"bIsInteractionActive"', "InteractionComponent", "bool", "bIsInteractionActive"),
    ("M5 world", "s2m_world.cpp", r'"MainLight"', "BP_Light_Base_C", "Ref", "MainLight"),
    ("M6 pop", "s2m_pop_x0.cpp", r'"MeshComponent", m\)', "BP_StaticItemContainer_C", "Ref", "MeshComponent"),
    ("M6 pop", "s2m_pop_x0.cpp", r'std::string path = ', "Character", "Ref", "Mesh"),
    ("M6 pop", "s2m_pop_x0.cpp", r'std::string path = ', "Character", "Ref", "Mesh.SkeletalMesh"),
    ("M6 pop", "s2m_pop_x0.cpp", r'std::string path = ', "BP_StaticItemContainer_C", "Ref", "MeshComponent.StaticMesh"),
    ("M8 lock", "s2m_lock.cpp", r'"bIsInteractionActive"', "InteractionComponent", "bool", "bIsInteractionActive"),
    ("M8 lock", "s2m_lock.cpp", r'"NPCComponent"', "BP_AI_Human_C", "Ref", "NPCComponent"),
    ("M8 lock", "s2m_lock.cpp", r'"BedMeshComponent"', "BedView", "Ref", "BedMeshComponent"),
    ("M8 lock", "s2m_lock.cpp", r'"AnswerContainer"', "W_DialogueView_C", "Ref", "AnswerContainer"),
    ("M8 lock", "s2m_lock.cpp", r'"ButtonHintTrade"', "W_DialogueView_C", "Ref", "ButtonHintTrade"),
    ("M10 travel", "s2m_travel_x0.cpp", r'"bInCinematicSequence"', "PC", "bool", "bInCinematicSequence"),
]

CLASS_RX = [
    r'(?:first_of|all_of|is_a)\(\s*(?:\w+,\s*)?L"([^"]+)"',
    r'list_class\("([^"]+)"',
    r'owned_components\([^,]+,\s*"([^"]+)"',
    r'class_chain_has\([^,]+,\s*"([^"]+)"',
]


def add_extra(g):
    add = g.add
    for mod, base, rx, cls, fn, bl, rl in FN_ROWS:
        ln = srcline(g, base, rx)
        label = ",".join("%s(%s)" % (h, p) for h, p in bl)
        if rl:
            label += (" -> " if label else "-> ") + ",".join("%s(%s)" % (h, p or "ReturnValue") for h, p in rl)
        g.check_fn(mod, base, ln, cls, fn, label or "(no args)", bl, rl, " [logic-level name: class given by the caller's list_class/first_of]")
    for mod, base, rx, cls, T, pth in PROP_ROWS:
        ln = srcline(g, base, rx)
        g.check_prop(mod, base, ln, cls, "prop", T, pth, " [logic-level name]")
    for mod, files in g.MODFILES.items():
        for base in files:
            path = os.path.join(g.SRC, base)
            if not os.path.exists(path):
                continue
            seen = set()
            for i, ln in enumerate(open(path, encoding="utf-8", errors="replace").read().split("\n")):
                for rx in CLASS_RX:
                    for m in re.finditer(rx, ln):
                        c = m.group(1)
                        if (c, base) in seen:
                            continue
                        seen.add((c, base))
                        known = [p for p in g.short.get(c, []) if p in g.CLASSES]
                        if known:
                            add(mod, base, i + 1, c, "class " + c, "class name used by first_of/all_of/is_a", "class exists: " + known[0], "OK")
                        else:
                            add(mod, base, i + 1, c, "class " + c, "class name used by first_of/all_of/is_a", "class absent from the dump (blueprint class not loaded at dump time, or wrong name)", "NOT-FOUND")
    for base in ("s2m_world_x0.cpp", "s2m_travel_x0.cpp", "s2m_session_x0.cpp", "s2m_pop_x0.cpp", "s2m_lock.cpp", "s2m_bodies.cpp", "s2m_damage.cpp", "s2m_quest.cpp"):
        path = os.path.join(g.SRC, base)
        if not os.path.exists(path):
            continue
        mod = [m for m, fl in g.MODFILES.items() if base in fl][0]
        for i, ln in enumerate(open(path, encoding="utf-8", errors="replace").read().split("\n")):
            for m in re.finditer(r'find_path\(L"(/Script/[^"]+)"\)', ln):
                pth = m.group(1)
                tail = pth.rsplit(".", 1)[-1]
                found = False
                if tail.startswith("Default__"):
                    cls = tail[len("Default__"):]
                    found = any(p in g.CLASSES for p in g.short.get(cls, []))
                else:
                    found = pth in g.CLASSES or pth in g.STRUCTS
                add(mod, base, i + 1, tail, pth, "find_path", "class %s exists in dump; CDO line present" % tail if found else "no such object in dump", "OK" if found else "NOT-FOUND")
    d = os.path.join(g.DATA, "s2u_actions.tsv")
    rows = list(csv.reader(open(d, encoding="utf-8"), delimiter="\t"))[1:]
    cnt = collections.Counter()
    bad = []
    for r in rows:
        k = r[0][1:]
        key = r[1]
        kn = [p for p in g.short.get(k, []) if p in g.CLASSES]
        items = []
        if key in ("son", "soff") and r[5]:
            items.append(("fn", r[5]))
        if key == "sclick" and r[3] == "i3":
            items.append(("fn", r[5]))
        if key == "sclick" and r[3] == "i1":
            items.append(("comp", r[5]))
        for t, v in items:
            if not kn:
                cnt[t + "_class_absent"] += 1
                continue
            if t == "fn":
                q, f = g.find_fn(k, v)
                if f:
                    cnt["fn_found"] += 1
                else:
                    cnt["fn_missing"] += 1
                    bad.append(("fn", k, v))
            else:
                q, pr = g.find_prop(kn, v)
                if pr:
                    cnt["comp_found"] += 1
                else:
                    cnt["comp_missing"] += 1
                    bad.append(("comp", k, v))
    ln = srcline(g, "s2m_world.cpp", r'step\(named')
    add("M5 world", "s2m_world.cpp", ln, "s2u_actions.tsv", "action fn names (son/soff/click fn)", "%d fn + %d comp refs" % (cnt["fn_found"] + cnt["fn_missing"] + cnt["fn_class_absent"], cnt["comp_found"] + cnt["comp_missing"] + cnt["comp_class_absent"]),
        "fn found on class chain %d, missing %d, class absent from dump (unverifiable offline) %d; comp prop found %d, missing %d, class absent %d" % (cnt["fn_found"], cnt["fn_missing"], cnt["fn_class_absent"], cnt["comp_found"], cnt["comp_missing"], cnt["comp_class_absent"]),
        "WRONG" if bad else ("OK" if cnt["fn_found"] else "NOT-FOUND"), "data/s2u_actions.tsv x " + g.SRCNAME)
    for t, k, v in bad:
        add("M5 world", "data/s2u_actions.tsv", 0, k, v, t, "not on class chain", "WRONG")
    kt = os.path.join(g.DATA, "s2u_destruct.tsv")
    ass = set()
    for r in list(csv.reader(open(kt, encoding="utf-8"), delimiter="\t"))[1:]:
        if r[1] in ("sfx", "sau", "snm") and r[3]:
            ass.add(r[3])
    dump = open(g.DUMP_PATH, encoding="utf-8", errors="replace").read() if hasattr(g, "DUMP_PATH") else ""
    found = sum(1 for a in ass if dump and a in dump)
    ln = srcline(g, "s2m_world.cpp", r'env_\.asset\(e\.fx')
    add("M5 world", "s2m_world.cpp", ln, "s2u_destruct.tsv", "asset(path) for fx/au/mesh", "%d distinct asset paths" % len(ass),
        "%d of them appear as loaded objects in the dump; asset() is find_path = StaticFindObject without load, so unloaded paths return null" % found,
        "NOT-FOUND" if found < len(ass) else "OK", "data/s2u_destruct.tsv x " + g.SRCNAME)

    S = "logic check vs " + g.SRCNAME
    l = srcline(g, "s2m_travel_x0.cpp", r'c\.o\(L"PlayerController", pc\)')
    add("M10 travel", "s2m_travel_x0.cpp", l, "Actor", "EnableInput/DisableInput", "o(PlayerController)=first_of(PlayerController)", "controller passed, matches Object<PlayerController>", "OK", S)
