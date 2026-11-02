// names.tsv -> debugger symbols: s2dev.x64dbg.txt (x64dbg script: lbl mod.main()+rva, "name") and s2dev.map (MSVC-style MAP).
// Usage: node gen_symbols.js [exe for section table]
const fs = require('fs'), path = require('path');
const D = 'C:\\modding\\s2names', EXE = process.argv[2] || 'C:\\S2DevRoot\\Stalker2\\Binaries\\Win64\\Stalker2-Win64-Shipping.exe';
const rows = fs.readFileSync(path.join(D, 'names.tsv'), 'utf8').split(/\r?\n/).slice(1).filter(l => l).map(l => l.split('\t'));
const fd = fs.openSync(EXE, 'r'); const h = Buffer.alloc(0x1000); fs.readSync(fd, h, 0, 0x1000, 0); fs.closeSync(fd);
const pe = h.readUInt32LE(0x3c), coff = pe + 4, ns = h.readUInt16LE(coff + 2), soh = h.readUInt16LE(coff + 16), opt = coff + 20;
const ts = h.readUInt32LE(coff + 4), ib = h.readBigUInt64LE(opt + 24);
const secs = []; for (let i = 0; i < ns; i++) { const o = opt + soh + i * 40; secs.push({ name: h.toString('latin1', o, o + 8).replace(/\0/g, ''), va: h.readUInt32LE(o + 12), vs: h.readUInt32LE(o + 8), ch: h.readUInt32LE(o + 36) }); }
const secOf = r => { for (let i = 0; i < secs.length; i++) if (r >= secs[i].va && r < secs[i].va + secs[i].vs) return i; return -1; };
const H8 = n => n.toString(16).toUpperCase().padStart(8, '0');
const funcKinds = new Set(['ufunc_exec', 'ufunc_impl', 'vfunc', 'native_exec', 'native_impl', 'ue_core', 'remap', 'export']);

// x64dbg: one label per row (labels must be unique: suffix duplicates)
const seen = new Map(); const x = [`// S2 names for x64dbg (${rows.length} labels). Run: Script tab -> Load -> this file -> Run (space). Main module must be the S2 exe.`];
for (const [r, kind, name] of rows) {
  let n = name.replace(/"/g, "'"); const k = seen.get(n) || 0; seen.set(n, k + 1); if (k) n += `_${k}`;
  x.push(`lbl mod.main()+${r}, "${n}"`);
}
x.push('ret');
fs.writeFileSync(path.join(D, 's2dev.x64dbg.txt'), x.join('\r\n') + '\r\n');

// MAP (link.exe /MAP layout: section:offset, Rva+Base, f = function)
const m = [' Stalker2-Win64-Shipping', '', ` Timestamp is ${ts.toString(16)}`, '', ` Preferred load address is ${ib.toString(16).toUpperCase().padStart(16, '0')}`, '',
  ' Start         Length     Name                   Class'];
secs.forEach((s, i) => m.push(` ${(i + 1).toString(16).toUpperCase().padStart(4, '0')}:00000000 ${H8(s.vs)}H ${s.name.padEnd(23)} ${(s.ch & 0x20) ? 'CODE' : 'DATA'}`));
m.push('', '  Address         Publics by Value              Rva+Base               Lib:Object', '');
let nmap = 0;
for (const [r, kind, name] of rows) {
  const rv = parseInt(r, 16), si = secOf(rv); if (si < 0) continue;
  const va = (ib + BigInt(rv)).toString(16).padStart(16, '0');
  m.push(` ${(si + 1).toString(16).toUpperCase().padStart(4, '0')}:${H8(rv - secs[si].va)}       ${name.padEnd(30)} ${va} ${funcKinds.has(kind) ? 'f  ' : '   '} ${kind}`); nmap++;
}
m.push('', ` entry point at        0001:00000000`, '');
fs.writeFileSync(path.join(D, 's2dev.map'), m.join('\r\n') + '\r\n');
console.log(`x64dbg labels ${rows.length}, map publics ${nmap}`);
