# SimGolf sound.dll — reverse-engineering notes for the browser port

Sources: `sound.dll` (SimGolfDeps project, image base 0x10000000, 1847 fns) and `golf_nocd.exe` (golf.exe baseline).
Working files (all under `~/ghidra_work/sound/`):

| file | contents |
|---|---|
| `out/` | DumpSummary of sound.dll (summary.txt, strings.txt, decomp_all.c) |
| `vt/vt_<addr>.c` | every slot of the sound.dll vtables, decompiled (Wave sound 1005c7e8, Wave_Device 1005b304, Midi_Device 1005b28c, Wave_In_Device 1005b3d4, MIDI sound 1005b6a8) |
| `golfvt/vt_<addr>.c` | golf.exe's mirror/proxy classes (4bac68, 4bacf8, 4badec, 4badf8, 4baeac, 4baf00, 4bafb0, 4bb014) |
| `golf_sound_layer.c` | golf decompile 0x483010–0x488230 (sound glue + some unrelated PCX/Bink code) |
| `golf_calls.txt`, `golf_calls2.txt` | xrefs to golf's sound array / device globals / wrapper functions, each with the CALL that follows |
| `golf_disasm.txt` | disassembly of golf 0x448220, 0x45baf0, 0x483e90, 0x483f10, 0x40f5c0, 0x43cce0, 0x4490b0 |
| `sound_index.txt` | **sound index → WAV file → flags table** (all 219 registrations) |
| `scripts/` | helper scripts (pe_exports.py, thunk.py, vt.py, rd.py, DumpVtables.java, DumpDisasm.java, GolfSoundCalls.java) |

Conventions: "slot +0xNN" = byte offset into a vtable. **(guess)** marks inferences not directly proven by the code.
All golf.exe addresses are golf_nocd.exe addresses.

---

## 0. The big picture (read this first)

* sound.dll is a complete, self-contained **software mixer** built on DirectSound (plus WINMM mmio for WAV parsing, a WINMM multimedia timer, a hidden message window/thread, a MIDI-out path and a waveIn "voice chat" path).
  * Output: one DirectSound secondary streaming buffer (64 KB, `DSBCAPS_GETCURRENTPOSITION2|CTRLVOLUME|LOCSOFTWARE` = 0x10088), format **44100 Hz, stereo, 16-bit**; mixing happens every **30 ms** (`timeSetEvent(30)`, 1323 frames per tick) on the timer thread.
  * Every sound can have **up to 16 simultaneous voices** (instances) if its data is in memory; volume/pan/pitch/fades are applied per voice in software; resampling handles any input rate; 8- and 16-bit, mono and stereo PCM.
  * Sounds whose `data` chunk is ≤ 128 KB are fully cached; bigger ones are **streamed from disk** through a 128 KB mmio buffer (single voice only).
  * Optional per-sound "dedicated DirectSound buffer" mode (flag 0x10): the whole file is loaded into its own static DS secondary buffer (`0x100e0` = CTRLFREQUENCY|CTRLPAN|CTRLVOLUME|GETCURRENTPOSITION2) and DirectSound does volume/pan/frequency. SimGolf uses this for its music/jingles.
  * 3D (DS3D) buffers exist (flag 0x40 / device flag 0x20) but **SimGolf never enables them**.
* golf.exe does **not** call the DLL objects from game code directly. It compiles in a copy of the sound library's *header-side* classes: a proxy base class (same field layout as the DLL's base `Sound` class, vtable 0x4bac68) and a 61-slot proxy `Wave sound` class (vtable 0x4bacf8) that holds the DLL object at `+0x40` and forwards nearly every slot 1:1 to the same slot of the DLL object. Device wrappers likewise hold the DLL device at `+0x14` and forward slot-for-slot.
* The game itself uses a tiny, index-based API (300-entry array of wrapper objects at **0x0080d840**, stride 0x6c):
  * `register_all_sounds()` **0x00448220** – 219 × `wrapper->setup_and_load(filename, flags)` (golf slot +0x90 = FUN_00484e30), flags = 4 (lazy) or 0x14 (lazy + dedicated DS buffer).
  * `play_sound(idx, vol, pan, pitch_cents, delay_ms)` **0x004481b0** – 51 call sites.
  * `fade_out_sound(idx)` **0x00448200** – fade out over 1000 ms – 3 call sites (music/ambience cross-fades).
  * stop of the ambience loop (#45) in **0x0040f5c0** (the main game loop) at 0x004104f7 – 1 site.
  * `release_all_sounds()` **0x004490b0** – shutdown.
* **MIDI is not used.** The MIDI device object is created but `midiOutOpen` is skipped (golf passes no 0x100 flag), and no .mid/.rmi file is ever created. `Sounds\Music` contains only WAVs. Wave-in (voice chat) device is created/initialised but never used by game code.
* No master volume, no listener, no 3D, no completion callbacks are used by the game.

A port therefore only needs: decode PCM WAV (+ `smpl` loop chunk), play up to N overlapping instances with linear volume/pan, pitch shift, start delay, linear fade-out, stop, and looping streams for music/ambience.

---

## 1. How golf.exe loads and initialises sound

### 1.1 Loading (FUN_00483fc0 @ 0x00483fc0)
```
if (hSoundDll == NULL) {                              // DAT_0083afc0
    hSoundDll = LoadLibraryA(".\\sound.dll");         // string @ 0x004e43bc
    if (!hSoundDll) return 1;
    for (i = 0; i < 11; i++) {                        // table 0x0083af6c..0x0083af94
        pfn[i] = GetProcAddress(hSoundDll, MAKEINTRESOURCE(i + 1));   // BY ORDINAL 1..11
        if (!pfn[i]) { unload (FUN_00484060); soundOK = 0; return 1; }
    }
    v = get_sound_version();                          // via FUN_004840c0
    if ((v & 0xff00) != 0x0c00)
        MessageBoxA(0, "The sound header files used in the game do not match the ones used in sound.dll. Check all sound.h and sound device.h versions!", "Sound Version Warning", 0);   // FUN_00485590; warning only, continues
}
return 0;
```
Imports are resolved **by ordinal, not by name**. Ordinal → golf function-pointer slot:

| ord | export | golf pointer | golf thunk |
|---|---|---|---|
| 1 | `create_sound` | 0x0083af6c | FUN_004840e0 (returns 1 if sound not enabled) |
| 2 | `delete_sound` | 0x0083af70 | FUN_00484110 |
| 3 | `init_sound_timer` | 0x0083af74 | FUN_00484090 (returns 4 if DLL not loaded) |
| 4 | `release_sound` | 0x0083af78 | called directly in FUN_00483f10 |
| 5 | `Dll_Wave_Device::create_device` | 0x0083af7c | golf Wave wrapper slot +0x04 (FUN_004873f0) |
| 6 | `Dll_Wave_Device::delete_device` | 0x0083af80 | Wave wrapper slot +0x08 (FUN_00487430) |
| 7 | `Dll_Midi_Device::create_device` | 0x0083af84 | Midi wrapper +0x04 (FUN_004879b0) |
| 8 | `Dll_Midi_Device::delete_device` | 0x0083af88 | Midi wrapper +0x08 (FUN_004879f0) |
| 9 | `Dll_Wave_In_Device::create_device` | 0x0083af8c | WaveIn wrapper +0x04 (FUN_00487b90) |
| 10 | `Dll_Wave_In_Device::delete_device` | 0x0083af90 | WaveIn wrapper +0x08 (FUN_00487bd0) |
| 11 | `get_sound_version` | 0x0083af94 | FUN_004840c0 |
| 12 | `WaveInDeviceMgr::call_back` | – | **not imported by golf** |

`get_sound_version()` returns **0x05000C01**; the check only looks at byte 1 (0x0C).

### 1.2 Initialisation (FUN_00483e90 @ 0x00483e90, called once from FUN_0045baf0 @ 0x0045bd75)
Call: `sound_init(hwnd = mainWindow->vfunc_0x20(), flags = 0x8a)` (the `PUSH 0x8a` at 0x0045bd6c).
```
sound_init(HWND hwnd, uint flags /*0x8a*/):
    if (load_sound_dll()) return error;                    // 1.1
    init_sound_timer(0, 0);                                // ord 3 (creates 30 ms timer object, not started)
    if (flags & 2)                                         // yes
        MidiWrapper(0x0083ad58).create_and_init(hwnd, (flags & 0x100) ? 2 : 0);   // -> ord 7, then Midi_Device::init(hwnd, 0): no midiOutOpen
    err = WaveWrapper(0x0083ad80).create_and_init(hwnd, flags);   // -> ord 5, then Wave_Device::init(hwnd, 0x8a)
    if (err) { unload dll; return err; }
    if (flags & 8)                                         // yes
        WaveInWrapper(0x0083af98).create_and_init(hwnd, flags);  // -> ord 9, Wave_In_Device::init
    soundEnabled = 1;                                      // DAT_0083afc4
```
`create_and_init` = golf device-wrapper slot +0x10 (FUN_00487c00 / FUN_00487a70): `this->create()` (ordinal create_device into `this+0x14`) then `dev->init(hwnd, flags)` (DLL device slot +0x10); on failure `this->destroy()`.

Wave_Device::init (sound.dll FUN_1000b420) with 0x8a:
* 0x80 → creates the hidden `snd_class`/`snd_window` window + thread (FUN_100286f0) used for internal notifications (message 0x7ee/0x7f0).
* creates the DirectSound wrapper: `DirectSoundEnumerateA` (DSOUND ordinal 2), `CoInitialize`, `CoCreateInstance(CLSID_DirectSound)`, `IDirectSound::Initialize(guid)`, `SetCooperativeLevel(hwnd, DSSCL_PRIORITY)`, primary format 44100/2ch/16-bit.
* creates the 64 KB streaming secondary buffer and the mixer (FUN_10006e00), registers itself with the timer and **starts the 30 ms timer** (`timeBeginPeriod(wPeriodMin)`, `timeSetEvent(30, …, TIME_PERIODIC)`).
* bits 0x02/0x08 are stored at device+0x1c4 and passed to the DS enumerator; exact meaning unknown (only 0x20/0x40 = "use 3D / quad" are tested there, and golf does not set them).

### 1.3 Startup order (all inside FUN_0045baf0)
1. `sound_init(hwnd, 0x8a)` (0x0045bd75)
2. FUN_0043cd70 → FUN_0043cce0: intro Bink movie. Gets the `IDirectSound*` via Wave wrapper slot +0x74 (FUN_00487630 → Wave_Device slot +0x74) at 0x0043cd40 and passes it to `BinkSetSoundSystem(BinkOpenDirectSound, lpDS)` (FUN_00487090).
3. `register_all_sounds()` FUN_00448220 – creates 219 DLL sound objects (no file I/O yet, flag 4 = lazy load).
4. main game loop FUN_0040f5c0 (… plays sounds …); on exit it stops #45 (ambience).
5. `release_all_sounds()` FUN_004490b0.

The 300 wrapper objects are constructed by a static initializer FUN_00448160 (`eh vector ctor(0x0080d840, 0x6c, 300, ctor 0x00484820, dtor 0x004848a0)`). The ctor sets volume 0x7f, pan 0, fade time 1000 ms, channel group 0x10 (= none), float 1.0 at +0x60.

### 1.4 Shutdown
`FUN_004490b0`: for each of the 300 wrappers → golf slot +0x14 (FUN_00484f00): `dll->unload()` (DLL slot +0x14), drop the pointer, unlink. Then `sound_shutdown()` FUN_00483f10:
```
if (!soundEnabled) return 1;
release_sound();                                  // ord 4: stop + destroy timer
for each wrapper still in golf's sound list (head DAT_0083af68):
    if (w->dll) delete_sound(w->dll);             // ord 2
    free(w->filename);
    if (flags & 2 && flags & 0x200) { unlink; delete_sound(w); } else w->unlink();   // golf slot +0x80
MidiWrapper.shutdown()   // FUN_00487ac0: Midi_Device slot +0x14, delete_device (ord 8)
WaveWrapper.shutdown()   // FUN_00487460: Wave_Device slot +0x14, delete_device (ord 6)
WaveInWrapper.shutdown() // FUN_00487c40: Wave_In_Device slot +0x14, delete_device (ord 10)
FreeLibrary; clear pointer table (FUN_00484130)
```

---

## 2. Exported functions

All C exports are `__cdecl`. Exports at 0x1000xxxx short addresses are incremental-link thunks; real bodies given.

| ord | export (real body) | signature | semantics / returns |
|---|---|---|---|
| 1 | `create_sound` 0x1000137a → **0x100093e0** | `SNDERR create_sound(Sound **out, const char *filename, unsigned type)` | Creates (does **not** load) a sound object. `type==0` → chosen by `strstr(filename, ext)`: "wav"→1, "mid"→2, "aif"→3, "rmi"→2, none→0. Types: **1 = Wave sound** (0x1450 bytes, vtable 0x1005c7e8, filename copied into object); 2 = MIDI song (0x948, vt 0x1005b6a8, needs Midi_Device else returns 3); 3 / unknown → returns 0xb; 4 = ? (0xec, vt 0x1005b1c0); 5 = MIDI-based ? (0xa60, vt 0x1005b000, needs Midi_Device); 6 = **Voice Rx** (guess, 0xe8, vt 0x1005c450); 7 = **Voice Tx** (guess, 0x94, vt 0x1005c528); 8 = ? (0x10c, vt 0x1005c3a4). Returns 0 even if `new` fails (`*out = NULL`). **golf always passes type 1.** |
| 2 | `delete_sound` 0x10001a87 → **0x100097d0** | `SNDERR delete_sound(Sound *s)` | NULL → 0. Wave sounds: if the timer/mixer is running the delete is queued to the Wave_Device (command 0x11) so the mixer thread frees it; otherwise voices are stopped and the object is deleted immediately. MIDI types are routed through the Midi_Device. Always 0. |
| 3 | `init_sound_timer` 0x10001c67 → **0x100092e0** | `SNDERR init_sound_timer(unsigned unused, unsigned flags)` | Stores flags. If `!(flags & 1)` creates the global timer object (0x5c bytes, FUN_100089f0): period 30 ms, resolution `timeGetDevCaps.wPeriodMin`, mix format 44100/2/16, 1323 frames/tick. Timer is started later by Wave_Device::init. Returns 0. golf: `(0,0)`. |
| 4 | `release_sound` 0x1000265d → **0x10009390** | `SNDERR release_sound(void)` | Kills the timer event (`timeKillEvent`, `timeEndPeriod`), flushes queued timer items, destroys the timer object. Returns 0. |
| 5 | `Dll_Wave_Device::create_device` **0x10001fcd** | `static SNDERR create_device(Wave_Device **out, unsigned long unused)` | Singleton. If one exists → **0xc**. `new` 0x41e0 bytes, ctor FUN_1000b1d0, vtable **0x1005b304**, global DAT_100b4a20. Returns 0 (also if alloc failed, then `*out=NULL`). |
| 6 | `Dll_Wave_Device::delete_device` 0x100011c2 | `static SNDERR delete_device(void)` | `delete` (vslot 0) the singleton; 0. |
| 7 | `Dll_Midi_Device::create_device` 0x100021cb | `static SNDERR create_device(Midi_Device **out, unsigned long)` | Singleton (0xc if exists), 0x4058 bytes, vtable **0x1005b28c**, DAT_100b4a1c. |
| 8 | `Dll_Midi_Device::delete_device` 0x10001a5f | `static SNDERR delete_device(void)` | as 6. |
| 9 | `Dll_Wave_In_Device::create_device` 0x100020e5 | `static SNDERR create_device(Wave_In_Device **out, unsigned long)` | Singleton (0xc), 0x4c bytes, vtable **0x1005b3d4**, DAT_100b4a24. |
| 10 | `Dll_Wave_In_Device::delete_device` 0x100021fd | `static SNDERR delete_device(void)` | as 6. |
| 11 | `get_sound_version` 0x10002770 → **0x10009370** | `unsigned get_sound_version(void)` | returns **0x05000C01**. |
| 12 | `WaveInDeviceMgr::call_back` 0x1000179e | `static void __stdcall call_back(HWAVEIN, UINT msg, DWORD inst, DWORD p1, DWORD p2)` | waveIn callback: on `WIM_DATA` (0x3c0) posts 0x7f0 to the sound window (or queues it). Not used by golf. |

DllMain (entry 0x100430fe → FUN_10009210) stores the hInstance (DAT_100b5024).

### 2.1 SNDERR values observed
No symbol names survive; meanings inferred from where each value is returned.

| value | meaning (inferred) | where |
|---|---|---|
| 0 | OK | everywhere |
| 1 | not initialised / no device / DLL not loaded | golf wrappers when `soundEnabled==0`; Wave sound play when no Wave_Device |
| 3 | device missing / DirectSound init failed | create_sound type 2/5 without Midi_Device; Wave_Device init/select failures |
| 4 | DLL not loaded (golf-side) | FUN_00484090 |
| 6 | no free voice (all 16 busy) / busy / device select failed | start-voice FUN_10034430, Wave_Device |
| 7 | MIDI device open failed (midiOutOpen) | Midi_Device::init |
| 8 | file not found / cannot load; no filename | WAV loader (mmioOpen fail), slot +0x8c |
| 0xa (10) | invalid parameter (NULL / 0) | many |
| 0xb (11) | unsupported / not implemented | unknown type, stub slots |
| 0xc (12) | already exists / already created | create_device, golf FUN_00484c20 |
| 0xd (13) | timer already running | FUN_10008ce0 |
| 0xe (14) | sound is streaming and already playing (cannot overlap) | Wave sound slot +0x1c |
| 0xf (15) | sound busy/loaded, cannot reload (golf side) | golf FUN_004842f0 |
| 0x11 (17) | DirectSound buffer creation failed (dedicated-buffer mode) | FUN_10032130 |
| 0x12 (18) | not a RIFF/WAVE file or missing `fmt `/`data` | WAV loader |
| 0x13 (19) | no Wave_Device / no child object | several |
| 0x14 (20) | sound not loaded / no underlying object | stop, slot +0x20 etc. |
| 0x15 (21) | not playing | fade functions |
| 0x19 (25) | (golf, graphics init — unrelated) | |
| 0x1d, 0x25 | DS buffer pool exhausted (retried) | FUN_10012260 |
| 0x1e (30) | DS streaming buffer creation failed | Wave_Device::init |
| 0x20 (32) | CoInitialize failed | FUN_10011c60 |
| 0x21 (33) | operation needs in-memory sound | slot +0x94 |
| 0x22 (34) | queue full (4096 commands) / too many samples per mix call (>2000) | FUN_1000c2f0, mixer |
| 0x23 (35) | short read from WAV | dedicated-buffer load |
| 0x28 (40) | sound being deleted | slot +0x1c |

---

## 3. Classes used through vtables

### 3.1 How golf talks to them
* `Wave sound` (DLL vt **0x1005c7e8**, 61 slots) ← golf proxy `GolfWaveSound` (vt **0x004bacf8**, 61 slots, object size 0x6c, DLL object at **+0x40**). The two vtables are slot-compatible; golf's slot N implementation calls DLL slot N (exceptions noted).
* golf proxy base `GolfSound` (vt 0x004bac68, 33 slots) = header copy of the DLL's base `Sound` class (same flags/link layout: +0x44 flags, +0x48/+0x4c list links, +0x54 type). The DLL's own Wave sound also inherits this base (its slots +0x88/+0x8c/+0x90 contain the identical "proxy" code, calling `create_sound` internally).
* golf `GolfVoice` (vt 0x004badf8, type 6, name "Voice Rx") – constructor FUN_004852e0 exists, never instantiated by game code that I could find **(guess: dead code)**.
* Device proxies: base 0x004baeac (21 pure-virtual slots), **Wave** 0x004baf00 (43 slots ↔ Wave_Device 43 slots, global object 0x0083ad80), **Midi** 0x004bafb0 (25 slots, global 0x0083ad58), **WaveIn** 0x004bb014 (27 slots, global 0x0083af98). DLL device pointer at **+0x14**.

"Golf calls" below = how many times golf *game code* reaches that DLL slot (call sites, through the proxy). Determined from: all xrefs to the sound array (0x0080d840..0x008156d0) and device globals (0x0083ad58..0x0083afd0), all direct callers of every proxy-slot function (`golf_calls*.txt`). The array is only touched from 0x00448220, 0x004481b0, 0x00448200, 0x004104f7 and 0x004490b0.

Golf proxy object layout (0x6c): +0x00 vtbl, +0x04 volume (0–127, default 0x7f), +0x08 pan (−64..63), +0x30 loop flag, +0x34 start delay, +0x38 fade time (1000), +0x3c channel group (0x10 = none; groups 0–15 live in the Wave proxy at 0x0083ada8, unused), **+0x40 DLL Sound\***, +0x44 class flags, +0x48/+0x4c prev/next in golf's list (head DAT_0083af68, tail DAT_0083af64), +0x50 filename copy, +0x54 type, +0x58 proxy flags, +0x5c pitch (cents), +0x60 float 1.0, +0x64 length ms, +0x68 release time stamp.

### 3.2 Wave sound — DLL vtable 0x1005c7e8 (object 0x1450 bytes; inner "WaveData/voice" object at +0x70)

Volume values are 0..127 (`(v+1)*512` → linear gain /65536); pan −64..+63 (linear: louder side keeps `vol`, other side `vol - vol*|pan|/63`); pitch in cents ±1200.

| slot | DLL fn | proposed name / C signature (this = Sound*) | semantics | golf calls |
|---|---|---|---|---|
| +0x00 | 10039760 | `void* dtor(int deleting)` | scalar deleting dtor | via delete_sound |
| +0x04 | 1003a6b0 | `int set_fade_out_time(unsigned ms)` | per-tick fade decrement = vol/(ms/30) (per voice if in-memory) | 3 (fade_out_sound) + ctor default 1000 is only stored golf-side |
| +0x08 | 10005670 | `int set_field38(int v)` | stores +0x38; 10 if 0 **(guess: default fade time)** | 0 |
| +0x0c | 1002b110 | `int unsupported0c()` | returns 0xb | 0 |
| +0x10 | 100399a0 | `int load(const char *filename)` | opens/parses WAV (FUN_100326a0) — see §4; multi-instance clone support (+0x1438) | 0 direct (called internally on first play) |
| +0x14 | 10039bd0 | `int unload()` | if loaded, queue cmd 3 (stop+close) on Wave_Device | 300 objects at shutdown (FUN_004490b0 → FUN_00484f00) — 1 site |
| +0x18 | 1002b1b0 | `int child_18(int)` | forwards to child at +0x40 (0x14 if none) | 0 |
| +0x1c | 10039ce0 | **`int play()`** | lazy-loads file if needed; for in-memory sounds starts a new voice (max 16); queues cmd 1 (start) to the Wave_Device; streaming sound already playing → 0xe | **51 sites** (play_sound) |
| +0x20 | 1003a040 | **`int stop()`** | queue cmd 2 (stop all voices, post "finished"); 0x14 if not loaded | **1 site** (0x004104f7, ambience #45) |
| +0x24 | 1002b3a0 | `void fade_out(unsigned ms)` | `set_fade_out_time(ms); start_fade();` | **3 sites** (fade_out_sound, ms=1000) — golf calls its own proxy +0x24 which calls DLL +0x04 and +0x28 |
| +0x28 | 10039c40 | `int start_fade()` | marks voices "fading" (FUN_10034ac0); stops at 0 vol; 0x15 if not playing | 3 (as above) |
| +0x2c | 1002b430 | `void fade_in(unsigned ms)` **(guess)** | `set_fade_in_time(ms); start_fade();` | 0 |
| +0x30 | 10039c80 | `int fade_in_from_silence()` **(guess)** | FUN_10034b90: current vol←0, target←vol | 0 |
| +0x34 | 1003a140 | `void fade_to(int fromVol, int toVol, unsigned ms)` | −1 = current volume | 0 |
| +0x38 | 1003a080 | `int set_pause()` **(guess; sets per-voice bit 1 / flag 0x80)** | | 0 |
| +0x3c | 1003a770 | `HWND get_hwnd()` | DS cooperative-level window | 0 |
| +0x40 | 1003a0d0 | **`void set_volume(int 0..127)`** | linear | **51** |
| +0x44 | 1003a1e0 | **`void set_pan(int -64..63)`** | | **51** |
| +0x48 | 1003a690 | `void set_loop(int on)` | loop whole sound | 0 by game (only via flag 2 path, unused) |
| +0x4c | 1003a1c0 | **`void set_start_delay(unsigned ms)`** | voice starts after `ms` (+0x29c) | **51** (always 0 except none; arg5 of play_sound) |
| +0x50 | 1002ad10 | `void set_type(int t)` | base: type + class-flag bits (1→0x4,2→0x8,4→0x10,5→0x28,6→0x100,7→0x80) | ctor only |
| +0x54 | 1003a6e0 | `int set_fade_in_time(unsigned ms)` **(guess)** | like +0x04 with negative step | 0 |
| +0x58 | 1003a750 | `int is_looping()` | | internal (golf +0x8c) |
| +0x5c | 1003a790 | `int is_playing()` | playing && !stopping | 0 by game |
| +0x60 | 1003a7e0 | `int is_loaded()` | | internal |
| +0x64 | 100056c0 | `Sound* next()` | global list | 0 |
| +0x68 | 100056e0 | `Sound* prev()` | | 0 |
| +0x6c | 1003a370 | **`int set_flags(unsigned f)`** | see flag table below | **219** (register) |
| +0x70 | 1003a560 | `unsigned get_flags()` | inverse of +0x6c (+8 if delayed) | 219 (register, via golf proxy +0x70) |
| +0x74 | 1003a710 | `unsigned get_elapsed()` **(guess: ms since start / delay counter)** | | 0 |
| +0x78 | 10005700 | `int get_type()` | | internal |
| +0x7c | 1002b010 | `void link()` | add to DLL global sound list | internal |
| +0x80 | 1003a0b0 | `void unlink()` | | internal |
| +0x84 | 10039980 | `int reload()` | clear loaded flag, reload from stored filename | 0 |
| +0x88 | 1002b8d0 | `void setup(const char *name, unsigned flags)` | base-class proxy logic (store name; if flags&4 create child via create_sound) | golf runs its own copy (0x00484a40) 219× |
| +0x8c | 1002bd80 | `int create_and_load()` | base proxy logic | golf copy; not reached (flags always have 4) |
| +0x90 | 1002be90 | `int setup_and_load(const char *name, unsigned flags)` | `setup(); if(!(flags&4)) create_and_load();` | golf copy 0x00484e30 called **219×** |
| +0x94 | 10039f30 | `int play_voice(int *voiceOut)` | play, returns voice index (0x21 if streaming) | 0 |
| +0x98 | 10039e70 | `int restart_voice(int voice)` | | 0 |
| +0x9c | 1003a210 | **`int set_pitch(int cents)`** | ±1200 (mod 1200 if outside); semitone table 2^(n/12) with linear cent interpolation; DS-buffer mode uses 11025..44100 table (assumes 22050 base) | **51** |
| +0xa0..+0xb4 | 1002c2b0.. | 6 × `int unsupported()` | 0xb **(guess: 3D placeholders)** | 0 |
| +0xb8 | 1003a590 | `int get_voice_info(int voice)` | voice +0x2f8 if active | 0 |
| +0xbc | 1003a730 | `int get_current_cue()` | last `cue ` point passed, −1 | 0 |
| +0xc0 | 1002c1c0 | `int child_c0()` | forward | 0 |
| +0xc4 | 1002c1f0 | `int child_text(char *buf, int len, int flag)` **(guess)** | forward; empty string + 1 if none | 0 |
| +0xc8 | 1003a670 | `unsigned get_length_ms()` | frames*1000/rate | internal (golf +0x8c path) |
| +0xcc | 1003a7c0 | `int uses_ds_buffer()` | bit 0x10000000 (dedicated DirectSound buffer) | internal |
| +0xd0/+0xd4 | 1002b870/1002b8a0 | child forwards | | 0 |
| +0xd8 | 1003a5b0 | `int set_3d_position(x,y,z)` **(guess)** | only with DS3D buffer, else 0xb | 0 |
| +0xdc/+0xe0/+0xe4 | 1003a5e0/630/650 | 3D params (min/max distance, mode…) **(guess)** | 0xb unless 3D | 0 |
| +0xe8 | 1003a800 | `int is_3d()` | | 0 |
| +0xec | 1003a600 | `int set_3d_x(int)` **(guess)** | | 0 |
| +0xf0 | 1003a440 | `int post_command(const char *s, int p)` | queues command 0xd with a string to the Wave_Device | 0 |

Flags for `set_flags` / `setup_and_load` (same bit meanings in golf's proxy and the DLL):

| bit | meaning | used by golf |
|---|---|---|
| 0x001 | in-memory requested (base +0x58 bit0) **(guess)** | no |
| 0x002 | loop (calls `set_loop(1)`) | no |
| 0x004 | **lazy**: create object now, open/parse file on first `play()`; `setup_and_load` returns without loading | **all 219** |
| 0x010 | **dedicated DirectSound buffer** (whole file into its own static DS buffer; DS does vol/pan/freq) | 18 music/jingle sounds (flags 0x14) |
| 0x020 | ? (+0xc8 bit31) | no |
| 0x040 | DS3D buffer (needs 3D-capable device) | no |
| 0x080 | one-shot: golf proxy releases DLL object after play, uses length_ms + timeGetTime to answer is_playing | no |
| 0x100, 0x200, 0x400 | misc (+0xcc bits 0x40/0x80/0x200) | no |

### 3.3 Wave_Device — DLL vtable 0x1005b304 (singleton, 0x41e0 bytes; golf proxy 0x004baf00 at 0x0083ad80)

| slot | fn | name / signature | semantics | golf calls |
|---|---|---|---|---|
| +0x00 | 1000b2b0 | dtor | | delete_device |
| +0x04/+0x08 | 10029290/100292b0 | `int nop()` | return 0 (golf proxy uses its own +0x04/+0x08 = create/delete via ordinals) | – |
| +0x0c | 1000e7f0 | `void clear_timer_ref()` | | 0 |
| +0x10 | 1000b420 | **`int init(HWND, unsigned flags)`** | DirectSound + mixer + timer (see §1.2) | **1** (0x00483ed2) |
| +0x14 | 1000b960 | **`int shutdown()`** | stop timer, release DS buffers, mixer, window thread | **1** (0x00487460) |
| +0x18 | 1000e200 | `int num_devices()` | DS enumeration count | 0 |
| +0x1c | 1000c020 | `int select_device(unsigned idx)` | recreate DS on device idx | 0 (golf proxy +0x1c also reloads all sounds) |
| +0x20 | 1000e260 | `int device_name(unsigned idx, char *buf, size_t n)` | | 0 |
| +0x24 | 1000e2a0 | `void set_master_volume(unsigned)` **(guess)** | FUN_10013560 on DS object | 0 |
| +0x28..+0x34 | | stubs (0 / void) | | 0 |
| +0x38 | 1000c230 | `void tick()` | timer callback: process command queue (FUN_1000cd80), advance delays, mix into DS buffer (FUN_10007010) | internal |
| +0x3c/+0x40 | 1000e810/1000e830 | `set/get_tick_param(int)` **(guess: ms per tick)** | | 0 |
| +0x44/+0x48 | 1000e1d0/1000e7b0 | `add_time(int ms)` / `get_time()` | | 0 |
| +0x4c | 1000bef0 | `void pause_output()` | stop DS buffers + timer | 0 (called by +0x68) |
| +0x50 | 1000bf90 | `void resume_output()` | | 0 |
| +0x54/+0x58 | 1000b110/1000b170 | `int start_capture(LPSTR file)` / `stop_capture()` **(guess: record mix to WAV)** | | 0 |
| +0x5c | 1000e230 | `int get_ds_caps_x()` | | 0 |
| +0x60 | 1000e790 | `void reset_time()` | | 0 |
| +0x64 | 1000baa0 | `int reacquire()` | recreate DS after suspend (also resumes MIDI) | 0 |
| +0x68 | 1000be00 | `int suspend()` | release DS entirely (e.g. app deactivated) | 0 |
| +0x6c | 1000e7d0 | `int is_suspended()` | | 0 |
| +0x70 | 1000b140 | `int ds_op(int)` **(guess: set primary format)** | | 0 |
| +0x74 | 1000e2d0 | **`IDirectSound* get_directsound()`** | | **1** (0x0043cd40, for Bink) |
| +0x78 | 1000e850 | `int flag_8()` | | 0 |
| +0x80 | 1000e310 | 3D-related query | | 0 |
| +0x88 | 1000e5b0 | `int has_3d()` | | 0 |
| +0x8c | 1000e490 | `int set_listener_position(x,y,z)` **(guess)** | | 0 |
| others (+0x7c,+0x84,+0x90..+0xa8) | | return 0xb | | 0 |

Internals worth knowing: command ring buffer of 4096 entries at +0x1d8 (FUN_1000c2f0(dev, sound, cmd)); commands 1 start, 2 stop (+notify), 3 unload, 4 midi, 5 finished-notify, 6 fade/stop-end, 0xd named command, 0x11 deferred delete. Active-sound list at +0x1c8 (no global cap; each sound ≤16 voices).

### 3.4 Midi_Device — vtable 0x1005b28c (golf proxy 0x004bafb0 at 0x0083ad58)
Golf reaches only: **+0x10 `init(HWND, flags)`** (1 site, flags 0 → `midiOutOpen` skipped; just registers with the timer) and **+0x14 `shutdown()`** (1 site). Slots +0x58 resume / +0x5c suspend / +0x60 is_suspended are called by Wave_Device. Everything else (sequencer, channel volumes, etc.) unused. No MIDI files exist in the game data.

### 3.5 Wave_In_Device — vtable 0x1005b3d4 (golf proxy 0x004bb014 at 0x0083af98)
Golf reaches **+0x10 `init(HWND, flags)`** (1 site; allocates the voice codec/buffer manager FUN_10038890(…,10,0x168) once, does not start recording **(guess)**) and **+0x14 `shutdown()`** (1 site). Used for multiplayer voice ("Voice Rx"/"Voice Tx" sound types 6/7). Nothing else.

---

## 4. Audio features actually used

* **Formats**: RIFF/WAVE parsed with `mmioOpen/mmioDescend` (`RIFF`/`WAVE`, `fmt ` (16 bytes read → plain PCM only), optional `smpl` (loop points), optional `cue `, `data`). Mixer supports 8-bit, 16-bit, mono, stereo. Filenames containing "mp3"/"MP3" take a separate path (not used).
  Survey of the 244 WAVs in `simgolf-wasm/assets`: all PCM 16-bit — 209 mono 22050 Hz, 29 stereo 22050 Hz, 5 mono 44100 Hz. 77 files have >128 KB of data (biggest: `Music/Credit.wav` 14.5 MB).
* **Looping**: there is no game-side loop call. Looping comes from the WAV **`smpl` chunk**: `GolfAmbience122.wav` (#45, the course ambience) and `GolfAmbienceWtr22.wav` each have one loop over the whole file → loop forever. (Music files `Steel Drum*` have `smpl` with 0 loops → play once.) `newhart-theme.wav` has 2 `cue ` points (unused by game).
* **Static vs streaming**: data ≤ 128 KB → whole data in memory, up to 16 overlapping voices. Larger → streamed from disk through a 128 KB mmio buffer, single voice (a second `play()` while playing returns 0xe and is ignored). Flag 0x10 sounds (music) → whole file into a dedicated DS buffer at first play.
* **Lazy loading**: all 219 objects are created at startup without I/O; a file is opened on its first `play()` and **stays open/cached** until shutdown.
* **Volume/pan/pitch/delay**: every `play_sound(idx, vol, pan, pitch, delay)` sets pitch, volume, pan, delay then plays. Observed arguments: volume almost always 100 (one call 0x46=70), pan always 0, pitch 0 except club-drop #144 with `-random(300)` cents, delay always 0. Volume scale is linear: gain = (v+1)/128.
* **Fades**: `fade_out_sound(idx)` = linear fade to silence over 1000 ms, then stop. Used for ambience↔music cross-fades (FUN_004385d0: fade #45 and play #128 or #125 on entering, fade the music and replay #45 on leaving; FUN_0044cff0: fade #127).
* **3D positioning**: implemented in the DLL (DS3D), **not used** by SimGolf.
* **MIDI**: **not used** (see §3.4).
* **Mixing limits**: 16 voices per sound; active sounds unbounded; command queue 4096; mixer ≤ 2000 frames per call; output 44.1 kHz stereo 16-bit, 30 ms ticks, 64 KB DS ring buffer.
* **Timer** (`init_sound_timer`): one WINMM periodic multimedia timer at 30 ms drives command processing, fades/delays and mixing for all devices on the timer thread (critical section DAT_100b4990). golf passes flags 0 (= DLL owns the timer).
* **Bink**: the intro/cut-scene movies get the DLL's `IDirectSound*` (Wave_Device +0x74) and play their own audio through it.
* **Duplicate registrations**: 9 indices are registered twice (e.g. #24 `effects\boing.wav` then `emotion\FemaleSkTTFailure.wav`). Because the DLL object is created on the first registration and the second only updates the golf-side filename/flags, the DLL plays the **first** file (here boing.wav; #24 is the most-played sound, 13 sites). High confidence from the code, not verified at runtime.
* **Indices**: see `sound_index.txt` (index 0..269 used, 219 registrations; array has 300 slots). Paths are DOS-style, mixed case (`sounds\Golf sfx\Iron.wav`); the asset tree in `simgolf-wasm/assets` differs in case and uses `_` for some spaces (`Music/Links_Music/…`) → needs a case-insensitive, normalising path lookup.

---

## 5. Recommendations for the Web Audio / SDL_mixer shim

Do **not** emulate sound.dll's object model. Re-implement golf's five entry points directly (they are the only places game code touches sound):

```c
void snd_init(void);                                   // replaces FUN_00483e90 (hwnd/flags ignored)
void snd_register(int idx, const char *dosPath, unsigned flags); // FUN_00484e30 on &g_sounds[idx]; keep FIRST registration's file
void snd_play(int idx, int vol0_127, int pan_m64_63, int pitchCents, int delayMs); // FUN_004481b0 (ignore idx == -1)
void snd_fade_out(int idx, int ms /*1000*/);           // FUN_00448200
void snd_stop(int idx);                                // proxy slot +0x20 (0x004104f7)
void snd_shutdown(void);                               // FUN_004490b0 + FUN_00483f10
```
and stub the Bink `IDirectSound` hand-off (0x0043cce0) – the video player needs its own audio path.

Web Audio mapping (recommended over SDL_mixer because of pitch/delay/fade):
* One `AudioContext`; decode each WAV on first play (`decodeAudioData` handles PCM WAV; or parse yourself to also read `smpl` loops) and cache the `AudioBuffer` (lazy like the original). Pre-fetch music files in the background if desired.
* Per play: `AudioBufferSourceNode` → `GainNode` (gain = (vol+1)/128) → `StereoPannerNode` (pan/64, or two gains to match the linear law: louder side 1, other side 1-|pan|/63) → master `GainNode`. `playbackRate = 2^(cents/1200)`. `start(ctx.currentTime + delayMs/1000)`.
* Keep ≤16 live sources per index (drop the new one when full, like error 6). For "streamed" sounds (data > 128 KB, i.e. ambience, music, long speech) allow only one instance: ignore `play` while it is playing (error 0xe behaviour) — this matters for #45 ambience, which is re-triggered after cut-scenes.
* `smpl` loop → `source.loop = true; loopStart/loopEnd` from the chunk (only #45/ambience-wtr actually loop).
* Fade-out: `gain.setValueAtTime(cur, t); linearRampToValueAtTime(0, t+ms/1000)` then `stop(t+ms/1000)`.
* Stop: `stop()` all sources of that index.
* Resume the `AudioContext` on the first user gesture (browser autoplay policy).
* With Emscripten: implement these as `EM_JS` functions or a small JS library; WAVs can stay in the preloaded FS (`FS.readFile` → ArrayBuffer) or be fetched from a URL map built from `sound_index.txt` with normalised paths.

SDL_mixer alternative: `Mix_LoadWAV` per index, `Mix_AllocateChannels(32+)`, `Mix_Volume` (vol*128/128) and `Mix_SetPanning`; music cross-fades via `Mix_FadeOutChannel(ch, 1000)`; looping ambience via `Mix_PlayChannel(ch, chunk, -1)` (hard-code index 45 or read `smpl`). Pitch (#144 only) and start delay have no direct equivalent – drop pitch or pre-render; delay is always 0 in practice.

### Open questions
1. Exact meaning of Wave_Device init flags 0x02/0x08 (stored, not decoded) and of slots +0x38, +0x54, +0x74 of the Wave sound – irrelevant to the port unless found used.
2. Whether duplicate-index registrations really play the first file (static analysis says yes; worth one runtime check, e.g. #24 boing vs FemaleSkTTFailure).
3. Whether any game code reaches the sound proxies via *virtual* calls on pointers I did not trace (all direct array/global xrefs were checked; a pointer to a `&g_sounds[i]` stored elsewhere would be missed — none were seen).
4. DS-buffer mode (flag 0x10) volume conversion to DirectSound dB (FUN_1000f1c0) not decoded; the port can use the same linear gain as for mixed sounds (perceived loudness of music relative to SFX may differ slightly).
5. Sound types 4, 5 and 8 of `create_sound` (vtables 0x1005b1c0, 0x1005b000, 0x1005c3a4) not identified; not used by golf.
6. Options screen: no master/music volume or sound on/off path into sound.dll was found; check whether the game gates `play_sound` on an options flag elsewhere (not in the sound layer).
