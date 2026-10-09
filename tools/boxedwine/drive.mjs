import { chromium } from 'playwright-core';
import fs from 'node:fs';
const [,, query, secs = '120', tag = 'run', clickSpec = ''] = process.argv;
const clicks = clickSpec ? clickSpec.split(';').map(c => { const [xy, t] = c.split('@'); const [x, y] = xy.split(',').map(Number); return { x, y, t: +t * 1000, done: false }; }) : [];
const out = new URL(`../../build/boxedwine/runs/${tag}`, import.meta.url).pathname; fs.mkdirSync(out, { recursive: true });
const log = fs.createWriteStream(`${out}/console.log`);
const browser = await chromium.launch({ channel: 'chrome', headless: true, args: ['--use-angle=metal', '--enable-unsafe-swiftshader', '--autoplay-policy=no-user-gesture-required'] });
const page = await browser.newPage({ viewport: { width: 1100, height: 900 } });
page.on('console', m => log.write(`[${m.type()}] ${m.text()}\n`));
page.on('pageerror', e => log.write(`[pageerror] ${e.message}\n`));
await page.goto(`http://localhost:8080/boxedwine.html?${query}`);
const t0 = Date.now(); let i = 0;
while (Date.now() - t0 < +secs * 1000) {
  for (let k = 0; k < 15; k++) {
    await page.waitForTimeout(1000);
    for (const c of clicks) if (!c.done && Date.now() - t0 >= c.t) { c.done = true; await page.mouse.click(c.x, c.y); log.write(`[click] ${c.x},${c.y}\n`); await page.waitForTimeout(1500); await page.screenshot({ path: `${out}/click-${c.x}-${c.y}.png` }); }
  }
  await page.screenshot({ path: `${out}/shot${String(i++).padStart(2, '0')}.png` });
}
await browser.close(); log.end(); console.log('done', out);
