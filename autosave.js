// Save history kept by the page, independent of the game's own save slots.
//
// The game autosaves every 512 game ticks (about 45 s of unpaused play) into
// two alternating files, &AutoSave1 and &AutoSave2, and src/port/fs.c mirrors
// everything it writes into IndexedDB. That leaves two gaps this file closes:
// a bug that corrupts the game state overwrites both autosaves within a minute
// and a half, and nothing outside the browser holds a copy.
//
// Every .sve the game finishes writing is copied into its own IndexedDB
// database (a file the game was still writing when it crashed never gets
// here, because fs.c reports a file only after it's closed). The newest 20
// copies are kept, plus one per half hour before that, up to 20 more. A small
// panel (the "Saves" button, or Ctrl+F8) lists them: Restore copies one back
// into Saved Games under a new name, to be loaded from the game's Load menu;
// Download and Import move .sve files in and out of the browser.
(function () {
  'use strict';
  var KEEP_RECENT = 20, KEEP_OLDER = 20, OLDER_SPACING = 30 * 60 * 1000;
  var SKIP = /^while_browsing\.sve$/i;  // written whenever a course is browsed

  var dbp = null;
  function db() {
    // Opened without a version so a database that exists without the store
    // (created by something else) gets upgraded instead of breaking.
    function openv(v, res, rej) {
      var r = v ? indexedDB.open('simgolf-snapshots', v) : indexedDB.open('simgolf-snapshots');
      r.onupgradeneeded = function () {
        if (!r.result.objectStoreNames.contains('snaps')) r.result.createObjectStore('snaps', { keyPath: 'id', autoIncrement: true });
      };
      r.onsuccess = function () {
        var d = r.result;
        if (d.objectStoreNames.contains('snaps')) return res(d);
        var next = d.version + 1;
        d.close();
        openv(next, res, rej);
      };
      r.onerror = function () { rej(r.error); };
    }
    if (!dbp) dbp = new Promise(function (res, rej) { openv(0, res, rej); });
    return dbp;
  }
  function req(r) {
    return new Promise(function (res, rej) {
      r.onsuccess = function () { res(r.result); };
      r.onerror = function () { rej(r.error); };
    });
  }
  function store(mode) {
    return db().then(function (d) { return d.transaction('snaps', mode).objectStore('snaps'); });
  }
  // Metadata only, newest first (getAll would read every snapshot's bytes).
  function list() {
    return store('readonly').then(function (s) {
      return new Promise(function (res, rej) {
        var out = [], c = s.openCursor(null, 'prev');
        c.onsuccess = function () {
          var cur = c.result;
          if (!cur) return res(out);
          var v = cur.value;
          out.push({ id: v.id, name: v.name, time: v.time, size: v.size, hash: v.hash });
          cur.continue();
        };
        c.onerror = function () { rej(c.error); };
      });
    });
  }
  function get(id) { return store('readonly').then(function (s) { return req(s.get(id)); }); }

  function hash(bytes) {
    var h = 0x811c9dc5;
    for (var i = 0; i < bytes.length; i++) h = Math.imul(h ^ bytes[i], 0x01000193);
    return (h >>> 0).toString(16) + ':' + bytes.length;
  }

  function prune(snaps) {
    var drop = [], lastKept = Infinity, older = 0;
    snaps.forEach(function (s, i) {
      if (i < KEEP_RECENT) { lastKept = s.time; return; }
      if (older < KEEP_OLDER && lastKept - s.time >= OLDER_SPACING) { lastKept = s.time; older++; return; }
      drop.push(s.id);
    });
    if (!drop.length) return;
    return store('readwrite').then(function (s) { drop.forEach(function (id) { s.delete(id); }); });
  }

  var queue = Promise.resolve();
  function record(rel, data) {
    var name = rel.split('/').pop();
    if (!/\.sve$/i.test(name) || SKIP.test(name) || data.length < 1024) return;
    toast(/^&autosave/i.test(name) ? 'Autosaved' : 'Saved');
    var bytes = data.slice(), h = hash(bytes);
    queue = queue.then(function () {
      return list().then(function (snaps) {
        if (snaps.length && snaps[0].hash === h) return;  // same game state written again
        return store('readwrite')
          .then(function (s) { return req(s.add({ name: name, time: Date.now(), size: bytes.length, hash: h, bytes: bytes })); })
          .then(list).then(prune);
      });
    }).then(refresh).catch(function (e) { console.warn('autosave: ' + e); });
  }

  // A short notice each time the game writes a save, so players can see
  // autosave working (it only comes after ~45 s of unpaused play).
  var toastEl = null, toastTimer = 0;
  function toast(text) {
    if (!toastEl) {
      toastEl = document.createElement('div');
      toastEl.id = 'sv-toast';
      toastEl.setAttribute('role', 'status');
      document.body.appendChild(toastEl);
    }
    toastEl.textContent = text + ' ' + new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    toastEl.className = 'on';
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { toastEl.className = ''; }, 2500);
  }

  // ---- page hooks
  var Module = window.Module;
  Module.onGameFileWritten = record;
  function flush() { if (Module.persistFlush) Module.persistFlush(); }
  addEventListener('pagehide', flush);
  document.addEventListener('visibilitychange', function () { if (document.hidden) flush(); });

  var crashed = false;
  function onCrash() {
    if (crashed) return;
    crashed = true;
    flush();
    open(true);
  }
  var prevAbort = Module.onAbort;
  Module.onAbort = function (what) { if (prevAbort) prevAbort(what); onCrash(); };
  addEventListener('error', onCrash);

  // ---- panel
  var css = document.createElement('style');
  css.textContent =
    '#sv-btn{position:fixed;right:10px;bottom:10px;z-index:1000;font:12px system-ui,sans-serif;' +
    'background:#222;color:#ddd;border:1px solid #444;border-radius:4px;padding:4px 9px;opacity:.4;cursor:pointer}' +
    '#sv-btn:hover{opacity:1}' +
    '#sv-panel{position:fixed;right:10px;bottom:42px;z-index:1000;width:min(380px,calc(100vw - 20px));' +
    'max-height:70vh;overflow:auto;font:13px system-ui,sans-serif;background:#1b1b1b;color:#ddd;' +
    'border:1px solid #444;border-radius:6px;padding:10px 12px;box-shadow:0 6px 24px #000a;display:none}' +
    '#sv-panel h3{margin:0 0 6px;font-size:14px}' +
    '#sv-panel .note{color:#aaa;margin:0 0 8px;line-height:1.35}' +
    '#sv-panel .crash{background:#4a1f1f;color:#fdd;padding:6px 8px;border-radius:4px;margin-bottom:8px;line-height:1.35}' +
    '#sv-panel .row{display:flex;align-items:center;gap:6px;padding:4px 0;border-top:1px solid #2c2c2c}' +
    '#sv-panel .row span{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}' +
    '#sv-panel .row small{color:#888}' +
    '#sv-panel button{font:12px system-ui,sans-serif;background:#2c2c2c;color:#ddd;border:1px solid #555;' +
    'border-radius:3px;padding:2px 7px;cursor:pointer}' +
    '#sv-panel button:hover{background:#3a3a3a}' +
    '#sv-panel .bar{display:flex;gap:6px;margin-bottom:8px}' +
    '#sv-msg{color:#9d9;margin:6px 0 0;min-height:1em}' +
    '#sv-toast{position:fixed;left:50%;top:12px;transform:translateX(-50%);z-index:1001;pointer-events:none;' +
    'font:13px system-ui,sans-serif;background:#1b1b1bd9;color:#cfc;border:1px solid #4a4;border-radius:12px;' +
    'padding:3px 12px;opacity:0;transition:opacity .4s}' +
    '#sv-toast.on{opacity:1}';
  document.head.appendChild(css);

  var btn = document.createElement('button');
  btn.id = 'sv-btn';
  btn.textContent = 'Saves';
  btn.title = 'Save history (Ctrl+F8)';
  var panel = document.createElement('div');
  panel.id = 'sv-panel';
  var file = document.createElement('input');
  file.type = 'file';
  file.accept = '.sve';
  file.multiple = true;
  file.style.display = 'none';
  document.body.appendChild(btn);
  document.body.appendChild(panel);
  document.body.appendChild(file);
  // Keep clicks and keys in the panel away from the game.
  ['mousedown', 'mouseup', 'click', 'keydown', 'keyup', 'wheel'].forEach(function (t) {
    panel.addEventListener(t, function (e) { e.stopPropagation(); });
    btn.addEventListener(t, function (e) { e.stopPropagation(); });
  });

  // The button sits in the margin beside the game; with no margin it hides
  // (Ctrl+F8 still opens the panel, and a crash opens it anyway).
  function place() {
    var c = document.getElementById('canvas');
    var r = c ? c.getBoundingClientRect() : { right: 0, bottom: 0 };
    var room = innerWidth - r.right >= 80 || innerHeight - r.bottom >= 36;
    btn.style.display = room || shown ? '' : 'none';
  }
  addEventListener('resize', place);
  place();

  var shown = false, msg = '';
  function open(v) { shown = v; panel.style.display = v ? 'block' : 'none'; place(); if (v) refresh(); }
  btn.addEventListener('click', function () { open(!shown); });
  addEventListener('keydown', function (e) {
    if (e.ctrlKey && e.key === 'F8') { e.preventDefault(); e.stopImmediatePropagation(); open(!shown); }
  }, true);

  function pad(n) { return (n < 10 ? '0' : '') + n; }
  function when(t) {
    var d = new Date(t), now = new Date();
    var hm = pad(d.getHours()) + ':' + pad(d.getMinutes());
    if (d.toDateString() === now.toDateString()) return hm;
    return d.toLocaleDateString(undefined, { month: 'short', day: 'numeric' }) + ' ' + hm;
  }
  function stamp(t) {
    var d = new Date(t);
    return d.getFullYear() + pad(d.getMonth() + 1) + pad(d.getDate()) + ' ' + pad(d.getHours()) + pad(d.getMinutes());
  }
  function label(name) { return name.replace(/\.sve$/i, '').replace(/^&/, 'autosave: '); }
  function esc(s) { return String(s).replace(/[&<>"]/g, function (c) { return '&#' + c.charCodeAt(0) + ';'; }); }

  function download(snap) {
    var a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([snap.bytes], { type: 'application/octet-stream' }));
    a.download = snap.name.replace(/^&/, '').replace(/\.sve$/i, '') + ' ' + stamp(snap.time) + '.sve';
    a.click();
    setTimeout(function () { URL.revokeObjectURL(a.href); }, 1000);
  }
  function restore(snap) {
    if (!Module.saveImport) { say('The game hasn\'t started yet.'); return; }
    var name = 'Restored ' + stamp(snap.time).slice(4) + '.sve';
    Module.saveImport(name, snap.bytes);
    say('Added "' + name.slice(0, -4) + '" to your saved games. Open it from the game\'s Load menu.');
  }
  function say(m) { msg = m; var el = document.getElementById('sv-msg'); if (el) el.textContent = m; }

  file.addEventListener('change', function () {
    var files = Array.prototype.slice.call(file.files);
    file.value = '';
    if (!Module.saveImport) { say('The game hasn\'t started yet.'); return; }
    Promise.all(files.map(function (f) {
      return f.arrayBuffer().then(function (b) {
        var name = f.name.replace(/[\\/:*?"<>|]/g, '_');
        if (!/\.sve$/i.test(name)) name += '.sve';
        Module.saveImport(name, new Uint8Array(b));
        return name.slice(0, -4);
      });
    })).then(function (names) {
      say('Imported ' + names.map(function (n) { return '"' + n + '"'; }).join(', ') + '. Open it from the game\'s Load menu.');
    });
  });

  function refresh() {
    if (!shown) return;
    list().then(function (snaps) {
      var h = '<h3>Save history</h3>';
      if (crashed) {
        h += '<div class="crash">The game crashed. Your saves are kept: reload the page and load the newest ' +
          'autosave from the Load menu, or restore an earlier copy below if that one misbehaves.</div>';
      }
      h += '<p class="note">Every save and autosave the game writes is copied here. Restore puts a copy back ' +
        'into your saved games.</p>';
      var sync = Module.saveSync;
      if (sync) {
        h += '<p class="note">' + (!sync.server ? 'Saves are kept in this browser only (no save server).'
          : sync.error ? 'Couldn\'t reach the save server: ' + esc(sync.error) + '. Saves are kept here and sent the next time the page loads.'
          : 'Saves sync with the server, so they follow you to other browsers' +
            (sync.last ? ' (last synced ' + esc(when(sync.last)) + ').' : '.')) + '</p>';
      }
      h += '<div class="bar"><button data-a="import">Import .sve…</button>' +
        (snaps.length ? '<button data-a="latest">Download newest</button>' : '') + '</div>';
      if (!snaps.length) h += '<p class="note">Nothing yet. The game autosaves after about 45 seconds of play.</p>';
      snaps.forEach(function (s) {
        h += '<div class="row"><span>' + esc(when(s.time)) + ' · ' + esc(label(s.name)) +
          ' <small>' + Math.round(s.size / 1024) + ' KB</small></span>' +
          '<button data-a="restore" data-id="' + s.id + '">Restore</button>' +
          '<button data-a="dl" data-id="' + s.id + '">Download</button></div>';
      });
      h += '<p id="sv-msg">' + esc(msg) + '</p>';
      if (Module.savePanelFooter) h += Module.savePanelFooter;   // extra controls from the page
      panel.innerHTML = h;
    }).catch(function (e) { panel.textContent = 'Save history unavailable: ' + e; });
  }
  panel.addEventListener('click', function (e) {
    var a = e.target.getAttribute && e.target.getAttribute('data-a');
    if (!a) return;
    if (a === 'import') { file.click(); return; }
    if (Module.onSavePanelAction && Module.onSavePanelAction(a)) return;
    var id = a === 'latest' ? null : Number(e.target.getAttribute('data-id'));
    (id === null ? list().then(function (s) { return get(s[0].id); }) : get(id)).then(function (snap) {
      if (!snap) return;
      if (a === 'restore') restore(snap); else download(snap);
    });
  });
})();
