import http from 'node:http'; import fs from 'node:fs'; import path from 'node:path';
const root = process.argv[2], port = +process.argv[3] || 8080;
const types = {'.html':'text/html','.js':'text/javascript','.wasm':'application/wasm','.css':'text/css','.zip':'application/zip'};
// POST /log appends the body to <root>/../browser.log (the page's crash reports).
const logFile = path.join(root, '..', 'browser.log');
// /saves/ keeps the files the game writes (saves, the player profile) so they
// follow the player between browsers and machines (src/port/fs.c syncs them):
// GET /saves/ lists {files: {path: mtime}, deleted: {path: mtime}}; GET, PUT
// and DELETE /saves/<path> read, store and delete one file. Times are this
// server's clock, so browsers with different clocks still agree.
const savesDir = process.env.SAVES_DIR || path.join(root, '..', 'saves');
const tombFile = path.join(savesDir, '.deleted.json');
const readTombs = () => { try { return JSON.parse(fs.readFileSync(tombFile, 'utf8')); } catch { return {}; } };
const writeTombs = (t) => { fs.mkdirSync(savesDir, { recursive: true }); fs.writeFileSync(tombFile, JSON.stringify(t)); };
function saveRel(p) {
  let parts;
  try { parts = p.split('/').map(decodeURIComponent); } catch { return null; }
  if (!parts.length || parts.length > 4) return null;
  for (const s of parts) if (!s || s.startsWith('.') || /[\\\0]/.test(s) || s.length > 200) return null;
  return parts.join('/');
}
function listSaves(dir, rel, out) {
  let names = [];
  try { names = fs.readdirSync(dir, { withFileTypes: true }); } catch { return out; }
  for (const d of names) {
    if (d.name.startsWith('.')) continue;
    const r = rel ? rel + '/' + d.name : d.name;
    if (d.isDirectory()) listSaves(path.join(dir, d.name), r, out);
    else if (d.isFile()) out[r] = Math.floor(fs.statSync(path.join(dir, d.name)).mtimeMs);
  }
  return out;
}
function json(res, code, obj) {
  res.writeHead(code, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
  res.end(JSON.stringify(obj));
}
function saves(req, res, sub) {
  if (sub === '') {
    if (req.method !== 'GET') return json(res, 405, {});
    return json(res, 200, { files: listSaves(savesDir, '', {}), deleted: readTombs() });
  }
  const rel = saveRel(sub);
  if (!rel) return json(res, 400, { error: 'bad path' });
  const file = path.join(savesDir, rel);
  if (req.method === 'GET') {
    return fs.readFile(file, (err, buf) => {
      if (err) { res.writeHead(404); return res.end(); }
      res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Cache-Control': 'no-store' });
      res.end(buf);
    });
  }
  if (req.method === 'PUT') {
    const chunks = []; let size = 0;
    req.on('data', (d) => { size += d.length; if (size <= 32 << 20) chunks.push(d); });
    req.on('end', () => {
      if (size > 32 << 20) return json(res, 413, {});
      fs.mkdirSync(path.dirname(file), { recursive: true });
      const tmp = path.join(path.dirname(file), '.' + path.basename(file) + '.tmp');
      fs.writeFileSync(tmp, Buffer.concat(chunks));
      const now = Date.now();
      fs.utimesSync(tmp, now / 1000, now / 1000);
      fs.renameSync(tmp, file);
      const t = readTombs();
      if (rel in t) { delete t[rel]; writeTombs(t); }
      json(res, 200, { mtime: Math.floor(fs.statSync(file).mtimeMs) });
    });
    return;
  }
  if (req.method === 'DELETE') {
    try { fs.unlinkSync(file); } catch {}
    const t = readTombs(), now = Date.now();
    t[rel] = now;
    writeTombs(t);
    return json(res, 200, { mtime: now });
  }
  json(res, 405, {});
}

http.createServer((req, res) => {
  const pathname = new URL(req.url, 'http://x').pathname;
  if (pathname.startsWith('/saves/')) return saves(req, res, pathname.slice(7));
  if (req.method === 'POST' && req.url === '/log') {
    let body = '';
    req.on('data', (d) => { if (body.length < 1 << 20) body += d; });
    req.on('end', () => { fs.appendFile(logFile, `==== ${new Date().toISOString()}\n${body}\n`, () => {}); res.writeHead(204); res.end(); });
    return;
  }
  const p = path.join(root, decodeURIComponent(new URL(req.url, 'http://x').pathname));
  fs.stat(p, (err, st) => {
    if (err || !st.isFile()) { res.writeHead(404); return res.end(); }
    res.writeHead(200, {'Content-Type': types[path.extname(p)] || 'application/octet-stream', 'Content-Length': st.size,
      'Cross-Origin-Opener-Policy':'same-origin','Cross-Origin-Embedder-Policy':'require-corp','Cache-Control':'no-store'});
    fs.createReadStream(p).pipe(res);
  });
}).listen(port, () => console.log('serving', root, 'on', port));
