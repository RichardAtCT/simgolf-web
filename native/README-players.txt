SimGolf for desktop
===================

This app runs Sid Meier's SimGolf (2002) natively.

  WINDOWS VERSION: UNTESTED. The Windows build compiles, but nobody has run
  it on Windows yet. It may not start or may crash. If you try it, please
  report what happens: https://github.com/RichardAtCT/simgolf-web/issues
  The macOS version has been tested on Apple Silicon and Intel (Rosetta).
 It contains no game
files: on first launch it asks for your own copy, the same two things the
browser version needs:

  * the SimGolf CD image (.iso, .bin or .img), and
  * the v1.02 no-CD golf.exe,

or the .zip or .7z they came in. Drop them on the setup window or click
"Choose files". They're unpacked into your app data folder and never leave
your computer:

  macOS:   ~/Library/Application Support/SimGolf/
  Windows: %APPDATA%\SimGolf\

Your saves are in the game/Saved Games folder there; Option+S (Alt+S on
Windows) in the game opens it. To bring a save over from the browser version, download it from
the browser's Saves panel (Option+S or Alt+S there) and put the .sve file in that
folder; to go the other way, Import it in the browser's Saves panel. Saves
from the original 2002 game work too. Updating the app keeps your saves.

Opening the app the first time
------------------------------
The app isn't signed by Apple or Microsoft, so the system warns about it.

  macOS: open the .dmg and drag SimGolf to Applications. The first time,
  Control-click (or right-click) SimGolf and choose Open. On macOS 15 and
  later, if that only offers "Move to Bin", open System Settings > Privacy &
  Security and click "Open Anyway" next to SimGolf, then open it again.

  Windows (untested, see above): unzip the folder anywhere and run
  SimGolf.exe. If SmartScreen
  says "Windows protected your PC", click "More info", then "Run anyway".

Keys
----
  Alt+Enter or F11 (Ctrl+Cmd+F on a Mac)   full screen on/off
  Ctrl+F9    high-resolution course terrain on/off
  Ctrl+F10   scaling: xBR (default), nearest, smooth
  Ctrl+F11   all high-resolution drawing on/off
  Option+S (Alt+S on Windows)   open your saves folder
  A key press or click skips the intro video.
  Everything else is the game's own keys.

The game draws at 800x600; the app redraws its text and the course terrain
at your screen's resolution (it needs OpenGL 3.3; without it the picture is
simply scaled up).

Mods
----
Unzip a mod into the mods folder in the app data folder above, one folder
per mod. Theme packs then show up under Select A Theme on the main menu.

Settings (edge scrolling, scaling, high-resolution drawing) are in
settings.ini in the app data folder. For widescreen, set screen_width=1066
there (800 is the original 4:3 view).

SimGolf is (c) Firaxis Games / Electronic Arts. This port is an unofficial
fan project: https://github.com/RichardAtCT/simgolf-web
