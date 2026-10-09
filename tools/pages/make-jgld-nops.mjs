#!/usr/bin/env node
// Writes web/pages/jgld-nops.json: the byte runs prepare-jgld.sh's NopCalls.java
// replaced with NOPs (debug-build stack fills and _chkesp calls), found by
// comparing jgld.dll with the exported image. The browser installer applies
// them so its image matches build/gen/images/jgld.bin byte for byte.
//   node tools/pages/make-jgld-nops.mjs <jgld.dll> [build/gen/images/jgld.bin]
import fs from 'node:fs';
import { mapPE } from './pe-image.mjs';
const [dll, ref = 'build/gen/images/jgld.bin'] = process.argv.slice(2);
const r = fs.readFileSync(ref), m = mapPE(fs.readFileSync(dll));
if (r.length !== m.length) throw new Error(`size ${m.length} != ${r.length}`);
const runs = [];
let s = -1;
for (let i = 0; i <= r.length; i++) {
  const d = i < r.length && r[i] !== m[i];
  if (d && r[i] !== 0x90) throw new Error(`non-NOP difference at 0x${i.toString(16)}`);
  if (d && s < 0) s = i;
  if (!d && s >= 0) { runs.push([s, i - s]); s = -1; }
}
fs.writeFileSync('web/pages/jgld-nops.json', JSON.stringify(runs) + '\n');
console.log(runs.length, 'runs');
