import json, os, sys, glob, re

ADD = {"EAdditiveAnimationType::AAT_None": 0, "EAdditiveAnimationType::AAT_LocalSpaceBase": 1, "EAdditiveAnimationType::AAT_RotationOffsetMeshSpace": 2}
FB = {"pt": "SK_pm_Skeleton", "ar": "SK_AK74_Skeleton"}


def load_dirs(roots):
    out = {}
    for root in roots:
        for p in glob.glob(os.path.join(root, '**', '*.json'), recursive=True):
            d = json.load(open(p, encoding='utf-8'))
            out[d[0]['Name']] = d
    return out


def leaf(op):
    return op.split('/')[-1].split('.')[0]


def game_path(op):
    base = op.rsplit('.', 1)[0]
    return base + '.' + base.split('/')[-1]


def seg0(m):
    tr = m[0]['Properties'].get('SlotAnimTracks', [])
    if not tr or not tr[0]['AnimTrack'].get('AnimSegments'):
        return None
    return tr[0]['AnimTrack']['AnimSegments'][0]['AnimReference']['ObjectPath']


def seq_skel(seqs, monts, mp):
    m = monts.get(leaf(mp)) if mp else None
    s = seg0(m) if m else None
    q = seqs.get(leaf(s)) if s else None
    if not q:
        return None
    return leaf(q[0]['Properties']['Skeleton']['ObjectPath'])


def reload_weapon(ia):
    for st in ia.get('Reloading') or []:
        if st.get('Key') != 'EAnimationStates::Stand':
            continue
        for mag in st.get('Value', {}).get('MagazineReload', []) or []:
            for ra in mag.get('Value', {}).get('MagazineReloadAnimations', []) or []:
                v = ra.get('Value', {})
                if v.get('Weapon'):
                    return v['Weapon']['ObjectPath']
    return None


def parse_cfg(path):
    ents = {}
    cur = None
    for line in open(path, encoding='utf-8-sig', errors='replace'):
        m = re.match(r'^([A-Za-z0-9_\[\]]+)\s*:\s*struct\.begin\s*(\{[^}]*\})?', line)
        if m:
            rk = re.search(r'refkey=([A-Za-z0-9_\[\]]+)', m.group(2) or '')
            cur = {'ref': rk.group(1) if rk else None, 'kv': {}}
            ents[m.group(1)] = cur
            continue
        if line.startswith('struct.end'):
            cur = None
            continue
        if cur is not None:
            k = re.match(r'^ {3}([A-Za-z0-9_]+)\s*=\s*(.*?)\s*$', line)
            if k and k.group(1) not in cur['kv']:
                cur['kv'][k.group(1)] = k.group(2)
    return ents


def resolve(ents, sid, key, depth=0):
    e = ents.get(sid)
    if not e or depth > 20:
        return None
    if key in e['kv']:
        return e['kv'][key]
    return resolve(ents, e['ref'], key, depth + 1) if e['ref'] else None


def weapon_sids(gd):
    ents = {}
    files = [os.path.join(gd, 'GameData', 'ItemPrototypes', 'WeaponPrototypes.cfg')] + glob.glob(os.path.join(gd, 'DLCGameData', '*', 'ItemPrototypes', '*WeaponPrototypes.cfg'))
    for f in files:
        ents.update(parse_cfg(f))
    meshes = {}
    for f in [os.path.join(gd, 'GameData', 'MeshPrototypes.cfg')] + glob.glob(os.path.join(gd, 'DLCGameData', '*', 'MeshPrototypes.cfg')):
        for k, e in parse_cfg(f).items():
            meshes[k] = e
            meshes[e['kv'].get('SID', k)] = e
    out = []
    for sid in sorted(ents):
        if sid.startswith('Template') or 'Stub' in sid:
            continue
        mp = resolve(ents, sid, 'MeshPrototypeSID')
        if not mp:
            continue
        path = resolve(meshes, mp, 'MeshPath') or ''
        m = re.search(r"'([^']+)'", path)
        out.append((sid, mp, m.group(1) if m else None))
    return out


def fam_of(mesh):
    m = re.search(r'/[Ww]eapons/([a-z]+)/', mesh or '')
    c = m.group(1) if m else ''
    if c == 'pt':
        return 'pt'
    if c and c not in ('knifes', 'grenades', 'items', 'attaches', 'bolt'):
        return 'ar'
    return 'bh'


def main():
    cmd = sys.argv[1]
    if cmd == 'meshrx':
        names = sorted({leaf(m) for _, _, m in weapon_sids(sys.argv[2]) if m})
        print('/(' + '|'.join(names) + r')\.uasset$')
        return
    coll_dirs, mont_dirs, seq_dirs, mesh_dir, gd, dst, rep = sys.argv[2].split(';'), sys.argv[3].split(';'), sys.argv[4].split(';'), sys.argv[5], sys.argv[6], sys.argv[7], sys.argv[8]
    colls = load_dirs(coll_dirs)
    monts = load_dirs(mont_dirs)
    seqs = load_dirs(seq_dirs)
    meshes = load_dirs([mesh_dir])
    rows = {}
    skipped = []
    for name in sorted(colls):
        if 'zombie' in name.lower():
            continue
        ia = colls[name][0].get('Properties', {}).get('InternalAnimations', {})
        sh = ia.get('ShootingInHip') or {}
        ch = (sh.get('Character') or {}).get('ObjectPath')
        wp = (sh.get('Weapon') or {}).get('ObjectPath')
        if not ch:
            skipped.append(name + ' (no ShootingInHip)')
            continue
        m = monts.get(leaf(ch))
        s = seg0(m) if m else None
        q = seqs.get(leaf(s)) if s else None
        if not q:
            skipped.append(name + ' (character shoot clip not exported)')
            continue
        skel = seq_skel(seqs, monts, reload_weapon(ia)) or seq_skel(seqs, monts, wp)
        if not skel:
            skipped.append(name + ' (no weapon skeleton)')
            continue
        add = ADD.get(q[0]['Properties'].get('AdditiveAnimType', 'EAdditiveAnimationType::AAT_None'), 0)
        ln = float(q[0]['Properties'].get('SequenceLength', 0))
        wm = monts.get(leaf(wp)) if wp else None
        ws = seg0(wm) if wm else None
        mp_ = m[0]['Properties']
        bi = float((mp_.get('BlendIn') or {}).get('BlendTime', 0.025))
        bo = float((mp_.get('BlendOut') or {}).get('BlendTime', 0.05))
        row = (skel, game_path(s), add, ln, float(sh.get('PlayRate', 1.0)), name, game_path(ws) if ws and seqs.get(leaf(ws)) else '', bi, bo)
        if skel in rows:
            if rows[skel][1] != row[1]:
                skipped.append(name + ' (skeleton ' + skel + ' already from ' + rows[skel][5] + ')')
            continue
        rows[skel] = row
    with open(dst, 'w', encoding='utf-8', newline='\n') as f:
        for skel in sorted(rows):
            r = rows[skel]
            f.write('X("%s", "%s", %d, %.4f, %.3f, "%s", %.3f, %.3f)\n' % (r[0], r[1], r[2], r[3], r[4], r[6], r[7], r[8]))
    lines = []
    n_own = n_fb = n_none = 0
    for sid, mp, mesh in weapon_sids(gd):
        mj = meshes.get(leaf(mesh)) if mesh else None
        sk = None
        if mj:
            for ex in mj:
                if ex.get('Type') == 'SkeletalMesh':
                    sk = leaf(((ex.get('Properties') or {}).get('Skeleton') or {}).get('ObjectPath', '')) or None
        fam = fam_of(mesh)
        if sk in rows:
            how, clip = 'own', leaf(rows[sk][1])
            gun = leaf(rows[sk][6]) if rows[sk][6] else '-'
            n_own += 1
        elif fam in FB and FB[fam] in rows:
            how, clip = 'fallback', leaf(rows[FB[fam]][1])
            gun = '-'
            n_fb += 1
        else:
            how, clip = 'NONE', '-'
            gun = '-'
            n_none += 1
        lines.append('%s\t%s\t%s\t%s\t%s\t%s\t%s' % (sid, mp, sk or '-', fam, how, clip, gun))
    with open(rep, 'w', encoding='utf-8', newline='\n') as f:
        f.write('sid\tmesh_sid\tskeleton\tfam\tshoot\tclip\tgun\n')
        f.write('\n'.join(lines) + '\n')
    print(len(rows), 'rows ->', dst)
    print('sids own %d fallback %d none %d -> %s' % (n_own, n_fb, n_none, rep))
    for s in skipped:
        print('skipped', s)


main()
