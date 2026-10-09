// Web Audio back end for src/platform/sound.cpp, linked with --js-library.
// See sound.h for the behaviour it reproduces and docs/sound.md for where it
// comes from.
//
// Each registered index has a URL, loads lazily on its first play (fetch, then
// our own RIFF parser so the `smpl` loop points survive), and keeps the decoded
// AudioBuffer until shutdown. A play that arrives while the file is still
// loading is queued and started once it's ready.

addToLibrary({
  $SND: {
    MAX_VOICES: 16,          // per index, as in sound.dll
    STREAM_BYTES: 128 * 1024, // data chunks above this were streamed: one voice only
    ctx: null,
    master: null,
    sounds: {},              // index -> {url, single, state, buffer, loop, loopStart, loopEnd, voices, pending}

    // Parses a PCM RIFF/WAVE file into an AudioBuffer plus its first smpl loop.
    decode(ab) {
      const dv = new DataView(ab);
      const tag = (p) => String.fromCharCode(dv.getUint8(p), dv.getUint8(p + 1), dv.getUint8(p + 2), dv.getUint8(p + 3));
      if (dv.byteLength < 12 || tag(0) !== 'RIFF' || tag(8) !== 'WAVE') throw new Error('not a RIFF/WAVE file');
      let fmt = null, data = null, loop = null;
      for (let p = 12; p + 8 <= dv.byteLength;) {
        const id = tag(p), size = dv.getUint32(p + 4, true), body = p + 8;
        if (id === 'fmt ') {
          fmt = { tag: dv.getUint16(body, true), channels: dv.getUint16(body + 2, true),
                  rate: dv.getUint32(body + 4, true), bits: dv.getUint16(body + 14, true) };
        } else if (id === 'data') {
          data = { offset: body, length: Math.min(size, dv.byteLength - body) };
        } else if (id === 'smpl' && size >= 60 && dv.getUint32(body + 28, true) > 0) {
          // 36-byte header, then 24-byte loops: cue id, type, start, end (inclusive), fraction, count.
          loop = { start: dv.getUint32(body + 44, true), end: dv.getUint32(body + 48, true) + 1 };
        }
        p = body + size + (size & 1);
      }
      if (!fmt || !data) throw new Error('missing fmt or data chunk');
      if (fmt.tag !== 1 || (fmt.bits !== 8 && fmt.bits !== 16)) throw new Error('unsupported format ' + fmt.tag + '/' + fmt.bits);
      const bytes = fmt.bits / 8, frames = Math.floor(data.length / (bytes * fmt.channels));
      const buffer = SND.ctx.createBuffer(fmt.channels, Math.max(frames, 1), fmt.rate);
      for (let c = 0; c < fmt.channels; c++) {
        const out = buffer.getChannelData(c);
        let p = data.offset + c * bytes;
        const step = bytes * fmt.channels;
        if (bytes === 2) for (let i = 0; i < frames; i++, p += step) out[i] = dv.getInt16(p, true) / 32768;
        else for (let i = 0; i < frames; i++, p += step) out[i] = (dv.getUint8(p) - 128) / 128;
      }
      return { buffer, loop, streamed: data.length > SND.STREAM_BYTES };
    },

    load(s) {
      if (s.state !== 'idle') return;
      s.state = 'loading';
      fetch(s.url)
        .then((r) => { if (!r.ok) throw new Error('HTTP ' + r.status); return r.arrayBuffer(); })
        .then((ab) => {
          if (!SND.ctx) return;
          const d = SND.decode(ab);
          s.buffer = d.buffer;
          s.single = s.single || d.streamed;
          if (d.loop && d.loop.end > d.loop.start) {
            s.loop = true;
            s.loopStart = d.loop.start / d.buffer.sampleRate;
            s.loopEnd = Math.min(d.loop.end, d.buffer.length) / d.buffer.sampleRate;
          }
          s.state = 'ready';
          const pending = s.pending;
          s.pending = [];
          for (const args of pending) SND.start(s, ...args);
        })
        .catch((e) => {
          s.state = 'failed';
          s.pending = [];
          err('sound: ' + s.url + ': ' + e.message);
        });
    },

    start(s, volume, pan, cents, delayMs) {
      const ctx = SND.ctx;
      // Before the first user gesture the context is suspended and anything
      // started now would play the moment it resumes. Keep loops (ambience),
      // drop one-shots so the first click doesn't release a burst of them.
      if (ctx.state !== 'running' && !s.loop) return;
      if (s.voices.length >= (s.single ? 1 : SND.MAX_VOICES)) return;

      const src = ctx.createBufferSource();
      src.buffer = s.buffer;
      src.playbackRate.value = Math.pow(2, cents / 1200);
      if (s.loop) {
        src.loop = true;
        src.loopStart = s.loopStart;
        src.loopEnd = s.loopEnd;
      }
      const gain = ctx.createGain();
      gain.gain.value = (Math.max(0, Math.min(127, volume)) + 1) / 128;
      if (pan) {
        // sound.dll's linear law: the louder side keeps full volume, the other
        // drops by |pan|/63.
        pan = Math.max(-64, Math.min(63, pan));
        const left = ctx.createGain(), right = ctx.createGain(), merger = ctx.createChannelMerger(2);
        left.gain.value = pan > 0 ? Math.max(0, 1 - pan / 63) : 1;
        right.gain.value = pan < 0 ? Math.max(0, 1 + pan / 63) : 1;
        if (s.buffer.numberOfChannels === 1) {
          src.connect(left);
          src.connect(right);
        } else {
          const splitter = ctx.createChannelSplitter(2);
          src.connect(splitter);
          splitter.connect(left, 0);
          splitter.connect(right, 1);
        }
        left.connect(merger, 0, 0);
        right.connect(merger, 0, 1);
        merger.connect(gain);
      } else {
        src.connect(gain);
      }
      gain.connect(SND.master);

      const voice = { src, gain };
      s.voices.push(voice);
      src.onended = () => {
        const i = s.voices.indexOf(voice);
        if (i >= 0) s.voices.splice(i, 1);
        gain.disconnect();
      };
      src.start(ctx.currentTime + Math.max(0, delayMs) / 1000);
    },

    unlock() {
      if (SND.ctx && SND.ctx.state === 'suspended') SND.ctx.resume();
    },
  },

  snd_js_init__deps: ['$SND'],
  snd_js_init: function () {
    if (SND.ctx) return;
    const AC = globalThis.AudioContext || globalThis.webkitAudioContext;
    if (!AC) { err('sound: no Web Audio'); return; }
    SND.ctx = new AC({ sampleRate: 44100 });   // sound.dll mixed at 44.1 kHz
    SND.master = SND.ctx.createGain();
    SND.master.connect(SND.ctx.destination);
    for (const ev of ['pointerdown', 'keydown', 'touchend']) addEventListener(ev, SND.unlock, true);
    Module['soundDebug'] = SND;   // for the console and tests
  },

  snd_js_register__deps: ['$SND'],
  snd_js_register: function (index, urlPtr, single) {
    SND.sounds[index] = { url: urlPtr ? UTF8ToString(urlPtr) : null, single: !!single,
                          state: urlPtr ? 'idle' : 'failed', buffer: null,
                          loop: false, loopStart: 0, loopEnd: 0, voices: [], pending: [] };
  },

  snd_js_play__deps: ['$SND'],
  snd_js_play: function (index, volume, pan, cents, delayMs) {
    const s = SND.sounds[index];
    if (!SND.ctx || !s || s.state === 'failed') return;
    if (s.state === 'ready') { SND.start(s, volume, pan, cents, delayMs); return; }
    if (s.pending.length < SND.MAX_VOICES) s.pending.push([volume, pan, cents, delayMs]);
    SND.load(s);
  },

  snd_js_fade_out__deps: ['$SND'],
  snd_js_fade_out: function (index, ms) {
    const s = SND.sounds[index];
    if (!SND.ctx || !s) return;
    s.pending = [];
    const now = SND.ctx.currentTime, end = now + Math.max(ms, 1) / 1000;
    for (const v of s.voices) {
      const g = v.gain.gain;
      g.cancelScheduledValues(now);
      g.setValueAtTime(g.value, now);
      g.linearRampToValueAtTime(0, end);
      v.src.stop(end);
    }
  },

  snd_js_stop__deps: ['$SND'],
  snd_js_stop: function (index) {
    const s = SND.sounds[index];
    if (!SND.ctx || !s) return;
    s.pending = [];
    for (const v of s.voices.slice()) v.src.stop();
  },

  snd_js_shutdown__deps: ['$SND'],
  snd_js_shutdown: function () {
    if (!SND.ctx) return;
    for (const ev of ['pointerdown', 'keydown', 'touchend']) removeEventListener(ev, SND.unlock, true);
    SND.ctx.close();
    SND.ctx = SND.master = null;
    SND.sounds = {};
  },
});
