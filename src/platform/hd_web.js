// WebGL2 presentation for src/port/hd.c, linked with --js-library. See hd.c
// for what the layers are and how they're combined.
//
// The game's 800x600 frame is uploaded as-is (R16UI, RGB555) and decoded in
// the shaders. Its text comes separately (gdi.c, gdi_hd_text): the frame
// arrives with the text erased, and the runs are drawn here with Canvas2D in
// the game's own fonts at the output resolution, then laid over the result. The terrain triangles from terrain_hd.cpp are replayed into a
// framebuffer the size of the display canvas, in their recorded (painter's)
// order with no depth test, like the original's generic GL.

addToLibrary({
  $HD: {
    FILTERS: ['nearest', 'linear', 'xbr'],
    enabled: true,
    terrainOn: true,
    filter: 2,
    failed: false,
    gl: null,
    canvas: null,
    textOn: true,
    textures: new Map(),  // serial -> {tex, used}
    fonts: [],            // gdi.c font file -> {state, family}
    textRuns: null,
    textKey: null,
    builds: 0,

    init() {
      const q = new URLSearchParams(location.search);
      if (q.get('hd') === '0' || q.get('hd') === 'off') HD.enabled = false;
      if (q.get('hdterrain') === '0') HD.terrainOn = false;
      if (q.get('hdtext') === '0') HD.textOn = false;
      const f = HD.FILTERS.indexOf(q.get('filter'));
      if (f >= 0) HD.filter = f;
      HD.debug = q.has('hddebug') ? 1 : 0;  // tints the pixels the terrain layer replaces
      addEventListener('keydown', (e) => {
        if (!e.ctrlKey || !['F9', 'F10', 'F11'].includes(e.key)) return;
        e.preventDefault();
        e.stopImmediatePropagation();
        if (e.key === 'F9') HD.terrainOn = !HD.terrainOn;
        else if (e.key === 'F10') HD.filter = (HD.filter + 1) % HD.FILTERS.length;
        else HD.enabled = !HD.enabled;
        out(`hd: ${HD.enabled ? 'on' : 'off'}, terrain ${HD.terrainOn ? 'high-res' : '1:1'}, 2D filter ${HD.FILTERS[HD.filter]}`);
      }, true);
      Module['hd'] = HD;
    },

    shader(gl, vs, fs) {
      const p = gl.createProgram();
      for (const [type, src] of [[gl.VERTEX_SHADER, vs], [gl.FRAGMENT_SHADER, fs]]) {
        const s = gl.createShader(type);
        gl.shaderSource(s, src);
        gl.compileShader(s);
        if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error('hd shader: ' + gl.getShaderInfoLog(s));
        gl.attachShader(p, s);
      }
      gl.linkProgram(p);
      if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error('hd link: ' + gl.getProgramInfoLog(p));
      const u = {};
      for (let i = 0, n = gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS); i < n; i++) {
        const name = gl.getActiveUniform(p, i).name;
        u[name] = gl.getUniformLocation(p, name);
      }
      return { p, u };
    },

    setup() {
      const c = document.createElement('canvas');
      Object.assign(c.style, { position: 'fixed', pointerEvents: 'none', zIndex: 5, imageRendering: 'auto' });
      document.body.appendChild(c);
      const gl = c.getContext('webgl2', { alpha: false, antialias: false, depth: false, stencil: false });
      if (!gl) throw new Error('no WebGL2');
      HD.canvas = c;
      HD.gl = gl;

      HD.terrainProg = HD.shader(gl, `#version 300 es
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec4 aCol;
        layout(location = 2) in vec2 aUV;
        uniform vec2 uTarget;
        out vec4 vCol;
        out vec2 vUV;
        void main() {
          vCol = aCol;
          vUV = aUV;
          gl_Position = vec4(aPos / uTarget * 2.0 - 1.0, 0.0, 1.0);
        }`, `#version 300 es
        precision highp float;
        in vec4 vCol;
        in vec2 vUV;
        uniform sampler2D uTex;
        uniform int uFlags;  // 1 blend, 2 textured, 4 texture has alpha
        out vec4 o;
        void main() {
          vec4 c = vCol;
          if ((uFlags & 2) != 0) {
            vec4 t = texture(uTex, vUV);
            c.rgb *= t.rgb;
            if ((uFlags & 4) != 0) c.a *= t.a;
          }
          if ((uFlags & 1) == 0) c.a = 1.0;
          o = c;
        }`);

      // The 2D layer. xBR is Hyllian's xBR level 2 (single pass), working on
      // the luma of the 5x5 neighbourhood minus its corners.
      HD.frameProg = HD.shader(gl, `#version 300 es
        out vec2 vUV;
        void main() {
          vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
          vUV = vec2(p.x, 1.0 - p.y);
          gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        }`, `#version 300 es
        precision highp float;
        precision highp usampler2D;
        uniform usampler2D uScreen;
        uniform sampler2D uMask;
        uniform sampler2D uTerrain;
        uniform vec2 uSrc;       // frame size
        uniform vec2 uTerrainScale;  // frame size / terrain target size
        uniform float uScale;    // output pixels per frame pixel
        uniform int uFilter;     // 0 nearest, 1 linear, 2 xBR
        uniform int uUseTerrain;
        uniform int uDebug;
        in vec2 vUV;
        out vec4 o;

        vec3 px(ivec2 p) {
          p = clamp(p, ivec2(0), ivec2(uSrc) - 1);
          uint c = texelFetch(uScreen, p, 0).r;
          return vec3(float((c >> 10) & 31u), float((c >> 5) & 31u), float(c & 31u)) / 31.0;
        }

        const vec3 kY = vec3(0.2126, 0.7152, 0.0722) * 48.0;
        vec4 luma(vec3 a, vec3 b, vec3 c, vec3 d) { return vec4(dot(a, kY), dot(b, kY), dot(c, kY), dot(d, kY)); }
        vec4 df(vec4 a, vec4 b) { return abs(a - b); }
        vec4 eq(vec4 a, vec4 b) { return vec4(lessThan(df(a, b), vec4(15.0))); }
        vec4 neq(vec4 a, vec4 b) { return vec4(notEqual(a, b)); }
        vec4 wd(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h) {
          return df(a, b) + df(a, c) + df(d, e) + df(d, f) + 4.0 * df(g, h);
        }
        float cdf(vec3 a, vec3 b) { vec3 d = abs(a - b); return d.r + d.g + d.b; }

        vec3 xbr(vec2 src) {
          ivec2 q = ivec2(floor(src));
          vec2 fp = fract(src);
          vec3 A1 = px(q + ivec2(-1, -2)), B1 = px(q + ivec2(0, -2)), C1 = px(q + ivec2(1, -2));
          vec3 A0 = px(q + ivec2(-2, -1)), A = px(q + ivec2(-1, -1)), B = px(q + ivec2(0, -1)), C = px(q + ivec2(1, -1)), C4 = px(q + ivec2(2, -1));
          vec3 D0 = px(q + ivec2(-2, 0)), D = px(q + ivec2(-1, 0)), E = px(q), F = px(q + ivec2(1, 0)), F4 = px(q + ivec2(2, 0));
          vec3 G0 = px(q + ivec2(-2, 1)), G = px(q + ivec2(-1, 1)), H = px(q + ivec2(0, 1)), I = px(q + ivec2(1, 1)), I4 = px(q + ivec2(2, 1));
          vec3 G5 = px(q + ivec2(-1, 2)), H5 = px(q + ivec2(0, 2)), I5 = px(q + ivec2(1, 2));

          // Component k is one corner of the pixel: x bottom-right, y top-right,
          // z top-left, w bottom-left, with the neighbourhood rotated to match.
          vec4 b = luma(B, D, H, F), c = luma(C, A, G, I);
          vec4 e = luma(E, E, E, E);
          vec4 d = b.yzwx, f = b.wxyz, g = c.zwxy, h = b.zwxy, i = c.wxyz;
          vec4 i4 = luma(I4, C1, A0, G5), i5 = luma(I5, C4, A1, G0), h5 = luma(H5, F4, B1, D0);
          vec4 f4 = h5.yzwx;

          const vec4 Ao = vec4(1.0, -1.0, -1.0, 1.0), Bo = vec4(1.0, 1.0, -1.0, -1.0), Co = vec4(1.5, 0.5, -0.5, 0.5);
          const vec4 Bx = vec4(0.5, 2.0, -0.5, -2.0), Cx = vec4(1.0, 1.0, -0.5, 0.0);
          const vec4 By = vec4(2.0, 0.5, -2.0, -0.5), Cy = vec4(2.0, 0.0, -1.0, 0.5);
          vec4 fx = Ao * fp.y + Bo * fp.x;
          vec4 fxLeft = Ao * fp.y + Bx * fp.x;
          vec4 fxUp = Ao * fp.y + By * fp.x;
          vec4 delta = vec4(1.0 / uScale);
          vec4 deltaL = vec4(0.5, 1.0, 0.5, 1.0) / uScale;
          vec4 deltaU = deltaL.yxwz;

          vec4 lv1 = neq(e, f) * neq(e, h) * clamp((1.0 - eq(f, b)) * (1.0 - eq(h, d)) +
                     eq(e, i) * (1.0 - eq(f, i4)) * (1.0 - eq(h, i5)) + eq(e, g) + eq(e, c), 0.0, 1.0);
          vec4 lv2Left = neq(e, g) * neq(d, g);
          vec4 lv2Up = neq(e, c) * neq(b, c);
          vec4 edr = vec4(lessThan(wd(e, c, g, i, h5, f4, h, f), wd(h, d, i5, f, i4, b, e, i))) * lv1;
          vec4 edrLeft = vec4(lessThanEqual(2.0 * df(f, g), df(h, c))) * lv2Left;
          vec4 edrUp = vec4(greaterThanEqual(df(f, g), 2.0 * df(h, c))) * lv2Up;

          vec4 fx45 = edr * smoothstep(Co - delta, Co + delta, fx);
          vec4 fx30 = edr * edrLeft * smoothstep(Cx - deltaL, Cx + deltaL, fxLeft);
          vec4 fx60 = edr * edrUp * smoothstep(Cy - deltaU, Cy + deltaU, fxUp);
          vec4 m = max(max(fx30, fx60), fx45);
          vec4 pf = vec4(lessThanEqual(df(e, f), df(e, h)));  // 1: blend with f, 0: with h

          vec3 r1 = mix(E, mix(H, F, pf.x), m.x);
          r1 = mix(r1, mix(B, D, pf.z), m.z);
          vec3 r2 = mix(E, mix(F, B, pf.y), m.y);
          r2 = mix(r2, mix(D, H, pf.w), m.w);
          return mix(r1, r2, step(cdf(E, r1), cdf(E, r2)));
        }

        vec3 bilinear(vec2 src) {
          vec2 s = src - 0.5;
          ivec2 q = ivec2(floor(s));
          vec2 f = fract(s);
          return mix(mix(px(q), px(q + ivec2(1, 0)), f.x), mix(px(q + ivec2(0, 1)), px(q + ivec2(1, 1)), f.x), f.y);
        }

        void main() {
          vec2 src = vUV * uSrc;
          vec3 c = uFilter == 2 ? xbr(src) : uFilter == 1 ? bilinear(src) : px(ivec2(floor(src)));
          if (uUseTerrain != 0) {
            vec2 t = vUV * uTerrainScale;
            vec4 terrain = texture(uTerrain, vec2(t.x, 1.0 - t.y));
            // r: terrain coverage, g: coverage times the shadow factor.
            vec2 m = texture(uMask, vUV).rg;
            float w = smoothstep(0.35, 0.65, m.r) * terrain.a;
            c = mix(c, terrain.rgb * (m.g / max(m.r, 1e-3)), w);
            if (uDebug != 0) c = mix(c, vec3(1.0, 0.0, 1.0), 0.4 * w);
          }
          o = vec4(c, 1.0);
        }`);

      HD.textProg = HD.shader(gl, `#version 300 es
        out vec2 vUV;
        void main() {
          vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
          vUV = vec2(p.x, 1.0 - p.y);
          gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        }`, `#version 300 es
        precision highp float;
        uniform sampler2D uText;
        in vec2 vUV;
        out vec4 o;
        void main() { o = texture(uText, vUV); }`);
      HD.textCanvas = document.createElement('canvas');
      HD.textCtx = HD.textCanvas.getContext('2d');

      HD.vbo = gl.createBuffer();
      HD.vao = gl.createVertexArray();
      gl.bindVertexArray(HD.vao);
      gl.bindBuffer(gl.ARRAY_BUFFER, HD.vbo);
      gl.enableVertexAttribArray(0);
      gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 32, 0);
      gl.enableVertexAttribArray(1);
      gl.vertexAttribPointer(1, 4, gl.FLOAT, false, 32, 8);
      gl.enableVertexAttribArray(2);
      gl.vertexAttribPointer(2, 2, gl.FLOAT, false, 32, 24);
      gl.bindVertexArray(null);
      HD.emptyVao = gl.createVertexArray();

      const tex = (filter) => {
        const t = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, t);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        return t;
      };
      HD.screenTex = tex(gl.NEAREST);
      HD.maskTex = tex(gl.LINEAR);
      HD.terrainTex = tex(gl.LINEAR);
      HD.textTex = tex(gl.NEAREST);
      HD.fbo = gl.createFramebuffer();
      HD.fboSize = [0, 0];
      HD.srcSize = [0, 0];
    },

    // Keeps the overlay on top of the SDL canvas at device-pixel resolution.
    layout() {
      const sdl = Module['canvas'];
      const r = sdl.getBoundingClientRect();
      const s = HD.canvas.style;
      s.left = r.left + 'px';
      s.top = r.top + 'px';
      s.width = r.width + 'px';
      s.height = r.height + 'px';
      const dpr = window.devicePixelRatio || 1;
      const w = Math.max(1, Math.round(r.width * dpr)), h = Math.max(1, Math.round(r.height * dpr));
      if (HD.canvas.width === w && HD.canvas.height === h) return false;
      HD.canvas.width = w;
      HD.canvas.height = h;
      return true;
    },

    // Draws the runs from gdi_hd_text over the output. The overlay is only
    // re-rendered when the runs or the output size change.
    drawText(w, h) {
      const gl = HD.gl, c = HD.textCanvas, W = HD.canvas.width, H = HD.canvas.height;
      const r = HD.textRuns;
      const k = HD.textKey;
      const same = k && k.W === W && k.H === H && k.r.length === r.length && k.r.every((v, i) => v === r[i]);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, HD.textTex);
      if (!same) {
        if (c.width !== W || c.height !== H) { c.width = W; c.height = H; }
        const ctx = HD.textCtx, sx = W / w, sy = H / h;
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.clearRect(0, 0, W, H);
        ctx.setTransform(sx, 0, 0, sy, 0, 0);
        ctx.textBaseline = 'alphabetic';
        ctx.textAlign = 'left';
        // Runs come newest first; paint oldest first.
        const starts = [];
        for (let i = 0; i < r.length; i += 13 + r[i + 12] * 3) starts.push(i);
        for (let j = starts.length - 1; j >= 0; j--) {
          const i = starts[j];
          const rgb = r[i + 2], n = r[i + 12];
          ctx.save();
          ctx.beginPath();
          ctx.rect(r[i + 5], r[i + 6], r[i + 7] - r[i + 5], r[i + 8] - r[i + 6]);
          ctx.clip();
          ctx.fillStyle = `rgb(${rgb & 255},${(rgb >> 8) & 255},${(rgb >> 16) & 255})`;
          ctx.font = `${r[i + 1]}px "${HD.fonts[r[i]].family}"`;
          // The game's bold is the glyph drawn again one pixel to the right.
          const steps = r[i + 3] ? Math.max(1, Math.ceil(sx)) : 0;
          for (let g = 0; g < n; g++) {
            const ch = String.fromCharCode(r[i + 13 + g * 3]), x = r[i + 14 + g * 3], y = r[i + 15 + g * 3];
            for (let t = 0; t <= steps; t++) ctx.fillText(ch, x + (steps ? t / steps : 0), y);
          }
          if (r[i + 4]) ctx.fillRect(r[i + 9], r[i + 11], r[i + 10] - r[i + 9], 1);
          ctx.restore();
        }
        gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, true);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, c);
        gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
        HD.textKey = { W, H, r };
      }
      gl.useProgram(HD.textProg.p);
      gl.uniform1i(HD.textProg.u.uText, 0);
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      gl.disable(gl.BLEND);
    },

    show(on) {
      if (HD.canvas) HD.canvas.style.display = on ? '' : 'none';
      // The SDL canvas stays in place for input; only its pixels are hidden.
      Module['canvas'].style.opacity = on ? '0' : '';
    },
  },

  hd_js_active__deps: ['$HD'],
  hd_js_active: function () {
    if (!HD.gl && !HD.failed) {
      HD.init();
      try {
        HD.setup();
      } catch (e) {
        err('hd: ' + e.message + '; presenting through SDL');
        HD.failed = true;
      }
    }
    const on = HD.enabled && !HD.failed;
    HD.show(on);
    // 2: the output size changed, so the terrain needs re-rendering.
    return !on ? 0 : HD.layout() ? 2 : 1;
  },

  hd_js_terrain_on__deps: ['$HD'],
  hd_js_terrain_on: function () { return HD.terrainOn ? 1 : 0; },

  hd_js_text_on__deps: ['$HD'],
  hd_js_text_on: function () { return HD.textOn ? 1 : 0; },

  // 1 once gdi.c's font `file` can be drawn here; the first call starts loading it.
  hd_js_font__deps: ['$HD'],
  hd_js_font: function (file, data, size) {
    let f = HD.fonts[file];
    if (!f) {
      f = HD.fonts[file] = { state: 0, family: 'simgolf-font-' + file };
      try {
        const face = new FontFace(f.family, HEAPU8.slice(data, data + size).buffer);
        face.load().then((ff) => { document.fonts.add(ff); f.state = 1; },
                         (e) => { f.state = -1; err('hd: font ' + file + ': ' + e); });
      } catch (e) {
        f.state = -1;
        err('hd: font ' + file + ': ' + e);
      }
    }
    return f.state === 1 ? 1 : 0;
  },

  hd_js_text__deps: ['$HD'],
  hd_js_text: function (runs, n) {
    HD.textRuns = n ? HEAPF32.slice(runs >> 2, (runs >> 2) + n) : null;
  },

  hd_js_texture__deps: ['$HD'],
  hd_js_texture: function (serial, w, h, rgba) {
    const have = HD.textures.get(serial);
    if (have) { have.used = HD.builds; return; }
    const gl = HD.gl;
    const t = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, t);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, HEAPU8.subarray(rgba, rgba + w * h * 4));
    gl.generateMipmap(gl.TEXTURE_2D);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
    HD.textures.set(serial, { tex: t, used: HD.builds });
  },

  hd_js_terrain__deps: ['$HD'],
  hd_js_terrain: function (verts, nverts, batches, nbatches, tw, th) {
    const gl = HD.gl;
    HD.builds++;
    // The framebuffer matches the output's pixel density over the 1:1 target.
    const fw = Math.max(1, Math.round(HD.canvas.width * tw / 800));
    const fh = Math.max(1, Math.round(HD.canvas.height * th / 600));
    if (HD.fboSize[0] !== fw || HD.fboSize[1] !== fh) {
      gl.bindTexture(gl.TEXTURE_2D, HD.terrainTex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, fw, fh, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
      gl.bindFramebuffer(gl.FRAMEBUFFER, HD.fbo);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, HD.terrainTex, 0);
      HD.fboSize = [fw, fh];
    }
    HD.terrainTarget = [tw, th];
    gl.bindFramebuffer(gl.FRAMEBUFFER, HD.fbo);
    gl.viewport(0, 0, fw, fh);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.useProgram(HD.terrainProg.p);
    gl.uniform2f(HD.terrainProg.u.uTarget, tw, th);
    gl.uniform1i(HD.terrainProg.u.uTex, 0);
    gl.bindVertexArray(HD.vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, HD.vbo);
    gl.bufferData(gl.ARRAY_BUFFER, HEAPF32.subarray(verts >> 2, (verts >> 2) + nverts * 8), gl.STREAM_DRAW);
    gl.activeTexture(gl.TEXTURE0);
    // Blending keeps the destination alpha, so uncovered pixels stay at 0.
    gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ZERO, gl.ONE);
    const b = HEAP32.subarray(batches >> 2, (batches >> 2) + nbatches * 5);
    let flags = -1;
    for (let k = 0; k < nbatches; k++) {
      const first = b[k * 5], count = b[k * 5 + 1], serial = b[k * 5 + 3], f = b[k * 5 + 4];
      if (f & 2) gl.bindTexture(gl.TEXTURE_2D, HD.textures.get(serial).tex);
      if (f !== flags) {
        gl.uniform1i(HD.terrainProg.u.uFlags, f);
        if (f & 1) gl.enable(gl.BLEND); else gl.disable(gl.BLEND);
        flags = f;
      }
      gl.drawArrays(gl.TRIANGLES, first, count);
    }
    gl.disable(gl.BLEND);
    gl.bindVertexArray(null);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    // Textures of a previous course type go once they've been unused a while.
    if (HD.builds % 64 === 0)
      for (const [serial, t] of HD.textures)
        if (HD.builds - t.used > 64) { gl.deleteTexture(t.tex); HD.textures.delete(serial); }
  },

  hd_js_frame__deps: ['$HD'],
  hd_js_frame: function (screen, w, h, mask) {
    const gl = HD.gl;
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, HD.screenTex);
    const px = HEAPU16.subarray(screen >> 1, (screen >> 1) + w * h);
    if (HD.srcSize[0] !== w || HD.srcSize[1] !== h) {
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.R16UI, w, h, 0, gl.RED_INTEGER, gl.UNSIGNED_SHORT, px);
      gl.bindTexture(gl.TEXTURE_2D, HD.maskTex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RG8, w, h, 0, gl.RG, gl.UNSIGNED_BYTE, null);
      HD.srcSize = [w, h];
    } else {
      gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, w, h, gl.RED_INTEGER, gl.UNSIGNED_SHORT, px);
    }
    const useTerrain = mask && HD.fboSize[0] > 0;
    if (useTerrain) {
      gl.activeTexture(gl.TEXTURE1);
      gl.bindTexture(gl.TEXTURE_2D, HD.maskTex);
      gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, w, h, gl.RG, gl.UNSIGNED_BYTE, HEAPU8.subarray(mask, mask + w * h * 2));
      gl.activeTexture(gl.TEXTURE2);
      gl.bindTexture(gl.TEXTURE_2D, HD.terrainTex);
    }
    const P = HD.frameProg;
    gl.useProgram(P.p);
    gl.uniform1i(P.u.uScreen, 0);
    gl.uniform1i(P.u.uMask, 1);
    gl.uniform1i(P.u.uTerrain, 2);
    gl.uniform2f(P.u.uSrc, w, h);
    const tt = HD.terrainTarget || [w, h];
    gl.uniform2f(P.u.uTerrainScale, w / tt[0], h / tt[1]);
    gl.uniform1f(P.u.uScale, HD.canvas.width / w);
    gl.uniform1i(P.u.uFilter, HD.filter);
    gl.uniform1i(P.u.uUseTerrain, useTerrain ? 1 : 0);
    gl.uniform1i(P.u.uDebug, HD.debug);
    gl.viewport(0, 0, HD.canvas.width, HD.canvas.height);
    gl.bindVertexArray(HD.emptyVao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    if (HD.textRuns) HD.drawText(w, h);
    gl.bindVertexArray(null);
    gl.activeTexture(gl.TEXTURE0);
  },
});
