// Video playback for src/port/golf/video.c: a WebM from the game FS in a
// <video> element over the canvas, awaited through JSPI. A click or key press
// skips it. The intro plays in the background instead: the game goes on
// loading under it (muted), so skipping it lands straight on the main menu.

#include <emscripten.h>
#include <stdio.h>
#include <string.h>

#include "fs.h"

EM_ASYNC_JS(void, port_play_video, (const char *path, int background), {
  const name = UTF8ToString(path);
  let data;
  try { data = FS.readFile(name); } catch (e) { err('video: cannot read ' + name); return; }
  const url = URL.createObjectURL(new Blob([data], { type: 'video/webm' }));
  const canvas = Module['canvas'];
  const r = canvas.getBoundingClientRect();
  const v = document.createElement('video');
  v.src = url;
  v.playsInline = true;
  Object.assign(v.style, {
    position: 'fixed', left: r.left + 'px', top: r.top + 'px', width: r.width + 'px', height: r.height + 'px',
    background: 'black', zIndex: 10, objectFit: 'contain', cursor: 'none',
  });
  document.body.appendChild(v);
  let onKey;
  const shown = new Promise((resolve) => {
    let done = false;
    const finish = () => { if (!done) { done = true; resolve(); } };
    v.addEventListener('ended', finish);
    v.addEventListener('error', finish);
    v.addEventListener('pointerdown', (e) => { e.preventDefault(); e.stopPropagation(); finish(); });
    onKey = (e) => { e.preventDefault(); e.stopPropagation(); finish(); };
    addEventListener('keydown', onKey, true);
    v.play().catch(() => {
      // autoplay with sound needs a user gesture first: play muted instead
      v.muted = true;
      v.play().catch(finish);
    });
  });
  // Web Audio's master gain; the sound device may only appear mid-video.
  const gain = (value) => { const s = Module['soundDebug']; if (s && s.master) s.master.gain.value = value; };
  const cleanup = () => {
    removeEventListener('keydown', onKey, true);
    v.pause();
    v.remove();
    URL.revokeObjectURL(url);
    if (background) gain(1);
  };
  if (background) {
    gain(0);
    v.addEventListener('timeupdate', () => gain(0));
    shown.then(cleanup);
    return;
  }
  await shown;
  cleanup();
});

// ?novideos in the page URL (the headless test runs) skips them.
EM_JS(int, port_no_videos, (void), { return Module['noVideos'] ? 1 : 0; });

// Plays the WebM next to a game .bik path, unless ?novideos. With background
// set it returns at once and the video stays up until it ends or is skipped.
void port_play_game_video(const char *dospath, int background) {
  if (port_no_videos()) return;
  char dos[260], real[512];
  snprintf(dos, sizeof dos, "%s", dospath);
  char *dot = strrchr(dos, '.');
  if (dot) snprintf(dot, sizeof dos - (dot - dos), ".webm");
  if (!fs_resolve(dos, real, sizeof real, 0)) {
    fprintf(stderr, "video: %s not found (convert it with tools/video/convert-bik.sh)\n", dos);
    return;
  }
  port_play_video(real, background);
}
