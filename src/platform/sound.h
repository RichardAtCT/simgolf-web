// Sound: the audio interface for the browser port. Recovered from sound.dll and
// the v1.02 NoCD golf.exe; see docs/sound.md.
//
// sound.dll is a full software mixer over DirectSound (plus unused MIDI and
// wave-in devices), but SimGolf's game code only touches it through five
// index-based helpers in golf.exe. The port replaces those helpers rather than
// emulating the DLL's object model:
//
//   golf.exe                                   port
//   FUN_00483e90 init (flags 0x8a)             snd_init
//   FUN_00448220 register 219 files            snd_register (once per index)
//   FUN_004481b0 play, 51 call sites           snd_play
//   FUN_00448200 fade out over 1000 ms         snd_fade_out
//   proxy slot +0x20 at 0x004104f7             snd_stop (ambience loop #45)
//   FUN_004490b0 + FUN_00483f10 shutdown       snd_shutdown
//
// Behaviour to keep (all from the original):
// - gain = (volume + 1) / 128, volume 0..127; pan -64..63 (always 0 in
//   practice); pitch in cents (only the club drop, #144, uses it).
// - Files load lazily on first play and stay cached.
// - Up to 16 overlapping instances per index. Files whose data chunk is over
//   128 KB were streamed and allow one instance: a play while it's playing is
//   ignored.
// - Looping comes only from a WAV `smpl` chunk (the two course ambience files).
// - When an index is registered twice (9 are), the first file wins.

#ifndef SIMGOLF_PLATFORM_SOUND_H
#define SIMGOLF_PLATFORM_SOUND_H

#ifdef __cplusplus
extern "C" {
#endif

enum { SND_MAX_SOUNDS = 300 };          // golf's array at 0x0080d840, stride 0x6c

// flags as golf passes them: 0x04 = lazy load, 0x10 = own buffer (music/jingles).
#define SND_FLAG_LAZY      0x04u
#define SND_FLAG_DEDICATED 0x10u

void snd_init(void);
void snd_register(int index, const char *dosPath, unsigned flags); // e.g. "sounds\\Golf sfx\\Iron.wav"
void snd_play(int index, int volume, int pan, int pitchCents, int delayMs); // index -1 is ignored
void snd_fade_out(int index, int ms);   // golf always uses 1000
void snd_stop(int index);
void snd_shutdown(void);

// sound.dll's real exports, for reference (golf binds them by ordinal 1-11):
//   1 SNDERR create_sound(Sound **out, const char *file, unsigned type)  type 1 = WAV
//   2 SNDERR delete_sound(Sound *)
//   3 SNDERR init_sound_timer(unsigned, unsigned flags)   30 ms mixer tick
//   4 SNDERR release_sound(void)
//   5/6 Dll_Wave_Device::create_device / delete_device      44.1 kHz stereo 16-bit
//   7/8 Dll_Midi_Device::create_device / delete_device      created, never opened
//   9/10 Dll_Wave_In_Device::create_device / delete_device  unused
//   11 unsigned get_sound_version(void)                     0x05000C01; golf checks (v & 0xff00) == 0x0c00

#ifdef __cplusplus
}
#endif

#endif  // SIMGOLF_PLATFORM_SOUND_H
