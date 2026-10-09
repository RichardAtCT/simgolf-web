// sound.h over Web Audio. This side keeps golf's registration rules and maps
// the game's DOS paths to converted asset files; src/platform/sound_web.js
// does the loading and playback.
//
// The game names files like "sounds\Golf sfx\Iron.wav", relative to the
// install folder. The asset tree differs in case and has some spaces turned
// into underscores ("Sounds/Golf_Sfx/Iron.wav"), so paths are matched through
// a list of the real files (/assets/sound-files.txt, written by CMake) after
// folding case, separators and underscores.

#include "platform/sound.h"

#include <stdio.h>

#include <string>
#include <unordered_map>

namespace {

const char *kManifest = "/assets/sound-files.txt";
const char *kUrlBase = "assets/";   // served next to simgolf.html

bool g_ready;
bool g_registered[SND_MAX_SOUNDS];
std::unordered_map<std::string, std::string> g_files;   // folded path -> real path

std::string fold(const char *path) {
  std::string out;
  for (const char *p = path; *p; p++) {
    char c = *p;
    if (c == '\\') c = '/';
    else if (c == '_') c = ' ';
    else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (c == '/' && out.empty()) continue;
    out += c;
  }
  if (out.compare(0, 2, "./") == 0) out.erase(0, 2);
  return out;
}

void loadManifest() {
  FILE *f = fopen(kManifest, "r");
  if (!f) {
    printf("sound: %s missing, no sounds will play\n", kManifest);
    return;
  }
  char line[512];
  while (fgets(line, sizeof line, f)) {
    std::string path(line);
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
    if (!path.empty()) g_files.emplace(fold(path.c_str()), path);
  }
  fclose(f);
}

}  // namespace

extern "C" {
void snd_js_init(void);
void snd_js_register(int index, const char *url, int single);
void snd_js_play(int index, int volume, int pan, int pitchCents, int delayMs);
void snd_js_fade_out(int index, int ms);
void snd_js_stop(int index);
void snd_js_shutdown(void);
}

void snd_init(void) {
  if (g_ready) return;
  loadManifest();
  snd_js_init();
  g_ready = true;
}

void snd_register(int index, const char *dosPath, unsigned flags) {
  if (!g_ready || index < 0 || index >= SND_MAX_SOUNDS || !dosPath) return;
  // The DLL object comes from the first registration; later ones for the same
  // index never reach it, so the first file is the one that plays.
  if (g_registered[index]) return;
  g_registered[index] = true;

  auto it = g_files.find(fold(dosPath));
  if (it == g_files.end()) {
    printf("sound: #%d %s not found in the assets\n", index, dosPath);
    snd_js_register(index, nullptr, 0);
    return;
  }
  // Each flag 0x10 sound had one dedicated DirectSound buffer, so (inferred)
  // one instance at a time. Big files get the same rule in JS once their size
  // is known, because sound.dll streamed them.
  std::string url = kUrlBase + it->second;
  snd_js_register(index, url.c_str(), (flags & SND_FLAG_DEDICATED) != 0);
}

void snd_play(int index, int volume, int pan, int pitchCents, int delayMs) {
  if (!g_ready || index < 0 || index >= SND_MAX_SOUNDS) return;
  snd_js_play(index, volume, pan, pitchCents, delayMs);
}

void snd_fade_out(int index, int ms) {
  if (!g_ready || index < 0 || index >= SND_MAX_SOUNDS) return;
  snd_js_fade_out(index, ms);
}

void snd_stop(int index) {
  if (!g_ready || index < 0 || index >= SND_MAX_SOUNDS) return;
  snd_js_stop(index);
}

void snd_shutdown(void) {
  if (!g_ready) return;
  snd_js_shutdown();
  g_files.clear();
  for (bool &r : g_registered) r = false;
  g_ready = false;
}
