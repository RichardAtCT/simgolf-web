#!/usr/bin/env node
// Headless sound check of the Pages build: install (or Play), click through
// the intro with real input, then dump the Web Audio state.
//   node tools/pages/sound-check.mjs <file>... [--url=...] [--wait=40]
import { chromium } from '../boxedwine/node_modules/playwright-core/index.mjs';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
const args = process.argv.slice(2);
const opt = Object.fromEntries(args.filter((a) => a.startsWith('--')).map((a) => a.slice(2).split('=')));
const files = args.filter((a) => !a.startsWith('--'));
const url = opt.url || 'http://localhost:8090/index.html';
const profile = opt.profile || fs.mkdtempSync(path.join(os.tmpdir(), 'simgolf-snd-'));
const ctx = await chromium.launchPersistentContext(profile, { channel: 'chrome', headless: true, viewport: { width: 1024, height: 768 } });
const page = ctx.pages()[0] || await ctx.newPage();
const t0 = Date.now();
page.on('console', (m) => { const t = m.text(); if (/sound|audio|video|error/i.test(t)) console.log(`[${((Date.now() - t0) / 1000).toFixed(1)}] ${t}`); });
page.on('pageerror', (e) => console.log('pageerror', e.message));
await page.goto(url);
if (files.length) {
  await page.waitForSelector('#setup.on', { timeout: 20000 });
  await page.setInputFiles('#pick', files);
  for (;;) {
    const s = await page.evaluate(() => ({ msg: document.getElementById('msg').textContent, bad: document.getElementById('msg').className, on: document.getElementById('setup').classList.contains('on') }));
    if (s.bad === 'bad') { console.log('setup failed', s.msg); process.exit(1); }
    if (!s.on) break;
    await page.waitForTimeout(1000);
  }
  console.log('installed');
} else {
  await page.waitForSelector('#play', { state: 'visible', timeout: 20000 });
  await page.click('#play');
}
const dump = () => page.evaluate(() => {
  const S = Module.soundDebug;
  if (!S) return 'no soundDebug';
  const st = {};
  for (const s of Object.values(S.sounds)) st[s.state] = (st[s.state] || 0) + 1;
  const failed = Object.values(S.sounds).filter((s) => s.state === 'failed').map((s) => s.url).slice(0, 15);
  return { ctx: S.ctx && S.ctx.state, gain: S.master && S.master.gain.value, registered: Object.keys(S.sounds).length, st, failed,
           voices: Object.values(S.sounds).reduce((n, s) => n + s.voices.length, 0) };
});
await page.waitForTimeout(8000);
console.log('during intro', JSON.stringify(await dump()));
await page.mouse.click(500, 400);   // skip intro
await page.waitForTimeout(+(opt.wait || 20) * 1000);
console.log('after skip', JSON.stringify(await dump()));
for (const [x, y] of [[512, 300], [512, 350], [512, 400]]) { await page.mouse.move(x, y); await page.mouse.click(x, y); await page.waitForTimeout(1500); }
console.log('after clicks', JSON.stringify(await dump()));
await page.screenshot({ path: opt.out || path.join(profile, 'shot.png') });
console.log('profile', profile);
await ctx.close();
