// Builds C:\modding\s2decomp\static_names.tsv (rva<TAB>kind<TAB>name<TAB>source, rva hex from 0x140000000)
// from re_out\natives_impl.tsv and the RE_MAP.md tables. Same format as s2names\names.tsv.
const fs = require('fs');
const BASE = 0x140000000;
const OUT = 'C:\\modding\\s2decomp\\static_names.tsv';
const NAT = 'C:\\modding\\re_out\\natives_impl.tsv';
const REMAP = 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\S.T.A.L.K.E.R. 2 Heart of Chornobyl\\ModWork\\agents\\RE_MAP.md';
const ident = s => s.trim().replace(/[^A-Za-z0-9_:]/g, '_').replace(/_+/g, '_').replace(/^_+|_+$/g, '');
const rows = [];
const implNames = new Map();
for (const line of fs.readFileSync(NAT, 'utf8').split(/\r?\n/)) {
  const p = line.split('\t');
  if (p.length < 2 || !p[1].trim()) continue;
  const name = ident(p[0]); if (!name) continue;
  rows.push([parseInt(p[1], 16) - BASE, 'exec', 'exec' + name, 'natives_impl']);
  const impls = (p[2] || '').trim().split(/\s+/).filter(Boolean);
  if (impls.length === 1) {
    const a = parseInt(impls[0], 16);
    if (!implNames.has(a)) implNames.set(a, new Set());
    implNames.get(a).add(name);
  }
}
for (const [a, ns] of implNames) if (ns.size === 1) rows.push([a - BASE, 'impl', [...ns][0], 'natives_impl']);

let hdr = null;
for (const line of fs.readFileSync(REMAP, 'utf8').split(/\r?\n/)) {
  if (!line.startsWith('|')) { hdr = null; continue; }
  const cells = line.trim().replace(/^\||\|$/g, '').split('|').map(c => c.trim());
  if (!hdr) { hdr = cells.map(c => c.toLowerCase()); continue; }
  if (/^[-:\s|]*$/.test(cells.join(''))) continue;
  const ni = hdr.indexOf('name'); if (ni < 0 || ni >= cells.length) continue;
  let nm = cells[ni].replace(/\(.*/, '').replace(/\*/g, '').trim();
  if (!nm || nm.includes('/')) continue;
  nm = nm.replace(/\s+impl$/, '_impl');
  nm = ident(nm.split(/\s+/)[0]); if (!nm || /^[a-z]/.test(nm)) continue;
  const addr = col => {
    const i = hdr.indexOf(col); if (i < 0 || i >= cells.length) return null;
    const m = /^0x(1[0-9a-fA-F]{8})$/.exec(cells[i].replace(/\*/g, '').trim());
    return m ? parseInt(m[1], 16) : null;
  };
  const ex = addr('exec'), im = addr('impl'), ad = addr('address');
  const short = nm.split('::').pop();
  if (im !== null) rows.push([im - BASE, 'impl', nm, 'RE_MAP']);
  if (ex !== null) rows.push([ex - BASE, 'exec', 'exec' + short, 'RE_MAP']);
  if (im === null && ex === null && ad !== null) rows.push([ad - BASE, 'func', nm, 'RE_MAP']);
}
fs.writeFileSync(OUT, 'rva\tkind\tname\tsource\n' + rows.map(r => `${r[0].toString(16)}\t${r[1]}\t${r[2]}\t${r[3]}`).join('\n') + '\n');
console.log(rows.length, 'rows ->', OUT);
