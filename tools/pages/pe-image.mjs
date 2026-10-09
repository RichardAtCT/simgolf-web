// Maps a PE file the way Ghidra's loader does for ExportPort's image.bin:
// headers plus each section's raw data at its RVA, zero-filled up to
// SizeOfImage. Shared by the browser installer (web/pages/install-worker.js
// imports a copy of it) and the tools below.
export function mapPE(buf) {
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

// [offset, length] runs to fill with NOP (0x90).
export function applyNops(img, runs) {
  for (const [off, len] of runs) img.fill(0x90, off, off + len);
  return img;
}
