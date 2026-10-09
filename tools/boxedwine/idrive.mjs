import { chromium } from 'playwright-core';
import fs from 'node:fs';
const [,, query, tag = 'live'] = process.argv;
const out = new URL(`../../build/boxedwine/runs/${tag}`, import.meta.url).pathname; fs.mkdirSync(out, { recursive: true });
const cmdFile = `${out}/cmd`; fs.writeFileSync(cmdFile, '');
const log = fs.createWriteStream(`${out}/console.log`);
const browser = await chromium.launch({ channel: 'chrome', headless: true });
const page = await browser.newPage({ viewport: { width: 1100, height: 900 } });
page.on('console', m => log.write(`[${m.type()}] ${m.text()}\n`));
page.on('pageerror', e => log.write(`[pageerror] ${e.message}\n`));
await page.goto(`http://localhost:8080/boxedwine.html?${query}`);
let n = 0;
for (;;) {
  await page.waitForTimeout(500);
  const lines = fs.readFileSync(cmdFile, 'utf8').split('\n').filter(Boolean); if (!lines.length) continue;
  fs.writeFileSync(cmdFile, '');
  for (const l of lines) {
    const [c, a, b] = l.split(' ');
    if (c === 'click') { await page.mouse.click(+a, +b); log.write(`[click] ${a},${b}\n`); }
    else if (c === 'key') await page.keyboard.press(a);
    else if (c === 'wait') await page.waitForTimeout(+a * 1000);
    else if (c === 'shot') await page.screenshot({ path: `${out}/${a || 's' + n++}.png`, clip: { x: 150, y: 31, width: 800, height: 600 } });
    else if (c === 'quit') { await browser.close(); process.exit(0); }
  }
  fs.writeFileSync(`${out}/ack`, String(Date.now()));
}
