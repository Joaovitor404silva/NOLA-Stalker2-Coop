// Post-process the raw dump (addr\b_*.c + .idx, never modified) into the browsable tree + index.tsv, applying the CURRENT names
// (static_names.tsv, then C:\modding\s2names\names.tsv overriding). Cheap (no Ghidra), re-run whenever names.tsv grows.
//   - each function's name = names map, else Ghidra's name; FUN_<va> tokens in every body are replaced by mapped names
//   - functions whose final name has a namespace (A::B::f) -> ns\A__B.c ; others -> bucket\b_<rva>.c
//   - index.tsv: rva, name, file, line, size, callers   (+ failures.tsv from *.fail)
const fs = require('fs'), path = require('path');
const ROOT = 'C:\\modding\\s2decomp', ADDR = path.join(ROOT, 'addr'), BASE = 0x140000000;
const names = new Map();
for (const f of [path.join(ROOT, 'static_names.tsv'), 'C:\\modding\\s2names\\names.tsv']) {
  if (!fs.existsSync(f)) continue;
  for (const l of fs.readFileSync(f, 'utf8').split(/\r?\n/)) {
    const p = l.split('\t'); if (p.length < 3 || p[0] === 'rva' || !p[2].trim()) continue;
    const r = parseInt(p[0].replace(/^0x/, ''), 16); if (!isNaN(r)) names.set(r, p[2].trim());
  }
}
const tmpRoot = path.join(ROOT, '_build');
fs.rmSync(tmpRoot, { recursive: true, force: true });
fs.mkdirSync(path.join(tmpRoot, 'ns'), { recursive: true }); fs.mkdirSync(path.join(tmpRoot, 'bucket'), { recursive: true });
const nsLines = new Map(), nsBuf = new Map(); let bufBytes = 0;
const flush = () => { for (const [n, b] of nsBuf) fs.appendFileSync(path.join(tmpRoot, 'ns', n + '.c'), b.join('')); nsBuf.clear(); bufBytes = 0; };
const lc = s => { let n = 0; for (let i = 0; i < s.length; i++) if (s.charCodeAt(i) === 10) n++; return n; };
const fixBody = s => s.replace(/\bFUN_(1[0-9a-f]{8})\b/g, (m, h) => { const n = names.get(parseInt(h, 16) - BASE); return n ? n.replace(/::/g, '::') : m; });
const index = ['rva\tname\tfile\tline\tsize\tcallers']; const fails = ['rva\tname\treason'];
const idxFiles = fs.readdirSync(ADDR).filter(f => f.endsWith('.idx')).sort();
for (const fi of idxFiles) {
  const tag = fi.slice(0, -4);
  const rows = fs.readFileSync(path.join(ADDR, fi), 'utf8').split('\n').filter(Boolean).map(l => l.split('\t'));
  const src = fs.readFileSync(path.join(ADDR, tag + '.c'), 'utf8').split('\n');
  if (fs.existsSync(path.join(ADDR, tag + '.fail'))) fails.push(...fs.readFileSync(path.join(ADDR, tag + '.fail'), 'utf8').split('\n').filter(Boolean));
  let bucketOut = [], bucketLine = 1;
  for (let i = 0; i < rows.length; i++) {
    const [rvaH, gname, , lineS, size, callers] = rows[i];
    const start = +lineS - 1, end = i + 1 < rows.length ? +rows[i + 1][3] - 1 : src.length;
    const rva = parseInt(rvaH, 16), name = names.get(rva) || gname;
    let chunk = src.slice(start, end).join('\n') + '\n';
    chunk = chunk.replace(/^\/\/ ==== \S+ \S+/, `// ==== ${rvaH} ${name}`);
    chunk = fixBody(chunk);
    const k = name.lastIndexOf('::');
    if (k > 0) {
      const ns = name.slice(0, k).replace(/::/g, '__').replace(/[^A-Za-z0-9_]/g, '_').slice(0, 120);
      const ln = nsLines.get(ns) || 1;
      index.push(`${rvaH}\t${name}\tns/${ns}.c\t${ln}\t${size}\t${callers}`);
      nsLines.set(ns, ln + lc(chunk));
      if (!nsBuf.has(ns)) nsBuf.set(ns, []); nsBuf.get(ns).push(chunk); bufBytes += chunk.length;
    } else {
      index.push(`${rvaH}\t${name}\tbucket/${tag}.c\t${bucketLine}\t${size}\t${callers}`);
      bucketOut.push(chunk); bucketLine += lc(chunk);
    }
  }
  if (bucketOut.length) fs.writeFileSync(path.join(tmpRoot, 'bucket', tag + '.c'), bucketOut.join(''));
  if (bufBytes > 200e6) flush();
}
flush();
fs.writeFileSync(path.join(tmpRoot, 'index.tsv'), index.join('\n') + '\n');
fs.writeFileSync(path.join(tmpRoot, 'failures.tsv'), fails.join('\n') + '\n');
// swap in
for (const d of ['ns', 'bucket']) { fs.rmSync(path.join(ROOT, d), { recursive: true, force: true }); fs.renameSync(path.join(tmpRoot, d), path.join(ROOT, d)); }
for (const f of ['index.tsv', 'failures.tsv']) fs.renameSync(path.join(tmpRoot, f), path.join(ROOT, f));
fs.rmSync(tmpRoot, { recursive: true, force: true });
console.log(`buckets=${idxFiles.length} functions=${index.length - 1} namespaces=${nsLines.size} failures=${fails.length - 1} names=${names.size}`);
