// Records the game files a first run reads (startup, then loading the first
// autosave) and writes them in order to web/preload.txt, which web/shell.html
// prefetches. Files the game writes (saves, the log, the profile) are left out.
//   node tools/port/record-preload.mjs [--url U] [--out web/preload.txt]
import { chromium } from '../boxedwine/node_modules/playwright-core/index.mjs';
import fs from 'node:fs';

const args = process.argv.slice(2);
const opt = { url: 'http://localhost:8081/golf.html', out: 'web/preload.txt' };
for (let i = 0; i < args.length; i++) {
  if (args[i] === '--url') opt.url = args[++i];
  else if (args[i] === '--out') opt.out = args[++i];
}
const loadingScreens = fs.readdirSync('game/Interface/Loading_Screens').filter((f) => /\.pcx$/i.test(f))
  .sort().map((f) => 'Interface/Loading_Screens/' + f);
const writable = /^(logfile\.txt|top10\.sve|Saved Games\/|Themes\/Default\.pro)/i;

const browser = await chromium.launch({ channel: 'chrome', headless: true });
const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
// A fresh context has no IndexedDB saves, so this is what a first visit reads.
const files = [], seen = new Set();
page.on('console', (m) => {
  const t = m.text();
  if (!/^fetch game\//.test(t)) return;
  const name = t.slice('fetch game/'.length).split('/').map(decodeURIComponent).join('/');
  // The loading screens are picked at random, so the first one brings in all.
  const names = /^Interface\/Loading_Screens\//.test(name) ? loadingScreens : [name];
  for (const n of names) if (!seen.has(n) && !writable.test(n)) { seen.add(n); files.push(n); }
});
const t0 = Date.now();
await page.goto(opt.url + (opt.url.includes('?') ? '&' : '?') + 'novideos&noprefetch');
// Same clicks as docs/porting.md: load game, first autosave, OK.
for (const [x, y, at] of [[103, 87, 6000], [450, 122, 9000], [632, 553, 12000]]) {
  await page.waitForTimeout(at - (Date.now() - t0));
  await page.mouse.click(x, y);
}
await page.waitForTimeout(10000);
await browser.close();
fs.writeFileSync(opt.out, files.join('\n') + '\n');
console.log(`${files.length} files -> ${opt.out}`);
