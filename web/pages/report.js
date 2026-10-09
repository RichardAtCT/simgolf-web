// "Report a problem" for the GitHub Pages build: a form over the page that
// files the report as a GitHub issue on RichardAtCT/simgolf-web, with the
// browser, build and recent game log attached (shown to the player first).
//
// With REPORT_ENDPOINT set, the form posts there and the report is filed
// without leaving the page (a small relay that holds the GitHub token).
// Without it, GitHub's new-issue page opens in a new tab with everything
// filled in, and the player presses Submit there.
(function () {
  var REPO = 'RichardAtCT/simgolf-web';
  var REPORT_ENDPOINT = '';   // e.g. 'https://simgolf-report.example.workers.dev/report'
  var BUILD = document.documentElement.getAttribute('data-build') || 'dev';

  // ---- recent console lines and errors ----
  var lines = [], crash = null;
  ['log', 'warn', 'error'].forEach(function (k) {
    var orig = console[k];
    console[k] = function () {
      try {
        var msg = Array.prototype.map.call(arguments, String).join(' ');
        lines.push((k === 'warn' ? 'warn: ' : '') + msg);   // the game's stderr comes through console.error
        if (lines.length > 200) lines.shift();
      } catch (e) {}
      return orig.apply(console, arguments);
    };
  });
  function noteCrash(what) { if (!crash) crash = String(what).slice(0, 2000); }
  addEventListener('error', function (e) { noteCrash((e.message || 'error') + (e.error && e.error.stack ? '\n' + e.error.stack : '')); });
  addEventListener('unhandledrejection', function (e) { noteCrash('unhandled rejection: ' + (e.reason && (e.reason.stack || e.reason))); });
  var started = false;
  Module.postRun = (Module.postRun || []).concat(function () { started = true; });
  var prevAbort = Module.onAbort;
  Module.onAbort = function (what) { noteCrash('abort: ' + what); if (prevAbort) prevAbort(what); };

  // ---- styles (same look as the setup page) ----
  var css = document.createElement('style');
  css.textContent =
    '#rp-back{position:fixed;inset:0;z-index:2147483600;background:rgba(22,18,46,.55);display:none;align-items:center;justify-content:center;padding:16px;box-sizing:border-box}' +
    '#rp-back.on{display:flex}' +
    '#rp{width:100%;max-width:560px;max-height:100%;overflow:auto;box-sizing:border-box;padding:22px 24px;border-radius:28px;color:#2a2154;' +
      'font:500 15px/1.45 Nunito,system-ui,sans-serif;background:linear-gradient(#fffdf0,#fbf8e4);border:4px solid #241078;box-shadow:0 0 0 3px #fff inset,0 8px 0 rgba(36,16,120,.35)}' +
    '#rp h2{margin:0 0 4px;font:800 26px/1.1 "Baloo 2",Nunito,sans-serif;color:#1f9a3a;-webkit-text-stroke:1px #0d5c22}' +
    '#rp p{margin:0 0 12px}' +
    '#rp label{display:block;font-weight:800;margin:12px 0 4px}' +
    '#rp textarea,#rp input[type=text]{width:100%;box-sizing:border-box;font:500 15px/1.4 Nunito,system-ui,sans-serif;color:#2a2154;background:#fff;border:3px solid #8f89d9;border-radius:14px;padding:8px 12px}' +
    '#rp textarea:focus,#rp input:focus{outline:none;border-color:#3b19c4}' +
    '#rp details{margin:12px 0;font-size:13px}#rp summary{cursor:pointer;font-weight:800}' +
    '#rp pre{white-space:pre-wrap;word-break:break-word;max-height:180px;overflow:auto;background:#ece6c4;color:#5a2810;border-radius:10px;padding:8px 10px;font:12px/1.35 ui-monospace,Menlo,monospace}' +
    '#rp .row{display:flex;gap:10px;justify-content:flex-end;margin-top:16px;flex-wrap:wrap}' +
    '#rp button{font:800 16px/1 "Baloo 2",Nunito,sans-serif;padding:11px 20px;border-radius:30px;cursor:pointer;border:3px solid #241078;color:#241078;background:#fbf8e4;box-shadow:0 4px 0 rgba(36,16,120,.35)}' +
    '#rp button.go{background:linear-gradient(#fff27a,#ffd81a 55%,#f2b800)}' +
    '#rp button:active{transform:translateY(2px);box-shadow:0 2px 0 rgba(36,16,120,.35)}' +
    '#rp .note{font-size:13px;color:#5b4f8f}#rp .bad{color:#b3261e;font-weight:700}#rp .ok{color:#0d5c22;font-weight:800}' +
    '#rp .hp{position:absolute;left:-9999px}';
  document.head.appendChild(css);

  var back = document.createElement('div');
  back.id = 'rp-back';
  back.innerHTML =
    '<form id="rp" autocomplete="off">' +
    '<h2>Report a problem</h2>' +
    '<p class="note">This goes to the project\'s <a href="https://github.com/' + REPO + '/issues" target="_blank" rel="noopener">issue list on GitHub</a>, where anyone can read it.</p>' +
    '<label for="rp-what">What went wrong?</label>' +
    '<textarea id="rp-what" rows="3" required maxlength="4000" placeholder="e.g. The game froze when I opened the clubhouse menu"></textarea>' +
    '<label for="rp-doing">What were you doing just before? <span class="note">(optional)</span></label>' +
    '<textarea id="rp-doing" rows="2" maxlength="2000"></textarea>' +
    '<input class="hp" id="rp-hp" type="text" tabindex="-1" aria-hidden="true">' +
    '<details><summary>Technical details that will be included</summary><pre id="rp-tech"></pre></details>' +
    '<div id="rp-msg" role="status"></div>' +
    '<div class="row"><button type="button" id="rp-cancel">Cancel</button><button type="submit" class="go" id="rp-send">Send report</button></div>' +
    '</form>';
  document.body.appendChild(back);
  var form = back.querySelector('#rp'), msgEl = back.querySelector('#rp-msg');

  function tech() {
    var c = Module.canvas;
    var parts = [
      'Build: ' + BUILD,
      'Page: ' + location.href.split('?')[0],
      'Browser: ' + navigator.userAgent,
      'Window: ' + innerWidth + 'x' + innerHeight + ' @' + devicePixelRatio + 'x',
      'Game started: ' + (started ? 'yes' : 'no') + (c ? ', canvas ' + c.width + 'x' + c.height : ''),
    ];
    if (crash) parts.push('Crash: ' + crash);
    return parts.join('\n');
  }
  function logTail(maxChars) {
    var out = [], n = 0;
    for (var i = lines.length - 1; i >= 0 && n < maxChars; i--) { var l = lines[i].slice(0, 300); out.unshift(l); n += l.length + 1; }
    return out.join('\n');
  }
  function body(what, doing, logChars) {
    return '### What went wrong\n' + what + '\n\n' +
      (doing ? '### What I was doing\n' + doing + '\n\n' : '') +
      '<details><summary>Technical details</summary>\n\n```\n' + tech() + '\n```\n\nRecent log:\n\n```\n' + logTail(logChars) + '\n```\n</details>\n\n' +
      '_Sent from the in-game report form._';
  }

  function open(prefill) {
    msgEl.textContent = ''; msgEl.className = '';
    back.querySelector('#rp-what').value = prefill || '';
    back.querySelector('#rp-tech').textContent = tech() + '\n\nRecent log (last lines):\n' + logTail(3000);
    back.querySelector('#rp-send').disabled = false;
    back.classList.add('on');
    setTimeout(function () { back.querySelector('#rp-what').focus(); }, 0);
  }
  function close() { back.classList.remove('on'); }
  back.querySelector('#rp-cancel').onclick = close;
  back.addEventListener('pointerdown', function (e) { if (e.target === back) close(); });
  // Keep the game from seeing keys typed into the form
  ['keydown', 'keyup', 'keypress'].forEach(function (t) {
    back.addEventListener(t, function (e) { e.stopPropagation(); if (e.key === 'Escape') close(); });
  });

  form.onsubmit = function (e) {
    e.preventDefault();
    var what = back.querySelector('#rp-what').value.trim(), doing = back.querySelector('#rp-doing').value.trim();
    if (!what) return;
    var title = what.split('\n')[0].slice(0, 80);
    if (REPORT_ENDPOINT) {
      back.querySelector('#rp-send').disabled = true;
      msgEl.className = ''; msgEl.textContent = 'Sending…';
      fetch(REPORT_ENDPOINT, {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ title: title, body: body(what, doing, 12000), hp: back.querySelector('#rp-hp').value, build: BUILD }),
      }).then(function (r) { return r.json().catch(function () { return {}; }).then(function (j) { if (!r.ok) throw new Error(j.error || r.status); return j; }); })
        .then(function (j) {
          msgEl.className = 'ok';
          msgEl.innerHTML = 'Thanks! Your report was filed' + (j.url ? ' as <a href="' + j.url + '" target="_blank" rel="noopener">issue #' + j.number + '</a>' : '') + '.';
          setTimeout(close, 4000);
        }, function (err) {
          back.querySelector('#rp-send').disabled = false;
          msgEl.className = 'bad'; msgEl.textContent = "Couldn't send it (" + err.message + '). Opening GitHub instead…';
          setTimeout(function () { viaGitHub(title, what, doing); }, 1200);
        });
      return;
    }
    viaGitHub(title, what, doing);
  };

  function viaGitHub(title, what, doing) {
    // GitHub's new-issue page takes the text in the URL; keep it under ~7,500 characters.
    var base = 'https://github.com/' + REPO + '/issues/new?labels=player-report&title=' + encodeURIComponent(title) + '&body=';
    var logChars = 4000, url;
    do { url = base + encodeURIComponent(body(what, doing, logChars)); logChars -= 500; } while (url.length > 7500 && logChars > 0);
    window.open(url, '_blank', 'noopener');
    msgEl.className = 'ok';
    msgEl.textContent = 'GitHub opened in a new tab with your report filled in. Press "Create" there to send it (you need a free GitHub account).';
  }

  window.simgolfReport = { open: open };
})();
