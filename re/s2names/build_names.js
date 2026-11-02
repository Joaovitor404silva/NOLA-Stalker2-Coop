// Builds C:\modding\s2names\names.tsv (rva<TAB>kind<TAB>name<TAB>source; rva hex, no 0x, image base 0x140000000)
// from: ufuncs_raw.tsv + vtables_raw.tsv (S2Dev dumpfuncs/dumpvtables), re_out natives tables, UE4SS.log vtable layout,
// RE_MAP.md rows, PE imports/exports of the original exe. Usage: node build_names.js
const fs = require('fs'), path = require('path');
const D = 'C:\\modding\\s2names', GAME = 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\S.T.A.L.K.E.R. 2 Heart of Chornobyl';
const EXE = 'C:\\modding\\ghidra_in\\s2.exe', IB = 0x140000000;
const TEXT_BEG = 0x1000, TEXT_END = 0x1000 + 0x7ccc008;
const rd = f => fs.existsSync(f) ? fs.readFileSync(f, 'utf8').split(/\r?\n/) : [];
const tsv = f => rd(f).filter(l => l).map(l => l.split('\t'));

const names = new Map();   // rva -> {kind,name,source,prio}
const counts = {};
function add(rva, kind, name, source, prio) {
  if (!rva || !isFinite(rva)) return;
  name = name.replace(/[\s\t]+/g, '_');
  const cur = names.get(rva);
  if (cur && cur.prio >= prio) return;
  names.set(rva, { kind, name, source, prio });
}
const hex = s => parseInt(s, 16);
const inText = r => r >= TEXT_BEG && r < TEXT_END;

// ---- UE4SS: known functions/globals (absolute addresses of its run) + vtable layout (byte offsets per class)
const ue4ss = rd(path.join(GAME, 'Stalker2\\Binaries\\Win64\\ue4ss\\UE4SS.log'));
let ue4Base = 0; for (const l of ue4ss) { const m = l.match(/MainExe @ 0x([0-9a-f]+)/i); if (m) ue4Base = hex(m[1]); }
const slotNames = {};  // UE class name -> {slotIdx: name}
let layoutDone = false;
for (const l of ue4ss) {
  const body = l.replace(/^\[[^\]]*\]\s*/, '');
  if (!layoutDone) {
    const m = body.match(/^([UAF])([A-Za-z0-9_]+)::(\w+) = 0x([0-9A-F]+)$/);
    if (m) {
      let cls = m[1] === 'F' ? 'F' + m[2] : m[2]; if (cls === 'ObjectBase' || cls === 'ObjectBaseUtility') cls = 'Object';
      const off = hex(m[4]); if (off % 8 === 0 && !m[3].startsWith('__vecDelDtor') || off === 0) (slotNames[cls] = slotNames[cls] || {})[off / 8] = m[3] === '__vecDelDtor' ? '__vecDelDtor' : m[3];
    }
    if (/Post-initialization/.test(body)) layoutDone = true;  // member offsets come later
  }
}
// cut layout lines at the first member-offset block: UE4SS prints vtables first (ini), members after init
if (ue4Base) {
  const known = [[/Found FName::FName\(wchar_t\*\): 0x([0-9a-f]+)/i, 'FName::FName'], [/Found StaticConstructObject_Internal: 0x([0-9a-f]+)/i, 'StaticConstructObject_Internal'],
    [/Found ConsoleManagerSingleton: 0x([0-9a-f]+)/i, 'ConsoleManagerSingleton'], [/GMalloc address: 0x([0-9a-f]+)/i, 'GMalloc'],
    [/FName::ToString address: 0x([0-9a-f]+)/i, 'FName::ToString'], [/GNatives address: 0x([0-9a-f]+)/i, 'GNatives'],
    [/GUObjectHashTables_Get address: 0x([0-9a-f]+)/i, 'GUObjectHashTables'], [/ProcessLocalScriptFunction address: 0x([0-9a-f]+)/i, 'ProcessLocalScriptFunction'],
    [/GUObjectArray address: 0x([0-9a-f]+)/i, 'GUObjectArray'], [/GameEngine::LoadMap address 0x([0-9a-f]+)/i, 'UGameEngine::LoadMap'],
    [/GameModeBase::InitGameState address 0x([0-9a-f]+)/i, 'AGameModeBase::InitGameState'], [/ProcessEvent address 0x([0-9a-f]+)/i, 'UObject::ProcessEvent'],
    [/ProcessConsoleExec address 0x([0-9a-f]+)/i, 'UObject::ProcessConsoleExec'], [/UStruct::Link address 0x([0-9a-f]+)/i, 'UStruct::Link'],
    [/ProcessInternal address: 0x([0-9a-f]+)/i, 'UObject::ProcessInternal'], [/GameViewportClient::Tick address 0x([0-9a-f]+)/i, 'UGameViewportClient::Tick'],
    [/AActor::Tick address 0x([0-9a-f]+)/i, 'AActor::Tick'], [/AActor::BeginPlay address 0x([0-9a-f]+)/i, 'AActor::BeginPlay'], [/AActor::EndPlay address 0x([0-9a-f]+)/i, 'AActor::EndPlay']];
  for (const l of ue4ss) for (const [re, n] of known) { const m = l.match(re); if (m) { const r = hex(m[1]) - ue4Base; if (r > 0 && r < 0xb964000) add(r, inText(r) ? 'ue_core' : 'global', n, 'UE4SS.log', 90); } }
  add(0x50c40c, 'ue_core', 'UGameEngine::Tick', 'UE4SS.log', 90);
}

// ---- UFunctions (live)
const execCount = new Map();
const uf = tsv(path.join(D, 'ufuncs_raw.tsv')).slice(1);
for (const [ow, fn, kind, fl, r] of uf) { const v = hex(r); if (v) execCount.set(v, (execCount.get(v) || 0) + 1); }
const execName = new Map(), liveNames = new Set(uf.map(r => r[1]));
for (const [ow, fn, kind, fl, r] of uf) {
  const v = hex(r); if (!v || !inText(v)) continue;
  if (execCount.get(v) > 1) continue;            // ProcessInternal and other shared thunks
  if (!(hex(fl) & 0x400)) continue;               // FUNC_Native only
  execName.set(v, [ow, fn]);
  add(v, 'ufunc_exec', `${ow}::exec${fn}`, 'S2Dev dumpfuncs', 80);
}
// impl via static natives_impl (exec -> impl); impl shared by >1 exec is not named after one of them
const ni = tsv('C:\\modding\\re_out\\natives_impl.tsv');
// exec -> impl: Ghidra natives_impl first, else first non-helper direct callee from S2Dev `callees` (exec_callees.tsv)
const helpers = new Set(rd('C:\\modding\\re_out\\helpers.txt').filter(x => x.trim()).map(x => hex(x) - IB));
const impls = [];  // [execRva, implRva, staticName, source]
const niExec = new Set();
for (const [n, e, i] of ni) if (i && i.trim()) { impls.push([hex(e) - IB, hex(i) - IB, n, 'natives_impl.tsv']); niExec.add(hex(e) - IB); }
for (const [e, b, s] of tsv(path.join(D, 'exec_callees.tsv'))) {
  const ev = hex(e); if (niExec.has(ev) || !s) continue;
  const c = s.split(',').map(x => hex(x.slice(2))).find(x => !helpers.has(x)); if (c) impls.push([ev, c, null, 'S2Dev callees']);
}
const implUsers = new Map();
for (const [ev, iv] of impls) { if (!implUsers.has(iv)) implUsers.set(iv, new Set()); implUsers.get(iv).add(ev); }
for (const [ev, iv, n, src] of impls) {
  if (implUsers.get(iv).size > 1 || !inText(iv) || iv === ev) continue;
  const en = execName.get(ev);
  if (en) add(iv, 'ufunc_impl', `${en[0]}::${en[1]}`, src + '+dumpfuncs', 75);
  else if (n && !liveNames.has(n)) add(iv, 'native_impl', n, src, 40);
}
// static natives (name, no class) for execs the live dump did not see
for (const [t, n, e] of tsv('C:\\modding\\re_out\\natives.tsv')) { const ev = hex(e) - IB; if (inText(ev) && !execName.has(ev) && !liveNames.has(n) && /^[A-Za-z_]\w*$/.test(n)) add(ev, 'native_exec', `exec${n}`, 'natives.tsv', 30); }

// ---- vtables (live CDOs). Slot i of class C names the function only if C introduces or overrides it.
const vt = tsv(path.join(D, 'vtables_raw.tsv')).slice(1).map(([c, s, meta, v, n, p, slots, cf]) => ({ c, s, meta, v: hex(v), slots: slots ? slots.split(',').map(hex) : [], cf: cf ? cf.split(',').map(x => x.split(':').map(hex)) : [] }));
const byName = new Map(); for (const x of vt) if (x.meta === 'Class' && !byName.has(x.c)) byName.set(x.c, x);
// vtable length: UE vtables have no RTTI separator, so the live walk may run into the next vtable. Cap at the next known
// vtable start and at the shortest child (a child contains all of its parent's slots); leaf tails stay unverified.
const starts = [...new Set(vt.map(x => x.v))].sort((a, b) => a - b);
for (const x of vt) { const i = starts.indexOf(x.v); const nx = starts[i + 1]; x.cap = nx ? Math.min(x.slots.length, (nx - x.v) / 8) : x.slots.length; x.kids = 0; }
const depth = x => { let d = 0; for (let s = x; s && d < 200; s = byName.get(s.s)) d++; return d; };
const introName = (x, i) => {  // name of slot i from the topmost ancestor that has it and a UE4SS name
  let chain = []; for (let s = x; s; s = byName.get(s.s)) chain.push(s);
  for (let k = chain.length - 1; k >= 0; k--) if (chain[k].slots.length > i) { const nm = (slotNames[chain[k].c] || {})[i]; if (nm) return nm; }
  return null;
};
const byDepth = vt.filter(x => x.meta === 'Class').sort((a, b) => depth(b) - depth(a));
for (const x of byDepth) { const sp = byName.get(x.s); if (sp && sp !== x && sp.v !== x.v) { sp.cap = Math.min(sp.cap, x.cap); sp.kids++; } }
for (const x of vt) if (x.meta !== 'Class') { const sp = byName.get(x.s); if (sp && sp.v === x.v) x.cap = sp.cap; }
const slotIdx = new Map(); const noteSlot = (f, i) => { if (!slotIdx.has(f)) slotIdx.set(f, new Set()); slotIdx.get(f).add(i); };
const vfCand = new Map();  // rva -> [{name, depth}]
const vtSeen = new Set();
for (const x of vt) {
  if (vtSeen.has(x.v)) continue; vtSeen.add(x.v);
  const sup = byName.get(x.s);
  if (x.meta !== 'Class' && sup && sup.v === x.v) continue;
  add(x.v, 'vtable', `${x.c}::vftable`, 'S2Dev dumpvtables', 70);
  const d = depth(x);
  x.slots.slice(0, x.cap).forEach((f, i) => {
    if (sup && sup.cap > i && sup.slots[i] === f) return;   // inherited unchanged
    const nm = introName(x, i) || `vf_${i}`;
    noteSlot(f, i);
    const tail = !x.kids && (!sup || i >= sup.cap);
    if (!vfCand.has(f)) vfCand.set(f, []); vfCand.get(f).push({ name: `${x.c}::${nm}`, d, tail });
  });
}
for (const [f, cands] of vfCand) {
  cands.sort((a, b) => (a.tail - b.tail) || (a.d - b.d)); const c0 = cands[0];
  add(f, 'vfunc', c0.name, 'S2Dev dumpvtables' + (cands.length > 1 ? ` (shared x${cands.length})` : '') + (c0.tail ? ' (leaf tail, unverified)' : ''), c0.tail ? 45 : cands.length > 4 ? 50 : 60);
}

// ---- UClass code pointers: +0xB0 ClassConstructor (InternalConstructor<T>: mov rcx,[rcx]; jmp T::T), +0xB8 VTableHelperCtorCaller,
// +0xC0 AddReferencedObjects. Shared bodies take the shallowest class.
{
  const what = { 0xb0: 'InternalConstructor', 0xb8: 'VTableHelperCtorCaller', 0xc0: 'AddReferencedObjects' };
  const cand = new Map(), ctorOf = new Map();
  for (const x of vt) if (x.meta === 'Class') for (const [o, f] of x.cf) if (what[o]) {
    if (!cand.has(f)) cand.set(f, []); cand.get(f).push({ c: x.c, d: depth(x), o });
    if (o === 0xb0) ctorOf.set(f, x.c);
  }
  for (const [f, cs] of cand) { cs.sort((a, b) => a.d - b.d); add(f, 'uclass_fn', cs[0].c + '::' + what[cs[0].o], 'S2Dev dumpvtables UClass+0x' + cs[0].o.toString(16) + (cs.length > 1 ? ' (shared x' + cs.length + ')' : ''), 55); }
  const ctorUse = new Map();
  const ctorRows = tsv(path.join(D, 'ctor_callees.tsv')).filter(r => r[2] && r[2].startsWith('J:') && cand.get(hex(r[0])).length === 1);
  for (const r of ctorRows) { const t = hex(r[2].split(',')[0].slice(2)); ctorUse.set(t, (ctorUse.get(t) || 0) + 1); }
  for (const r of ctorRows) { const t = hex(r[2].split(',')[0].slice(2)), c = ctorOf.get(hex(r[0])); if (ctorUse.get(t) === 1) add(t, 'ctor', c + '::' + c, 'InternalConstructor tail jump', 55); }
}

// ---- FField classes (dumpfieldvt): abstract supers have no live instance, so a slot shared by several classes is named
// after their lowest common ancestor; leaf length = shortest sibling (FProperty leaves add no virtuals, walk overruns)
{
  const fr = tsv(path.join(D, 'fieldvt_raw.tsv')).slice(1).filter(r => !/^FNone$|</.test(r[0]) && !/</.test(r[1]))
    .map(([c, s, meta, v, n, p, slots]) => ({ c, s, v: hex(v), n: +n, slots: slots ? slots.split(',').map(hex) : [] }));
  const uniq = new Map(); for (const x of fr) if (!uniq.has(x.c)) uniq.set(x.c, x);
  const sup = { FNumericProperty: 'FProperty', FObjectPropertyBase: 'FProperty', FMulticastDelegateProperty: 'FProperty', FProperty: 'FField' };
  for (const x of uniq.values()) sup[x.c] = x.s;
  const chain = c => { const r = []; for (let k = c; k && r.length < 16; k = sup[k]) r.push(k); return r; };
  const minSib = {}; for (const x of uniq.values()) minSib[x.s] = Math.min(minSib[x.s] || 1e9, x.n);
  const at = new Map();  // "i:f" -> [classes]
  for (const x of uniq.values()) {
    add(x.v, 'vtable', x.c + '::vftable', 'S2Dev dumpfieldvt', 70);
    x.slots.slice(0, minSib[x.s]).forEach((f, i) => { const k = i + ':' + f; if (!at.has(k)) at.set(k, []); at.get(k).push(x.c); });
  }
  for (const [k, cls] of at) {
    const [i, f] = k.split(':').map(Number); noteSlot(f, i);
    let lca = cls[0]; if (cls.length > 1) { const sets = cls.map(c => chain(c)); lca = sets[0].find(a => sets.every(ch => ch.includes(a))) || 'FField'; }
    const ch = chain(lca); let nm = null; for (let j = ch.length - 1; j >= 0 && !nm; j--) nm = (slotNames[ch[j]] || {})[i] || null;
    add(f, 'vfunc', lca + '::' + (nm || 'vf_' + i), 'S2Dev dumpfieldvt' + (cls.length > 1 ? ' (shared x' + cls.length + ')' : ''), 58);
  }
}

// a body used at 3+ different vtable slot indexes is an ICF-merged trivial stub (return 0, empty...): no class name
for (const [f, set] of slotIdx) { const cur = names.get(f); if (set.size >= 3 && cur && cur.kind === 'vfunc') names.set(f, { kind: 'vfunc', name: 'vstub_' + f.toString(16), source: 'ICF stub, ' + set.size + ' slot indexes', prio: cur.prio }); }

// ---- RE_MAP.md rows
for (const l of rd(path.join(GAME, 'ModWork\\agents\\RE_MAP.md'))) {
  if (!l.startsWith('|')) continue;
  const cells = l.split('|').slice(1, -1).map(c => c.trim().replace(/\*\*/g, '').replace(/`/g, ''));
  if (cells.length < 2) continue;
  const a0 = cells[0].match(/^0x(14[0-9a-f]{7})$/i); if (!a0) continue;
  let nameCell = cells[1], extra = '';
  if (/^0x14[0-9a-f]{7}$/i.test(nameCell)) { nameCell = cells[2] || ''; extra = '_exec';
    const iv = hex(cells[1].slice(2)) - IB, nm = clean(nameCell); if (nm) add(iv, 'remap', nm, 'RE_MAP.md', 72); }
  const nm = clean(nameCell); if (nm) add(hex(a0[1]) - IB, 'remap', nm + extra, 'RE_MAP.md', 72);
}
function clean(s) {
  s = s.split('(')[0].replace(/ impl$/, '_impl').replace(/ exec$/, '_exec').trim();
  if (!s || s === '-' || /^0x/i.test(s)) return '';
  s = s.replace(/[^A-Za-z0-9_:~]+/g, '_').replace(/^_+|_+$/g, '');
  return /^[A-Za-z_]/.test(s) && s.length <= 80 ? s : '';
}

// ---- PE imports / exports of the original exe
if (fs.existsSync(EXE)) {
  const fd = fs.openSync(EXE, 'r'); const hdr = Buffer.alloc(0x1000); fs.readSync(fd, hdr, 0, 0x1000, 0);
  const pe = hdr.readUInt32LE(0x3c), coff = pe + 4, ns = hdr.readUInt16LE(coff + 2), soh = hdr.readUInt16LE(coff + 16), opt = coff + 20;
  const secs = []; for (let i = 0; i < ns; i++) { const o = opt + soh + i * 40; secs.push({ va: hdr.readUInt32LE(o + 12), vs: hdr.readUInt32LE(o + 8), raw: hdr.readUInt32LE(o + 20) }); }
  const off = r => { for (const s of secs) if (r >= s.va && r < s.va + Math.max(s.vs, 1)) return r - s.va + s.raw; return -1; };
  const rdb = (r, n) => { const b = Buffer.alloc(n); fs.readSync(fd, b, 0, n, off(r)); return b; };
  const cstr = r => { const b = rdb(r, 512); return b.toString('latin1', 0, b.indexOf(0)); };
  const dir = i => [hdr.readUInt32LE(opt + 112 + i * 8), hdr.readUInt32LE(opt + 112 + i * 8 + 4)];
  const [er, es] = dir(0);
  if (er && es) {
    const e = rdb(er, 40), nf = e.readUInt32LE(20), nn = e.readUInt32LE(24), af = e.readUInt32LE(28), an = e.readUInt32LE(32), ao = e.readUInt32LE(36);
    const fa = rdb(af, nf * 4), na = rdb(an, nn * 4), oa = rdb(ao, nn * 2);
    for (let i = 0; i < nn; i++) { const fr = fa.readUInt32LE(oa.readUInt16LE(i * 2) * 4); if (fr < er || fr >= er + es) add(fr, 'export', cstr(na.readUInt32LE(i * 4)), 'PE exports', 95); }
  }
  const [ir] = dir(1);
  for (let d = ir; ; d += 20) {
    const b = rdb(d, 20); const ilt = b.readUInt32LE(0) || b.readUInt32LE(16), nm = b.readUInt32LE(12), iat = b.readUInt32LE(16); if (!nm) break;
    const dll = cstr(nm).replace(/\.dll$/i, '');
    for (let k = 0; ; k++) {
      const t = rdb(ilt + k * 8, 8).readBigUInt64LE(0); if (!t) break;
      const fn = (t >> 63n) ? `Ordinal_${Number(t & 0xffffn)}` : cstr(Number(t & 0x7fffffffn) + 2);
      add(iat + k * 8, 'import', `__imp_${dll}!${fn}`, 'PE imports', 95);
    }
  }
  fs.closeSync(fd);
}

// ---- write
const rows = [...names.entries()].sort((a, b) => a[0] - b[0]);
const out = ['rva\tkind\tname\tsource'];
for (const [r, v] of rows) { out.push(`${r.toString(16)}\t${v.kind}\t${v.name}\t${v.source}`); counts[v.kind] = (counts[v.kind] || 0) + 1; }
fs.writeFileSync(path.join(D, 'names.tsv.tmp'), out.join('\n') + '\n');
fs.renameSync(path.join(D, 'names.tsv.tmp'), path.join(D, 'names.tsv'));
console.log('names.tsv rows', rows.length, JSON.stringify(counts));
