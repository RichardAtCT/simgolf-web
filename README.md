# SimGolf in the browser

A fan port of **Sid Meier's SimGolf** (Firaxis / EA, 2002) to WebAssembly, so
it runs in a web browser on Mac, Linux and Windows with nothing to install.

**Play: https://richardatct.github.io/simgolf-web/**

This repository and the page contain no game files. You bring your own copy of
the game, and it never leaves your computer.

## How to play

1. Get the game files. You need two things:
   - the **game disc**: the `.7z` from the
     [Internet Archive](https://archive.org/details/sid-meiers-sim-golf-1641547)
     (about 250 MB; an `.iso` of the disc from elsewhere works too), and
   - the **v1.02 no-CD `golf.exe`**, usually a zip called
     `SIMGOLF.102.ENG.MYTH.NOCD.ZIP`, from
     [Abandonware DOS](https://www.abandonwaredos.com/abandonware-game.php?abandonware=Sid+Meier%27s+SimGolf&gid=2317).
2. Open the page in **Chrome or Edge, version 137 or newer**, on a desktop or
   laptop. (Firefox and Safari can't run it yet.)
3. Click **Choose your game files** and pick both files as they downloaded;
   there's no need to unzip anything. The page unpacks the game in about a
   minute and starts it. Next time it goes straight to the game.

## Good to know

- Saves stay in your browser. Press **Ctrl+F8** in the game to download a
  save or import one.
- To start over with different files, press **Ctrl+F8** and use the button at
  the bottom of the panel.
- Clearing your browser's site data for the page removes the unpacked game
  and your saves (download the saves first).

## How it works

The game isn't emulated. Its code was decompiled with Ghidra and translated
mechanically into C, which Emscripten compiles to WebAssembly. Windows,
graphics, the 3D terrain renderer and sound are replaced with browser
implementations. [docs/porting.md](docs/porting.md) explains the approach and
[docs/building.md](docs/building.md) how to build it yourself.

## Legal

This is an unofficial fan project, not affiliated with or endorsed by
Firaxis Games, Electronic Arts or Take-Two Interactive. SimGolf and its
assets are their property; this project distributes none of the game's files.
The port's own code is MIT licensed ([LICENSE](LICENSE)); third-party pieces
keep their licences (`third_party/`: unshield and webm-muxer are MIT, 7-Zip and FFmpeg are LGPL).
