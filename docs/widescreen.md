# Widescreen

The game draws an 800x600 frame. In widescreen the frame is wider and the course
view fills it; everything laid out for 4:3 stays 4:3, centred. It is off by default.

- **Browser:** the Widescreen checkbox under Settings in the Saves panel (Ctrl+F8),
  stored in localStorage `simgolf-wide` and read at page load, or the URL options below,
  which win over the setting.
- **Desktop:** `screen_width` in settings.ini (1066 for 16:9).

| URL | Frame |
|---|---|
| `?wide` or `?wide=16:9` | 1066x600 |
| `?wide=16:10` | 960x600 |
| `?wide=auto` | the window's shape at load |
| `?wide=<width>` | that width (even, 800..1280) |

The height stays 600, so the 2D art keeps its size and the course view shows more
course to the sides. Off by default; without `?wide` nothing changes.

## How it works

- **Width.** `port_screen_w()` (`src/port/win32.c`) reads the URL once. `WIN_SCREEN_W`
  is that value. A `golf.json` patch makes WinMain store it in golf's screen-width
  global (`0x822c8c`) instead of 800. Firaxis half-supported 1024x768 and 1280x1024,
  so the course view already follows that global: sprite placement and culling
  (`FUN_0042fb90`), picking (`FUN_00430020`), the terrain size (`FUN_00449790`).
- **Other `golf.json` patches** (all no-ops at 800 wide):
  - WinMain sizes the window from the height global, not `width * 3/4`.
  - `FUN_00404970`, golf's 320x240 layout-unit scaler, scales as at 800.
  - The right edge-scroll threshold follows the width (`FUN_0040f5c0`).
  - The scroll step table in `FUN_0044a6e0` is chosen by height 600, not exactly 800x600.
- **Terrain** (`terrain_dll.cpp`): the 800x600 camera pitch and projection extents,
  widened in proportion.
- **4:3 screens** (`src/port/gdi.c`, "widescreen"): menus and full-screen reports
  start by stretching an 800x600 background over a full-screen surface. That switches
  every W x 600 jgl surface (and its DIB) to a centred 800-wide view ("narrow"), so
  all later drawing, and the mouse (`gdi_wide_dx()`), is in 800x600 coordinates,
  with black bars beside it. A copy to or from golf's terrain cache (`*(0x4c1574)`)
  switches back to full width. golf's top-level windows cache a visible rect
  (`+0x1dc`) that is updated on each switch too.
- **Course HUD** (`golf.json`, `FUN_0040f5c0`): the money, golfer and rating counters are
  drawn at `width - 800 + x`, so they stay in the top-right corner, and the Paused/Fast
  caption is centred on the frame.
- **Dialogs**: `port_wide_center()` (`gdi.c`) is how far a dialog centred on 400 moves right;
  it is 0 on the 4:3 screens. The list dialog `FUN_0046d6e0` uses it when its x is 400, so
  menus anchored to the left-hand buttons stay put. The text prompt `FUN_0045b2c0` and the
  Save Game panels (`FUN_00405b10`, `FUN_0046f550`) always use it.
- **Page**: the canvas takes its aspect ratio from `--ar`, set by `gdi_init`; the HD
  presentation follows the SDL canvas size.

## Known gaps

- The bottom toolbar keeps its 4:3 position on the left.
- Dialogs not listed above that place themselves with fixed coordinates stay where
  they are; only Preferences, Rename Course and Save Game were checked by eye.
- Untested: the 2D zoom mode (`DAT_005a9cc0`), whose code still centres on 400,300.
