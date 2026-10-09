#!/usr/bin/env node
// Headless check of the Pages build: fresh browser profile, pick the given
// game files on the setup screen, wait for the install, then screenshot the
// game. Serve build/pages/site first (node tools/boxedwine/serve.mjs build/pages/site 8090).
//   node tools/pages/test-install.mjs <out.png> <file>... [--url=http://localhost:8090/index.html] [--wait=40]
import { chromium } from '../boxedwine/node_modules/playwright-core/index.mjs';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
const args = process.argv.slice(2);
const opt = Object.fromEntries(args.filter((a) => a.startsWith('--')).map((a) => a.slice(2).split('=')));
const [out, ...files] = args.filter((a) => !a.startsWith('--'));
const url = opt.url || 'http://localhost:8090/index.html';
const profile = opt.profile || fs.mkdtempSync(path.join(os.tmpdir(), 'simgolf-pages-'));
const ctx = await chromium.launchPersistentContext(profile, { channel: 'chrome', headless: true, viewport: { width: 1024, height: 768 } });
const page = ctx.pages()[0] || await ctx.newPage();
const t0 = Date.now();
page.on('console', (m) => { const t = m.text(); if (!/^fetch /.test(t)) console.log(`[${((Date.now() - t0) / 1000).toFixed(1)}] ${t}`); });
page.on('pageerror', (e) => console.log('pageerror', e.message));
await page.goto(url);
if (files.length) {
  await page.waitForSelector('#setup.on', { timeout: 10000 });
  await page.setInputFiles('#pick', files);
  let last = '';
  for (;;) {
    const s = await page.evaluate(() => ({ msg: document.getElementById('msg').textContent, bad: document.getElementById('msg').className, on: document.getElementById('setup').classList.contains('on') }));
    if (s.msg !== last) { console.log(`[${((Date.now() - t0) / 1000).toFixed(1)}] setup: ${s.msg}`); last = s.msg; }
    if (s.bad === 'bad') { await page.screenshot({ path: out }); await ctx.close(); process.exit(1); }
    if (!s.on) break;
    await page.waitForTimeout(1000);
  }
  console.log(`installed in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
}
await page.waitForTimeout((+opt.wait || 40) * 1000);
// --clicks="x,y;d:x,y" (page pixels, d: double-click), 8 s apart, then a final screenshot
for (const c of (opt.clicks || '').split(';').filter(Boolean)) {
  const dbl = c.startsWith('d:');
  const [x, y] = c.replace('d:', '').split(',').map(Number);
  await page.mouse.move(x, y); await page.waitForTimeout(300);
  await page.mouse.click(x, y, { clickCount: 1, delay: 80 });
  if (dbl) await page.mouse.dblclick(x, y, { delay: 60 });
  await page.waitForTimeout(8000);
}
await page.screenshot({ path: out });
console.log('profile', profile);
await ctx.close();
