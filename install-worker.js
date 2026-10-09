// In-browser installer for the GitHub Pages build (web/pages/index.html).
//
// Takes the player's own copy of the game: the SimGolf disc image (.iso, or a
// raw .bin/.img/.mdf) and the v1.02 no-CD golf.exe (bare, or in its .zip). It
// reads the disc, unpacks the InstallShield cabinets with unshield (compiled
// to wasm, src/install), and writes the game folder into the origin private
// file system (OPFS) under simgolf/:
//   simgolf/game/...          the installed game files
//   simgolf/image/golf.bin    golf.exe mapped as the loader maps it
//   simgolf/image/jgld.bin    jgld.dll mapped, with our NOP patches
//   simgolf/installed.json    {version, files: [[path, size], ...]}
// Nothing leaves the player's machine.
//
// Messages in: {files: [File, ...]}. Out: {progress, text}, {done}, {error}.

importScripts('unshield.js');

const VERSION = 1;
const NOCD_SHA1 = '6f05a3ae3b5050ed82dc0010c03842a2e0ab166b';   // golf.exe v1.02, no-CD
const GROUP = 'Program Files (ENGLISH)';
// Loose files in the disc root that the installer copies next to the game
const ROOT_EXTRA = /^(autosavebase\.pcx|autosavebuttons\.pcx|jackal\.pcx|jackal\.txt)$/i;

const post = (m) => self.postMessage(m);
const progress = (p, text) => post({ progress: p, text });

// ---------- PE images (same as tools/pages/pe-image.mjs) ----------

function mapPE(buf) {
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const pe = dv.getUint32(0x3c, true);
  const nsec = dv.getUint16(pe + 6, true), optSize = dv.getUint16(pe + 20, true);
  const opt = pe + 24, size = dv.getUint32(opt + 56, true), sizeHdr = dv.getUint32(opt + 60, true);
  const img = new Uint8Array(size);
  img.set(buf.subarray(0, Math.min(sizeHdr, size)));
  for (let i = 0; i < nsec; i++) {
    const s = opt + optSize + i * 40;
    const vsz = dv.getUint32(s + 8, true), va = dv.getUint32(s + 12, true);
    const rsz = dv.getUint32(s + 16, true), rp = dv.getUint32(s + 20, true);
    const n = Math.min(rsz, vsz || rsz, size - va);
    if (n > 0) img.set(buf.subarray(rp, rp + n), va);
  }
  return img;
}

async function sha1(buf) {
  const h = new Uint8Array(await crypto.subtle.digest('SHA-1', buf));
  return Array.from(h, (b) => b.toString(16).padStart(2, '0')).join('');
}

// ---------- zip (just enough to pull golf.exe out of the no-CD zip) ----------

async function zipEntries(file) {
  const tailLen = Math.min(file.size, 65557);
  const tail = new DataView(await file.slice(file.size - tailLen).arrayBuffer());
  let eocd = -1;
  for (let i = tailLen - 22; i >= 0; i--) if (tail.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
  if (eocd < 0) throw new Error(file.name + ' is not a zip file');
  const n = tail.getUint16(eocd + 10, true);
  const cdSize = tail.getUint32(eocd + 12, true), cdOff = tail.getUint32(eocd + 16, true);
  const cd = new DataView(await file.slice(cdOff, cdOff + cdSize).arrayBuffer());
  const out = [];
  for (let p = 0, i = 0; i < n; i++) {
    const method = cd.getUint16(p + 10, true), csize = cd.getUint32(p + 20, true);
    const nameLen = cd.getUint16(p + 28, true), extra = cd.getUint16(p + 30, true), comment = cd.getUint16(p + 32, true);
    const local = cd.getUint32(p + 42, true);
    const name = new TextDecoder().decode(new Uint8Array(cd.buffer, p + 46, nameLen));
    out.push({ name, method, csize, local });
    p += 46 + nameLen + extra + comment;
  }
  return out;
}

async function zipRead(file, e) {
  if (e.csize > 64 << 20) throw new Error(e.name + ' is too big to unzip in memory');
  const h = new DataView(await file.slice(e.local, e.local + 30).arrayBuffer());
  const start = e.local + 30 + h.getUint16(26, true) + h.getUint16(28, true);
  const data = file.slice(start, start + e.csize);
  if (e.method === 0) return new Uint8Array(await data.arrayBuffer());
  if (e.method !== 8) throw new Error('unsupported zip compression in ' + file.name);
  const stream = data.stream().pipeThrough(new DecompressionStream('deflate-raw'));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

// ---------- disc image (ISO 9660, Joliet names when present) ----------

const reader = new FileReaderSync();

function openDisc(file) {
  // Cooked .iso: 2048-byte sectors. Raw .bin/.img/.mdf: 2352-byte sectors
  // with a sync pattern; user data at +16 (mode 1) or +24 (mode 2 form 1).
  const head = new Uint8Array(reader.readAsArrayBuffer(file.slice(0, 16)));
  const sync = head[0] === 0 && head.subarray(1, 11).every((b) => b === 0xff) && head[11] === 0;
  const raw = sync ? 2352 : 2048;
  const dataOff = sync ? (head[15] === 2 ? 24 : 16) : 0;
  const sector = (lba, count = 1) => {
    if (!sync) return new Uint8Array(reader.readAsArrayBuffer(file.slice(lba * 2048, (lba + count) * 2048)));
    const out = new Uint8Array(count * 2048);
    const buf = new Uint8Array(reader.readAsArrayBuffer(file.slice(lba * raw, (lba + count) * raw)));
    for (let i = 0; i < count; i++) out.set(buf.subarray(i * raw + dataOff, i * raw + dataOff + 2048), i * 2048);
    return out;
  };
  // A file's bytes as a Blob without copying, when sectors are cooked
  const blob = (lba, size) => {
    if (!sync) return file.slice(lba * 2048, lba * 2048 + size);
    const parts = [];
    for (let done = 0, s = lba; done < size; s++, done += 2048) {
      const o = s * raw + dataOff;
      parts.push(file.slice(o, o + Math.min(2048, size - done)));
    }
    return new Blob(parts);
  };
  let root = null, joliet = false, volume = '';
  for (let lba = 16; lba < 64; lba++) {
    const d = sector(lba);
    if (String.fromCharCode(...d.subarray(1, 6)) !== 'CD001') break;
    if (d[0] === 255) break;
    if (d[0] === 1 && !root) { root = d.slice(156, 190); volume = String.fromCharCode(...d.subarray(40, 72)).trim(); }
    if (d[0] === 2 && d[88] === 0x25 && d[89] === 0x2f && [0x40, 0x43, 0x45].includes(d[90])) { root = d.slice(156, 190); joliet = true; }
  }
  if (!root) throw new Error(file.name + " doesn't look like a CD image");
  const dv = (a) => new DataView(a.buffer, a.byteOffset, a.byteLength);
  const readDir = (rec) => {
    const r = dv(rec), lba = r.getUint32(2, true), size = r.getUint32(10, true);
    const data = sector(lba, Math.ceil(size / 2048));
    const out = [];
    for (let p = 0; p < size;) {
      const len = data[p];
      if (!len) { p = (Math.floor(p / 2048) + 1) * 2048; continue; }
      const e = data.subarray(p, p + len), nameLen = e[32];
      let name;
      if (joliet) {
        name = '';
        for (let i = 0; i + 1 < nameLen; i += 2) name += String.fromCharCode((e[33 + i] << 8) | e[34 + i]);
      } else name = String.fromCharCode(...e.subarray(33, 33 + nameLen));
      if (!(nameLen === 1 && e[33] <= 1)) {
        name = name.replace(/;1$/, '').replace(/\.$/, '');
        out.push({ name, dir: !!(e[25] & 2), lba: dv(e).getUint32(2, true), size: dv(e).getUint32(10, true), rec: e.slice() });
      }
      p += len;
    }
    return out;
  };
  return { volume, rootEntries: () => readDir(root), readDir, blob, raw: sync };
}

// ---------- OPFS ----------

async function dirFor(root, parts, cache) {
  let d = root, key = '';
  for (const p of parts) {
    key += '/' + p;
    let next = cache.get(key);
    if (!next) { next = await d.getDirectoryHandle(p, { create: true }); cache.set(key, next); }
    d = next;
  }
  return d;
}

async function writeFile(root, path, data, cache) {
  const parts = path.split('/');
  const dir = await dirFor(root, parts.slice(0, -1), cache);
  const fh = await dir.getFileHandle(parts[parts.length - 1], { create: true });
  const h = await fh.createSyncAccessHandle();
  try { h.truncate(0); h.write(data, { at: 0 }); h.flush(); } finally { h.close(); }
}

// ---------- install ----------

// Streams a zip entry into a scratch OPFS file and returns it as a File, so
// a disc image inside the abandonware zip never has to sit in memory.
async function zipToScratch(file, e, scratch, name) {
  const h = new DataView(await file.slice(e.local, e.local + 30).arrayBuffer());
  const start = e.local + 30 + h.getUint16(26, true) + h.getUint16(28, true);
  let stream = file.slice(start, start + e.csize).stream();
  if (e.method === 8) stream = stream.pipeThrough(new DecompressionStream('deflate-raw'));
  else if (e.method !== 0) throw new Error('unsupported zip compression in ' + file.name);
  const fh = await scratch.getFileHandle(name, { create: true });
  const out = await fh.createSyncAccessHandle();
  let at = 0;
  try {
    out.truncate(0);
    const rd = stream.getReader();
    for (;;) {
      const { done, value } = await rd.read();
      if (done) break;
      out.write(value, { at });
      at += value.length;
      progress(0.01, 'Unzipping ' + e.name.split('/').pop() + ' (' + Math.round(at / 1048576) + ' MB)');
    }
    out.flush();
  } finally { out.close(); }
  return fh.getFile();
}

// .7z / .rar (archive.org's rip is a .7z holding BIN/CUE): unpacked with
// 7-Zip compiled to wasm (third_party/7z-wasm), loaded only when needed. The
// archive is read in place through WORKERFS; disc images are moved out of the
// wasm heap into scratch OPFS files as soon as they're extracted.
async function un7z(file, scratch) {
  progress(0.01, 'Unpacking ' + file.name + ' (this takes a minute)…');
  if (!self.SevenZip) importScripts('7zz.umd.js');
  const sz = await SevenZip({ locateFile: (p) => p, print: () => {}, printErr: () => {} });
  sz.FS.mkdir('/in');
  sz.FS.mount(sz.WORKERFS, { files: [file] }, '/in');
  sz.FS.mkdir('/out');
  try { sz.callMain(['x', '/in/' + file.name, '-o/out', '-y', '-bd']); }
  catch (e) { if (!(e && e.name === 'ExitStatus' && e.status === 0)) throw new Error("Couldn't unpack " + file.name + '.'); }
  const out = [];
  const walk = async (dir) => {
    for (const name of sz.FS.readdir(dir)) {
      if (name === '.' || name === '..') continue;
      const p = dir + '/' + name;
      if (sz.FS.isDir(sz.FS.stat(p).mode)) { await walk(p); continue; }
      const lower = name.toLowerCase();
      if (!/\.(iso|bin|img|mdf|exe|zip)$/.test(lower)) { sz.FS.unlink(p); continue; }
      const data = sz.FS.readFile(p);
      sz.FS.unlink(p);
      if (/\.(iso|bin|img|mdf)$/.test(lower)) {
        const fh = await scratch.getFileHandle('x' + out.length + '.' + lower.split('.').pop(), { create: true });
        const h = await fh.createSyncAccessHandle();
        try { h.truncate(0); h.write(data, { at: 0 }); h.flush(); } finally { h.close(); }
        out.push(new File([await fh.getFile()], name));
      } else out.push(new File([data], name));
    }
  };
  await walk('/out');
  return out;
}

// ---------- videos (Bink -> WebM) ----------

// Decodes with FFmpeg's Bink decoder (bink.wasm, src/install/bink_wasm.c),
// encodes VP8 + Opus with WebCodecs and muxes with webm-muxer.
async function binkToWebm(data, onProgress) {
  if (typeof VideoEncoder === 'undefined' || typeof AudioEncoder === 'undefined') throw new Error('no WebCodecs');
  if (!self.createBink) importScripts('bink.js', 'webm-muxer.js');
  const bk = await createBink({ locateFile: (p) => p, print: () => {}, printErr: () => {} });
  bk.FS.writeFile('/v.bik', data);
  const call = (n, r = 'number', a = [], v = []) => bk.ccall(n, r, a, v);
  if (!call('bk_open', 'number', ['string'], ['/v.bik'])) throw new Error("can't open");
  const w = call('bk_width'), h = call('bk_height'), fps = call('bk_fps') || 15;
  const rate = call('bk_sample_rate'), chans = call('bk_channels');
  const layout = call('bk_audio_layout');   // 1 planar float, 2 interleaved float
  const audio = rate > 0 && chans > 0 && layout > 0;
  const OUT_RATE = 48000;   // Opus

  const target = new WebMMuxer.ArrayBufferTarget();
  const muxer = new WebMMuxer.Muxer({
    target,
    video: { codec: 'V_VP8', width: w, height: h, frameRate: fps },
    audio: audio ? { codec: 'A_OPUS', sampleRate: OUT_RATE, numberOfChannels: chans } : undefined,
  });
  let failed = null;
  const venc = new VideoEncoder({ output: (c, m) => muxer.addVideoChunk(c, m), error: (e) => { failed = e; } });
  venc.configure({ codec: 'vp8', width: w, height: h, bitrate: 2500000, framerate: fps });
  let aenc = null;
  if (audio) {
    aenc = new AudioEncoder({ output: (c, m) => muxer.addAudioChunk(c, m), error: (e) => { failed = e; } });
    aenc.configure({ codec: 'opus', sampleRate: OUT_RATE, numberOfChannels: chans, bitrate: 128000 });
  }

  // Audio arrives at the disc's rate; it's collected, resampled to 48 kHz
  // (linear) and encoded at the end.
  const pcm = Array.from({ length: chans }, () => []);
  let pcmLen = 0;
  const yuv = new Uint8Array(w * h * 3 / 2);
  const frameUs = 1e6 / fps;
  let nFrames = 0;
  for (;;) {
    if (failed) throw failed;
    const k = call('bk_next');
    if (k <= 0) break;
    if (k === 1) {
      const heap = bk.HEAPU8;
      let o = 0;
      for (let p = 0; p < 3; p++) {
        const pw = p ? w >> 1 : w, ph = p ? h >> 1 : h;
        const base = call('bk_plane', 'number', ['number'], [p]), ls = call('bk_linesize', 'number', ['number'], [p]);
        for (let y = 0; y < ph; y++, o += pw) yuv.set(heap.subarray(base + y * ls, base + y * ls + pw), o);
      }
      const f = new VideoFrame(yuv, { format: 'I420', codedWidth: w, codedHeight: h, timestamp: Math.round(nFrames * frameUs), duration: Math.round(frameUs) });
      venc.encode(f, { keyFrame: nFrames % (fps * 4) === 0 });
      f.close();
      nFrames++;
      while (venc.encodeQueueSize > 8) await new Promise((r) => setTimeout(r, 2));
      if (nFrames % 15 === 0) onProgress(Math.min(0.9, nFrames / (fps * 60)));
    } else if (k === 2) {
      const n = call('bk_nb_samples');
      if (layout === 1) {
        for (let ch = 0; ch < chans; ch++) {
          const ptr = call('bk_samples', 'number', ['number'], [ch]) >> 2;
          pcm[ch].push(bk.HEAPF32.slice(ptr, ptr + n));
        }
      } else if (layout === 2) {
        const ptr = call('bk_samples', 'number', ['number'], [0]) >> 2, src = bk.HEAPF32;
        for (let ch = 0; ch < chans; ch++) {
          const a = new Float32Array(n);
          for (let i = 0; i < n; i++) a[i] = src[ptr + i * chans + ch];
          pcm[ch].push(a);
        }
      }
      pcmLen += n;
    }
  }
  call('bk_close', null);
  await venc.flush();

  if (aenc && pcmLen) {
    const joined = pcm.map((parts) => { const a = new Float32Array(pcmLen); let o = 0; for (const p of parts) { a.set(p, o); o += p.length; } return a; });
    const outLen = Math.floor(pcmLen * OUT_RATE / rate);
    const CHUNK = 4800;
    for (let start = 0; start < outLen; start += CHUNK) {
      const n = Math.min(CHUNK, outLen - start);
      const planar = new Float32Array(n * chans);
      for (let ch = 0; ch < chans; ch++) {
        const src = joined[ch];
        for (let i = 0; i < n; i++) {
          const x = (start + i) * rate / OUT_RATE, i0 = Math.floor(x), t = x - i0;
          const a = src[i0] || 0, b = src[Math.min(i0 + 1, pcmLen - 1)] || 0;
          planar[ch * n + i] = a + (b - a) * t;
        }
      }
      const ad = new AudioData({ format: 'f32-planar', sampleRate: OUT_RATE, numberOfFrames: n, numberOfChannels: chans, timestamp: Math.round(start * 1e6 / OUT_RATE), data: planar });
      aenc.encode(ad);
      ad.close();
      while (aenc.encodeQueueSize > 16) await new Promise((r) => setTimeout(r, 2));
    }
    await aenc.flush();
  }
  if (failed) throw failed;
  muxer.finalize();
  onProgress(1);
  return new Uint8Array(target.buffer);
}

function missing(text) { const e = new Error(text); e.missing = true; return e; }

// Accepts the disc image (.iso/.bin/.img/.mdf), golf.exe, the no-CD zip, or a
// zip holding any of those (the usual abandonware download bundles the ISO
// and the no-CD zip together).
async function classify(files, scratch) {
  let disc = null, exe = null, wrongExe = null, n = 0;
  const visit = async (f) => {
    const lower = f.name.toLowerCase();
    if (/\.(iso|bin|img|mdf)$/.test(lower)) { disc = disc || f; return; }
    if (/\.exe$/.test(lower)) {
      const data = new Uint8Array(await f.arrayBuffer());
      if (await sha1(data) === NOCD_SHA1) exe = data; else wrongExe = wrongExe || f.name;
      return;
    }
    if (/\.zip$/.test(lower)) {
      for (const e of await zipEntries(f)) {
        const base = e.name.split('/').pop(), bl = base.toLowerCase();
        if (/\.(iso|bin|img|mdf)$/.test(bl) && !disc) disc = await zipToScratch(f, e, scratch, 'disc' + (n++) + '.' + bl.split('.').pop());
        else if (bl === 'golf.exe' || /\.zip$/.test(bl)) {
          if (e.csize > 64 << 20) continue;
          await visit(new File([await zipRead(f, e)], base));
        }
      }
      return;
    }
    if (/\.(7z|rar)$/.test(lower)) {
      for (const x of await un7z(f, scratch)) await visit(x);
      return;
    }
    if (/\.(cue|mds|nfo|txt|log)$/.test(lower)) return;
    throw new Error(`Not sure what ${f.name} is. Choose the SimGolf disc image (.iso) and the no-CD golf.exe, or the zip they came in.`);
  };
  for (const f of files) await visit(f);
  if (!exe && wrongExe) throw new Error(`The golf.exe in ${wrongExe} isn't the v1.02 no-CD version this port is built from.`);
  if (!disc && !exe) throw new Error("Couldn't find the disc image or the no-CD golf.exe in what you chose.");
  if (!disc) throw missing('Found the no-CD golf.exe; now add the disc image (.iso) as well.');
  if (!exe) throw missing('Found the disc image; now add the no-CD golf.exe (or the zip it came in) as well.');
  return { disc, exe };
}

async function install(files) {
  progress(0, 'Checking your files…');
  const opfs = await navigator.storage.getDirectory();
  try { await opfs.removeEntry('simgolf-scratch', { recursive: true }); } catch (e) {}
  const scratch = await opfs.getDirectoryHandle('simgolf-scratch', { create: true });
  const { disc, exe } = await classify(files, scratch);
  const iso = openDisc(disc);
  const rootEntries = iso.rootEntries();
  const find = (n) => rootEntries.find((e) => !e.dir && e.name.toLowerCase() === n);
  const hdr = find('data1.hdr');
  const cabs = rootEntries.filter((e) => !e.dir && /^data\d+\.cab$/i.test(e.name));
  if (!hdr || !cabs.length) throw new Error("That disc image isn't the SimGolf CD (no installer cabinets found).");

  const us = await createUnshield({ locateFile: (p) => p, print: () => {}, printErr: () => {} });
  us.FS.mkdir('/cd');
  us.FS.mount(us.WORKERFS, {
    blobs: [hdr, ...cabs].map((e) => ({ name: e.name.toLowerCase(), data: iso.blob(e.lba, e.size) })),
  }, '/cd');
  const c = (name, ret, args, vals) => us.ccall(name, ret, args, vals);
  if (!c('us_open', 'number', ['string'], ['/cd/data1.cab'])) throw new Error("Couldn't open the installer on that disc image.");
  const first = c('us_group_first', 'number', ['string'], [GROUP]), last = c('us_group_last', 'number', ['string'], [GROUP]);
  if (first < 0) throw new Error("That disc image isn't the SimGolf CD (no game files in the installer).");

  try { await opfs.removeEntry('simgolf', { recursive: true }); } catch (e) {}
  const top = await opfs.getDirectoryHandle('simgolf', { create: true });
  const cache = new Map();
  const files_ = [];
  let jgld = null;
  const biks = [];
  const total = last - first + 1;
  for (let i = first; i <= last; i++) {
    if (!c('us_valid', 'number', ['number'], [i])) continue;
    // "Flics\\X.flc" -> "Flics/X.flc" (directories are relative to the install folder)
    const rel = c('us_path', 'string', ['number'], [i]).split('\\').filter(Boolean).join('/');
    if (!rel) continue;
    if (!c('us_save', 'number', ['number', 'string'], [i, '/tmp/f'])) throw new Error('Unpacking ' + rel + ' failed.');
    const data = us.FS.readFile('/tmp/f');
    us.FS.unlink('/tmp/f');
    if (/^jgld\.dll$/i.test(rel)) jgld = data;
    if (/\.bik$/i.test(rel)) biks.push([rel, data]);
    await writeFile(top, 'game/' + rel, data, cache);
    files_.push([rel, data.length]);
    if ((i - first) % 25 === 0) progress(0.05 + 0.75 * (i - first) / total, 'Unpacking ' + rel);
  }
  for (const e of rootEntries) {
    if (e.dir || !ROOT_EXTRA.test(e.name)) continue;
    const data = new Uint8Array(await iso.blob(e.lba, e.size).arrayBuffer());
    await writeFile(top, 'game/' + e.name, data, cache);
    files_.push([e.name, data.length]);
  }
  if (!jgld) throw new Error("jgld.dll wasn't in the installer; is this the right disc?");

  // The intro and closing videos: Bink on the disc, WebM for the game's
  // <video> player (src/port/video.c looks for X.webm next to X.bik).
  // Optional: if this browser can't encode them, the game skips the videos.
  for (let v = 0; v < biks.length; v++) {
    const [rel, data] = biks[v];
    const label = /intro/i.test(rel) ? 'the intro video' : /clos/i.test(rel) ? 'the closing video' : rel;
    try {
      const webm = await binkToWebm(data, (f) => progress(0.8 + 0.15 * (v + f) / biks.length, 'Converting ' + label + '…'));
      const out = rel.replace(/\.bik$/i, '.webm');
      await writeFile(top, 'game/' + out, webm, cache);
      files_.push([out, webm.length]);
    } catch (e) {
      console.warn('video ' + rel + ': ' + (e && e.message || e));
    }
  }

  progress(0.96, 'Preparing the game code…');
  await writeFile(top, 'image/golf.bin', mapPE(exe), cache);
  const nops = await (await fetch('jgld-nops.json')).json();
  const jimg = mapPE(jgld);
  for (const [off, len] of nops) jimg.fill(0x90, off, off + len);
  await writeFile(top, 'image/jgld.bin', jimg, cache);
  await writeFile(top, 'installed.json', new TextEncoder().encode(JSON.stringify({ version: VERSION, volume: iso.volume, files: files_ })), cache);
  try { await opfs.removeEntry('simgolf-scratch', { recursive: true }); } catch (e) {}
  progress(1, 'Done');
}

self.onmessage = (ev) => {
  install(ev.data.files).then(() => post({ done: true }), (e) => post({ error: e && e.message || String(e), missing: !!(e && e.missing) }));
};
