#!/usr/bin/env node
// Convert SimGolf assets into browser-friendly formats.
//
//   node tools/convert-assets/convert-assets.mjs [srcDir=game] [outDir=assets] [--only=pcx,bmp,tga,flc,bik] [--limit=N]
//
// .pcx (8-bit paletted and 24-bit), .bmp (8/24-bit uncompressed) and .tga
// (uncompressed 32-bit truecolour) become .png. 8-bit PCX stays paletted
// (PNG colour type 3) so the palette indices survive: the game's blitters give
// indices 0xF8..0xFF special meanings, and Pal*.pcx files are palette swaps.
// Each interface mask (X_A.pcx / X_alpha.pcx) also yields X.rgba.png, the
// colour image with the mask applied as straight alpha.
// .flc animations become a paletted sprite sheet (.png, one row per
// animation) plus a .json with the frame layout and anchor; see docs/flic.md.
// .bik videos become .webm via ffmpeg (VP9 + Opus). Everything else is left
// alone. The directory layout of srcDir is mirrored under outDir. No npm
// dependencies: PNG is written with node:zlib.

import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';

// ---------- PNG writer ----------

const CRC_TABLE = new Int32Array(256).map((_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c;
});

function crc32(buf) {
  let c = -1;
  for (const b of buf) c = CRC_TABLE[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ -1) >>> 0;
}

function chunk(type, data) {
  const out = Buffer.alloc(12 + data.length);
  out.writeUInt32BE(data.length, 0);
  out.write(type, 4, 'ascii');
  data.copy(out, 8);
  out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length);
  return out;
}

// pixels: Buffer of RGB or RGBA rows, top-down.
export function encodePng(width, height, pixels, channels) {
  const stride = width * channels;
  const raw = Buffer.alloc((stride + 1) * height);
  for (let y = 0; y < height; y++) pixels.copy(raw, y * (stride + 1) + 1, y * stride, (y + 1) * stride);
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;
  ihdr[9] = channels === 4 ? 6 : 2;
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

// indices: Buffer of width*height palette indices, top-down. palette: 768 bytes RGB.
// alpha: optional per-index alpha (Uint8Array(256)) written as tRNS.
export function encodePngIndexed(width, height, indices, palette, alpha) {
  const raw = Buffer.alloc((width + 1) * height);
  for (let y = 0; y < height; y++) indices.copy(raw, y * (width + 1) + 1, y * width, (y + 1) * width);
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;
  ihdr[9] = 3;
  const chunks = [chunk('IHDR', ihdr), chunk('PLTE', Buffer.from(palette.subarray(0, 768)))];
  if (alpha) {
    let n = 256;
    while (n > 0 && alpha[n - 1] === 255) n--;
    if (n) chunks.push(chunk('tRNS', Buffer.from(alpha.subarray(0, n))));
  }
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    ...chunks,
    chunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

export function writeImagePng(img) {
  return img.palette
    ? encodePngIndexed(img.width, img.height, img.pixels, img.palette, img.alpha)
    : encodePng(img.width, img.height, img.pixels, img.channels);
}

// ---------- decoders (all return { width, height, channels, pixels } and,
// when paletted, channels 1 with pixels = indices and a 768-byte palette) ----------

export function decodePcx(buf) {
  if (buf[0] !== 0x0a || buf[2] !== 1) throw new Error('not an RLE PCX');
  const bpp = buf[3];
  const xmin = buf.readUInt16LE(4), ymin = buf.readUInt16LE(6);
  const xmax = buf.readUInt16LE(8), ymax = buf.readUInt16LE(10);
  const planes = buf[65], bytesPerLine = buf.readUInt16LE(66);
  const width = xmax - xmin + 1, height = ymax - ymin + 1;
  if (bpp !== 8 || (planes !== 1 && planes !== 3)) throw new Error(`unsupported PCX bpp=${bpp} planes=${planes}`);

  // Decode the RLE stream into scanlines of planes * bytesPerLine bytes.
  const lineLen = planes * bytesPerLine;
  const data = Buffer.alloc(lineLen * height);
  let src = 128, dst = 0;
  while (dst < data.length && src < buf.length) {
    let b = buf[src++], count = 1;
    if ((b & 0xc0) === 0xc0) { count = b & 0x3f; b = buf[src++]; }
    while (count-- > 0 && dst < data.length) data[dst++] = b;
  }

  if (planes === 1) {
    // 256-colour palette is the last 768 bytes, preceded by 0x0c.
    const palOff = buf.length - 769;
    if (buf[palOff] !== 0x0c) throw new Error('missing 256-colour palette');
    const indices = Buffer.alloc(width * height);
    for (let y = 0; y < height; y++) data.copy(indices, y * width, y * lineLen, y * lineLen + width);
    return { width, height, channels: 1, pixels: indices, palette: Buffer.from(buf.subarray(palOff + 1)) };
  }
  const pixels = Buffer.alloc(width * height * 3);
  for (let y = 0; y < height; y++)
    for (let x = 0; x < width; x++)
      for (let c = 0; c < 3; c++) pixels[(y * width + x) * 3 + c] = data[y * lineLen + c * bytesPerLine + x];
  return { width, height, channels: 3, pixels };
}

export function decodeBmp(buf) {
  if (buf.toString('ascii', 0, 2) !== 'BM') throw new Error('not a BMP');
  const dataOff = buf.readUInt32LE(10), hdrSize = buf.readUInt32LE(14);
  const width = buf.readInt32LE(18), rawHeight = buf.readInt32LE(22);
  const bpp = buf.readUInt16LE(28), compression = buf.readUInt32LE(30);
  if (compression !== 0) throw new Error(`unsupported BMP compression ${compression}`);
  const height = Math.abs(rawHeight), bottomUp = rawHeight > 0;
  const stride = Math.ceil((width * bpp) / 32) * 4;
  let pal;
  if (bpp === 8) {
    const n = buf.readUInt32LE(46) || 256;
    pal = buf.subarray(14 + hdrSize, 14 + hdrSize + n * 4);
  } else if (bpp !== 24) throw new Error(`unsupported BMP bpp=${bpp}`);

  const pixels = Buffer.alloc(width * height * 3);
  for (let y = 0; y < height; y++) {
    const row = dataOff + (bottomUp ? height - 1 - y : y) * stride;
    for (let x = 0; x < width; x++) {
      const o = (y * width + x) * 3;
      const s = bpp === 8 ? buf[row + x] * 4 : row + x * 3;
      const p = bpp === 8 ? pal : buf;
      pixels[o] = p[s + 2]; pixels[o + 1] = p[s + 1]; pixels[o + 2] = p[s];
    }
  }
  return { width, height, channels: 3, pixels };
}

export function decodeTga(buf) {
  const idLen = buf[0], cmapType = buf[1], imgType = buf[2];
  const width = buf.readUInt16LE(12), height = buf.readUInt16LE(14);
  const bpp = buf[16], desc = buf[17];
  if (cmapType !== 0 || imgType !== 2 || (bpp !== 24 && bpp !== 32)) throw new Error(`unsupported TGA type=${imgType} bpp=${bpp}`);
  const bytes = bpp / 8, topDown = (desc & 0x20) !== 0, channels = bytes;
  const src = 18 + idLen;
  const pixels = Buffer.alloc(width * height * channels);
  for (let y = 0; y < height; y++) {
    const row = src + (topDown ? y : height - 1 - y) * width * bytes;
    for (let x = 0; x < width; x++) {
      const s = row + x * bytes, o = (y * width + x) * channels;
      pixels[o] = buf[s + 2]; pixels[o + 1] = buf[s + 1]; pixels[o + 2] = buf[s];
      if (channels === 4) pixels[o + 3] = buf[s + 3];
    }
  }
  return { width, height, channels, pixels };
}

// ---------- FLIC (.flc) ----------
//
// Every .flc in the game is an Animator Pro FLC (magic 0xAF12, 8-bit) with a
// Firaxis extension in the header's reserved area, marked by 0xF1F1F2F2 at
// 0x1a. One file holds `anims` animations (usually facing directions) of
// `framesPerAnim` frames each. Animation a starts at frame chunk
// a*(framesPerAnim+1): a BYTE_RUN keyframe, framesPerAnim-1 deltas, then a ring
// frame that loops back. golf.exe's player is 0x481b50..0x482b90 (v1.02).

function flcPalette(buf, s, pal) {
  let q = s + 8, idx = 0;
  for (let k = buf.readUInt16LE(s + 6); k > 0; k--) {
    idx += buf[q++];
    let c = buf[q++] || 256;
    for (; c > 0 && idx < 256; c--, idx++, q += 3) buf.copy(pal, idx * 3, q, q + 3);
  }
}

function flcByteRun(buf, s, w, h, px) {
  let q = s + 6;
  for (let y = 0; y < h; y++) {
    q++; // obsolete packet count
    let x = 0, o = y * w;
    while (x < w) {
      const n = buf.readInt8(q++);
      if (n < 0) { buf.copy(px, o + x, q, q - n); q -= n; x -= n; }
      else { px.fill(buf[q++], o + x, o + x + n); x += n; }
    }
  }
}

function flcDeltaFlc(buf, s, w, px) {
  let q = s + 8, y = 0;
  for (let lines = buf.readUInt16LE(s + 6); lines > 0; lines--) {
    let op;
    for (;;) {
      op = buf.readUInt16LE(q); q += 2;
      if ((op & 0xc000) === 0xc000) y += 0x10000 - op; // skip lines
      else break;
    }
    if ((op & 0xc000) === 0x8000) { px[y * w + w - 1] = op & 0xff; op = buf.readUInt16LE(q); q += 2; }
    let o = y * w;
    for (; op > 0; op--) {
      o += buf[q++];
      const n = buf.readInt8(q++);
      if (n < 0) { for (let i = 0; i < -n; i++, o += 2) { px[o] = buf[q]; px[o + 1] = buf[q + 1]; } q += 2; }
      else { buf.copy(px, o, q, q + 2 * n); q += 2 * n; o += 2 * n; }
    }
    y++;
  }
}

function flcDeltaFli(buf, s, w, px) {
  let y = buf.readUInt16LE(s + 6), q = s + 10;
  for (let lines = buf.readUInt16LE(s + 8); lines > 0; lines--, y++) {
    let o = y * w;
    for (let p = buf[q++]; p > 0; p--) {
      o += buf[q++];
      const n = buf.readInt8(q++);
      if (n < 0) { px.fill(buf[q++], o, o - n); o -= n; }
      else { buf.copy(px, o, q, q + n); q += n; o += n; }
    }
  }
}

// Returns { width, height, palette, frames: [[Buffer indices per frame] per anim], meta }.
export function decodeFlc(buf) {
  if (buf.readUInt16LE(4) !== 0xaf12) throw new Error('not an FLC');
  if (buf.readUInt16LE(12) !== 8) throw new Error('FLC is not 8-bit');
  const width = buf.readUInt16LE(8), height = buf.readUInt16LE(10);
  const firaxis = buf.readUInt32LE(0x1a) === 0xf1f1f2f2;

  const chunks = [];
  for (let off = buf.readUInt32LE(0x50); off + 6 <= buf.length;) {
    const size = buf.readUInt32LE(off);
    if (size < 6) break;
    if (buf.readUInt16LE(off + 4) === 0xf1fa) chunks.push(off);
    off += size;
  }
  // Plain FLCs are treated as one animation, as golf.exe does (0x481ca0).
  const anims = firaxis ? buf.readUInt16LE(0x60) : 1;
  const framesPerAnim = firaxis ? buf.readUInt16LE(0x62) : chunks.length - 1;
  if (anims * (framesPerAnim + 1) - 1 > chunks.length) throw new Error(`FLC has ${chunks.length} frames, header needs ${anims}x${framesPerAnim + 1}`);

  const px = Buffer.alloc(width * height);
  const pal = Buffer.alloc(768);
  let firstPal = null, paletteChanges = false;
  const frames = [];
  for (let a = 0; a < anims; a++) {
    const row = [];
    for (let f = 0; f < framesPerAnim; f++) {
      const fo = chunks[a * (framesPerAnim + 1) + f];
      for (let i = 0, s = fo + 16; i < buf.readUInt16LE(fo + 6); i++) {
        const type = buf.readUInt16LE(s + 4);
        if (type === 4 || type === 11) {
          // golf.exe treats COLOR_64 like COLOR_256 (8-bit components); none ship anyway.
          flcPalette(buf, s, pal);
          if (!firstPal) firstPal = Buffer.from(pal);
          else if (!pal.equals(firstPal)) paletteChanges = true;
        } else if (type === 7) flcDeltaFlc(buf, s, width, px);
        else if (type === 12) flcDeltaFli(buf, s, width, px);
        else if (type === 13) px.fill(0);
        else if (type === 15) flcByteRun(buf, s, width, height, px);
        else if (type === 16) buf.copy(px, 0, s + 6, s + 6 + width * height);
        s += buf.readUInt32LE(s);
      }
      row.push(Buffer.from(px));
    }
    frames.push(row);
  }
  return {
    width, height, palette: firstPal ?? pal, frames,
    meta: {
      width, height, anims, framesPerAnim,
      speedMs: buf.readUInt32LE(0x10),
      // Bit a set = animation a exists; 0 = all exist (golf.exe 0x482490).
      animMask: firaxis ? buf.readUInt32LE(0x70) : 0,
      // Top-left of the frame inside a canvas (usually 480x480) whose centre is the object's ground point.
      origin: firaxis ? [buf.readUInt16LE(0x64), buf.readUInt16LE(0x66)] : [0, 0],
      canvas: firaxis ? [buf.readUInt16LE(0x68), buf.readUInt16LE(0x6a)] : [width, height],
      unknown6c: firaxis ? buf.readUInt16LE(0x6c) : 0,
      ...(paletteChanges ? { paletteChanges } : {}),
    },
  };
}

// Normal sprite draws skip 0xFE and 0xFF (docs/graphsy-sprite.md).
const SPRITE_ALPHA = new Uint8Array(256).fill(255);
SPRITE_ALPHA[0xfe] = SPRITE_ALPHA[0xff] = 0;

// Sprite sheet: one row per animation, one column per frame.
export function flcSheet(flc) {
  const { width: w, height: h, frames } = flc;
  const cols = frames[0].length, rows = frames.length;
  const sheet = Buffer.alloc(w * cols * h * rows, 0xff);
  frames.forEach((row, r) => row.forEach((px, c) => {
    for (let y = 0; y < h; y++) px.copy(sheet, ((r * h + y) * cols + c) * w, y * w, (y + 1) * w);
  }));
  return { width: w * cols, height: h * rows, channels: 1, pixels: sheet, palette: flc.palette, alpha: SPRITE_ALPHA };
}

// ---------- interface masks (X_A.pcx / X_alpha.pcx) ----------
//
// A mask is an 8-bit image the same size as its colour image, drawn with sprite
// slot 21 DrawMasked: index 0 = opaque, 0xFF = skip, otherwise
// dst = colour + dst*m/256. The colour art is therefore premultiplied (painted
// over black), and alpha = 1 - m/256. Un-premultiply to get straight alpha.

export function applyMask(colour, mask) {
  if (colour.width !== mask.width || colour.height !== mask.height) throw new Error('mask size differs from colour image');
  const n = colour.width * colour.height, out = Buffer.alloc(n * 4);
  for (let i = 0; i < n; i++) {
    const m = mask.pixels[i];
    if (m === 0xff) continue;
    const a = 1 - m / 256;
    for (let c = 0; c < 3; c++) {
      const v = colour.palette ? colour.palette[colour.pixels[i] * 3 + c] : colour.pixels[i * 3 + c];
      out[i * 4 + c] = Math.min(255, Math.round(v / a));
    }
    out[i * 4 + 3] = Math.round(a * 255);
  }
  return { width: colour.width, height: colour.height, channels: 4, pixels: out };
}

const MASK_RE = /_(a|alpha)\.pcx$/i;

// Masks whose colour image isn't named after them (paths relative to the game dir, lower case).
const MASK_PARTNERS = {
  'interface/choosealphabuttons_a.pcx': ['ChooseDesertButtons.pcx', 'ChooseLinksButtons.pcx', 'ChooseParklandButtons.pcx', 'ChooseTropicalButtons.pcx'],
  'interface/infoscreens/sgareport_alpha.pcx': ['SGA.pcx'],
};

// ---------- driver ----------

const IMAGE_DECODERS = { pcx: decodePcx, bmp: decodeBmp, tga: decodeTga };

function walk(dir) {
  const out = [];
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) out.push(...walk(p));
    else if (e.isFile()) out.push(p);
  }
  return out;
}

function findCaseInsensitive(p) {
  const dir = path.dirname(p), want = path.basename(p).toLowerCase();
  const hit = fs.readdirSync(dir).find(n => n.toLowerCase() === want);
  return hit ? path.join(dir, hit) : null;
}

// X.flc -> X.png (sheet) + X.json (layout). Pairs X.flc with XShadow.flc.
function convertFlc(src, dst) {
  const flc = decodeFlc(fs.readFileSync(src));
  fs.writeFileSync(dst, writeImagePng(flcSheet(flc)));
  const meta = { ...flc.meta };
  const shadow = findCaseInsensitive(src.replace(/\.flc$/i, 'Shadow.flc'));
  if (shadow) meta.shadow = path.basename(shadow).replace(/\.flc$/i, '.png');
  fs.writeFileSync(dst.replace(/\.png$/, '.json'), JSON.stringify(meta) + '\n');
}

function convertBik(src, dst) {
  const r = spawnSync('ffmpeg', ['-hide_banner', '-loglevel', 'error', '-y', '-i', src,
    '-c:v', 'libvpx-vp9', '-crf', '32', '-b:v', '0', '-row-mt', '1', '-c:a', 'libopus', '-b:a', '128k', dst], { stdio: 'inherit' });
  if (r.status !== 0) throw new Error(`ffmpeg exited with ${r.status}`);
}

function main() {
  const args = process.argv.slice(2);
  const opts = Object.fromEntries(args.filter(a => a.startsWith('--')).map(a => a.slice(2).split('=')));
  const [srcDir = 'game', outDir = 'assets'] = args.filter(a => !a.startsWith('--'));
  const only = opts.only ? new Set(opts.only.split(',')) : null;
  const limit = opts.limit ? +opts.limit : Infinity;

  const counts = {};
  const failures = [];
  const masks = [];
  for (const src of walk(srcDir)) {
    const ext = path.extname(src).slice(1).toLowerCase();
    if (!(ext in IMAGE_DECODERS) && ext !== 'bik' && ext !== 'flc') continue;
    if (only && !only.has(ext)) continue;
    if ((counts[ext] ?? 0) >= limit) continue;
    const rel = path.relative(srcDir, src);
    const dst = path.join(outDir, rel.slice(0, -ext.length) + (ext === 'bik' ? 'webm' : 'png'));
    fs.mkdirSync(path.dirname(dst), { recursive: true });
    try {
      if (ext === 'bik') convertBik(src, dst);
      else if (ext === 'flc') convertFlc(src, dst);
      else {
        const img = IMAGE_DECODERS[ext](fs.readFileSync(src));
        fs.writeFileSync(dst, writeImagePng(img));
        if (ext === 'pcx' && MASK_RE.test(src)) masks.push(src);
      }
      counts[ext] = (counts[ext] ?? 0) + 1;
    } catch (e) {
      failures.push(`${rel}: ${e.message}`);
    }
  }
  for (const src of masks) {
    const rel = path.relative(srcDir, src);
    const partners = MASK_PARTNERS[rel.split(path.sep).join('/').toLowerCase()]?.map(n => path.join(path.dirname(src), n))
      ?? [src.replace(MASK_RE, '.pcx')];
    for (const p of partners) {
      try {
        const colourSrc = findCaseInsensitive(p);
        if (!colourSrc) throw new Error(`no colour image ${path.basename(p)}`);
        const img = applyMask(decodePcx(fs.readFileSync(colourSrc)), decodePcx(fs.readFileSync(src)));
        const dst = path.join(outDir, path.relative(srcDir, colourSrc).replace(/\.pcx$/i, '.rgba.png'));
        fs.writeFileSync(dst, writeImagePng(img));
        counts.mask = (counts.mask ?? 0) + 1;
      } catch (e) {
        failures.push(`${rel}: ${e.message}`);
      }
    }
  }
  console.log('converted:', counts);
  if (failures.length) {
    console.log(`failed (${failures.length}):`);
    for (const f of failures) console.log('  ' + f);
    process.exitCode = 1;
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(fs.realpathSync(process.argv[1])).href) main();
