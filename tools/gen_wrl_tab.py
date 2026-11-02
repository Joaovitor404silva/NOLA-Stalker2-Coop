import json, os, sys, glob

ADD = {"EAdditiveAnimationType::AAT_None": 0, "EAdditiveAnimationType::AAT_LocalSpaceBase": 1, "EAdditiveAnimationType::AAT_RotationOffsetMeshSpace": 2}


def load_dir(root):
    out = {}
    for p in glob.glob(os.path.join(root, '**', '*.json'), recursive=True):
        d = json.load(open(p, encoding='utf-8'))
        out[d[0]['Name']] = d
    return out


def leaf(op):
    return op.split('/')[-1].split('.')[0]


def game_path(op):
    base = op.rsplit('.', 1)[0]
    return base + '.' + base.split('/')[-1]


def main():
    src = sys.argv[1]
    dst = sys.argv[2]
    colls = load_dir(os.path.join(src, 'coll'))
    monts = load_dir(os.path.join(src, 'mont'))
    seqs = load_dir(os.path.join(src, 'seq'))
    rows = {}
    skipped = []
    for name in sorted(colls):
        if 'zombie' in name.lower():
            continue
        ia = colls[name][0].get('Properties', {}).get('InternalAnimations', {})
        pick = None
        for st in ia.get('Reloading') or []:
            if st.get('Key') != 'EAnimationStates::Stand':
                continue
            for mag in st.get('Value', {}).get('MagazineReload', []) or []:
                for ra in mag.get('Value', {}).get('MagazineReloadAnimations', []) or []:
                    v = ra.get('Value', {})
                    if ra.get('Key') == 'EAnimationReloadTypes::Full' and v.get('Character') and v.get('Weapon'):
                        pick = (v['Character']['ObjectPath'], v['Weapon']['ObjectPath'], float(v.get('PlayRate', 1.0)))
                        break
                if pick:
                    break
            if pick:
                break
        if not pick:
            skipped.append(name)
            continue
        segs = []
        for mp in pick[:2]:
            m = monts.get(leaf(mp))
            if not m:
                segs.append(None)
                continue
            tr = m[0]['Properties'].get('SlotAnimTracks', [])
            s = tr[0]['AnimTrack']['AnimSegments'][0]['AnimReference']['ObjectPath'] if tr and tr[0]['AnimTrack'].get('AnimSegments') else None
            segs.append(s)
        if None in segs:
            skipped.append(name)
            continue
        cs, ws = seqs.get(leaf(segs[0])), seqs.get(leaf(segs[1]))
        if not cs or not ws:
            skipped.append(name)
            continue
        skel = leaf(ws[0]['Properties']['Skeleton']['ObjectPath'])
        add = ADD.get(cs[0]['Properties'].get('AdditiveAnimType', 'EAdditiveAnimationType::AAT_None'), 0)
        ln = float(ws[0]['Properties'].get('SequenceLength', 0))
        row = (skel, game_path(segs[0]), game_path(segs[1]), add, ln, pick[2], name)
        if skel in rows:
            if rows[skel][1:4] != row[1:4]:
                skipped.append(name + ' (skeleton ' + skel + ' already from ' + rows[skel][6] + ')')
            continue
        rows[skel] = row
    with open(dst, 'w', encoding='utf-8', newline='\n') as f:
        for skel in sorted(rows):
            r = rows[skel]
            f.write('X("%s", "%s", "%s", %d, %.4f, %.3f)\n' % (r[0], r[1], r[2], r[3], r[4], r[5]))
    print(len(rows), 'rows ->', dst)
    for s in skipped:
        print('skipped', s)


main()
