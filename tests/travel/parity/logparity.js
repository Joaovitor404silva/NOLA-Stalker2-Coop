const fs = require('fs');
const lua = fs.readFileSync(process.argv[2], 'latin1').split('\n');
const cpp = fs.readFileSync(process.argv[3], 'latin1');
const prefixes = process.argv.slice(4);
const seen = new Map();
for (let i = 0; i < lua.length; i++) {
  const line = lua[i];
  const re = /"(NOLA (?:[a-z0-9_]+)[^"]*)"/g;
  let m;
  while ((m = re.exec(line))) {
    const s = m[1];
    if (!prefixes.some(p => s.startsWith('NOLA ' + p))) continue;
    const key = s.split(/%[-0-9.]*[a-z]/)[0].replace(/\\"/g, '"');
    if (!seen.has(key)) seen.set(key, i + 1);
  }
}
let miss = 0, ok = 0;
for (const [k, ln] of seen) {
  const probe = k.replace(/"/g, '\\"');
  if (cpp.includes(k) || cpp.includes(probe)) ok++;
  else { miss++; console.log(`MISSING L${ln}: ${k}`); }
}
console.log(`ok=${ok} missing=${miss}`);
