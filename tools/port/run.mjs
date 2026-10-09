// Runs the translated game in headless Chrome and saves screenshots.
//   node tools/port/run.mjs [--url U] [--ms N] [--shots DIR] [--every MS] [--click x,y@ms ...]
// Console output is printed with the elapsed time. Exits after --ms (default 20000).
import { chromium } from '../boxedwine/node_modules/playwright-core/index.mjs';
import fs from 'node:fs';

const args = process.argv.slice(2);
const opt = { url: 'http://localhost:8081/golf.html?novideos', ms: 20000, shots: '/tmp/golf-shots', every: 2000, clicks: [], keys: [], pause: [] };
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === '--url') opt.url = args[++i];
  else if (a === '--ms') opt.ms = +args[++i];
  else if (a === '--shots') opt.shots = args[++i];
  else if (a === '--every') opt.every = +args[++i];
  else if (a === '--click' || a === '--dblclick') { const [xy, t] = args[++i].split('@'); const [x, y] = xy.split(',').map(Number); opt.clicks.push({ x, y, t: +t, dbl: a === '--dblclick' }); }
  else if (a === '--pause') opt.pause.push(+args[++i]);
  else if (a === '--profile') { const [from, to] = args[++i].split('-').map(Number); opt.profile = { from, to }; }
  else if (a === '--eval') opt.evals = [...(opt.evals || []), args[++i]];
  else if (a === '--move') { const [xy, t] = args[++i].split('@'); const [x, y] = xy.split(',').map(Number); opt.keys.push({ move: true, x, y, t: +t }); }
  else if (a === '--viewport') { const [w, h] = args[++i].split('x').map(Number); opt.viewport = { width: w, height: h }; }
  else if (a === '--reload') opt.keys.push({ k: null, reload: true, t: +args[++i] });
  else if (a === '--key') { const [k, t] = args[++i].split('@'); opt.keys.push({ k, t: +t }); }
}
fs.mkdirSync(opt.shots, { recursive: true });
const browser = await chromium.launch({ channel: 'chrome', headless: true });
const page = await browser.newPage({ viewport: opt.viewport || { width: 800, height: 600 } });
const t0 = Date.now();
const stamp = () => ((Date.now() - t0) / 1000).toFixed(1).padStart(6);
page.on('console', m => console.log(`${stamp()} [${m.type()}] ${m.text()}`));
page.on('pageerror', e => console.log(`${stamp()} [pageerror] ${e.message}\n${e.stack || ''}`));
page.on('response', r => { if (r.status() >= 400) console.log(`${stamp()} [http ${r.status()}] ${r.url()}`); });
const cdp = await page.context().newCDPSession(page);
await cdp.send('Debugger.enable');
cdp.on('Debugger.paused', async ev => {
  console.log(`${stamp()} [paused] ` + ev.callFrames.slice(0, 25).map(f => f.functionName || '?').join(' <- '));
  await cdp.send('Debugger.resume').catch(() => {});
});
await page.goto(opt.url);
const events = [...opt.clicks.map(c => ({ ...c, kind: 'click' })), ...opt.keys.map(k => ({ ...k, kind: 'key' }))].sort((a, b) => a.t - b.t);
let n = 0, next = opt.every;
while (Date.now() - t0 < opt.ms) {
  const el = Date.now() - t0;
  while (events.length && events[0].t <= el) {
    const e = events.shift();
    if (e.kind === 'click') {
      await page.mouse.move(e.x - 3, e.y - 3); await new Promise(r => setTimeout(r, 100));
      await page.mouse.move(e.x, e.y); await new Promise(r => setTimeout(r, 150));
      await page.mouse.click(e.x, e.y, { clickCount: e.dbl ? 2 : 1, delay: 80 });
      if (e.dbl) await page.mouse.dblclick(e.x, e.y, { delay: 60 });
      console.log(`${stamp()} [${e.dbl ? 'dblclick' : 'click'}] ${e.x},${e.y}`);
    }
    else if (e.move) { await page.mouse.move(e.x, e.y, { steps: 5 }); console.log(`${stamp()} [move] ${e.x},${e.y}`); }
    else if (e.reload) { await page.reload(); console.log(`${stamp()} [reload]`); }
    else { await page.keyboard.press(e.k); console.log(`${stamp()} [key] ${e.k}`); }
  }
  if (opt.profile && !opt.profile.started && el >= opt.profile.from) {
    opt.profile.started = true; await cdp.send('Profiler.enable'); await cdp.send('Profiler.start'); console.log(stamp() + ' [profile start]');
  }
  if (opt.profile && opt.profile.started && !opt.profile.done && el >= opt.profile.to) {
    opt.profile.done = true;
    const { profile } = await cdp.send('Profiler.stop');
    // self time per function
    const self = new Map(), byId = new Map(profile.nodes.map(n => [n.id, n]));
    const dt = profile.timeDeltas; let total = 0;
    profile.samples.forEach((id, k) => { const nd = byId.get(id); const name = nd.callFrame.functionName || '(anon)'; self.set(name, (self.get(name) || 0) + dt[k]); total += dt[k]; });
    console.log(stamp() + ' [profile] self time:');
    [...self.entries()].sort((a, b) => b[1] - a[1]).slice(0, 30).forEach(([nm, t]) => console.log(`   ${(100 * t / total).toFixed(1).padStart(5)}%  ${nm}`));
  }
  while (opt.pause.length && opt.pause[0] <= el) { opt.pause.shift(); console.log(stamp() + ' [pause requested]'); cdp.send('Debugger.pause').then(() => console.log(stamp() + ' [pause sent]')).catch(e => console.log('pause failed ' + e)); }
  if (el >= next) {
    const f = `${opt.shots}/shot-${String(n++).padStart(3, '0')}.png`;
    await page.screenshot({ path: f }).catch(() => {});
    next += opt.every;
  }
  await new Promise(r => setTimeout(r, 100));
}
await page.screenshot({ path: `${opt.shots}/final.png` }).catch(() => {});
for (const e of opt.evals || []) console.log(stamp() + ' [eval] ' + e + ' => ' + JSON.stringify(await page.evaluate(e).catch(x => 'error: ' + x)));
await browser.close();
