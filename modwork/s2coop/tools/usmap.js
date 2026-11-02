// Lê um .usmap (v4, sem compressão) e imprime classes: node usmap.js <arquivo> <regex>
const fs = require('fs');
const [,, file, re = '.'] = process.argv;
const b = fs.readFileSync(file); let p = 0;
const u8 = () => b[p++], u16 = () => { const v = b.readUInt16LE(p); p += 2; return v; };
const i32 = () => { const v = b.readInt32LE(p); p += 4; return v; }, u32 = () => { const v = b.readUInt32LE(p); p += 4; return v; };
if (u16() !== 0x30C4) throw 'magic';
const ver = u8(); if (ver >= 1 && u32()) throw 'versioning não suportado';
if (u8()) throw 'compressão não suportada';
u32(); u32();
const names = []; for (let n = u32(); n--;) { const l = ver >= 2 ? u16() : u8(); names.push(b.toString('utf8', p, p + l)); p += l; }
const N = () => { const i = i32(); if (i < -1 || i >= names.length) throw new Error('nome fora da faixa ' + i + ' pos ' + p); return i < 0 ? '' : names[i]; };
for (let n = u32(); n--;) { N(); for (let c = ver >= 3 ? u16() : u8(); c--;) { if (ver >= 4) p += 8; N(); } }
const T = ['Byte','Bool','Int','Float','Object','Name','Delegate','Double','Array','Struct','Str','Text','Interface','MulticastDelegate','WeakObject','LazyObject','AssetObject','SoftObject','UInt64','UInt32','UInt16','Int64','Int16','Int8','Map','Set','Enum','FieldPath','Optional','Utf8Str','AnsiStr'];
const type = () => { const k = u8(), t = T[k]; if (!t) throw new Error('tipo ' + k + ' pos ' + p);
  if (t === 'Enum') { const i = type(); return `Enum<${N()}:${i}>`; } if (t === 'Struct') return `Struct<${N()}>`;
  if (t === 'Array' || t === 'Set' || t === 'Optional') return `${t}<${type()}>`; if (t === 'Map') return `Map<${type()},${type()}>`; return t; };
const out = [], all = []; const rx = new RegExp(re, 'i'); let cur = '', curProps = [], curProp = '';
try { for (let n = u32(); n--;) { const name = N(), sup = N(); cur = name; u16(); const cnt = u16(); const props = curProps = [];
  for (let c = cnt; c--;) { u16(); const arr = u8(); const pn = N(); curProp = pn; props.push(`  ${pn}${arr > 1 ? '[' + arr + ']' : ''}: ${type()}`); }
  const s = `${name} : ${sup}\n${props.join('\n')}`; all.push(s); if (rx.test(name)) out.push(s); }
} catch (e) { console.error('ERRO em', cur, e.message, '\nprops lidas:\n' + curProps.join('\n') + '\nprop em leitura: ' + curProp); }
console.log(out.join('\n\n'));
