// Open a URL in headless Chrome, optionally move the mouse, and save a screenshot.
//   node tools/boxedwine/shot.mjs <url> <out.png> [waitMs] [x,y]
import { chromium } from 'playwright-core';
const [,, url, out, wait = '3000', move] = process.argv;
const browser = await chromium.launch({ channel: 'chrome', headless: true });
const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
page.on('console', m => console.log(`[${m.type()}] ${m.text()}`));
page.on('pageerror', e => console.log(`[pageerror] ${e.message}`));
await page.goto(url);
await page.waitForTimeout(+wait);
if (move) { const [x, y] = move.split(',').map(Number); await page.mouse.move(x, y); await page.mouse.click(x, y); await page.waitForTimeout(500); }
await page.screenshot({ path: out });
await browser.close();
