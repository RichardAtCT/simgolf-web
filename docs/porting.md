# Porting approach: mechanical translation of the decompile

Started 2026-10-07. The game code is not rewritten by hand. Instead, Ghidra's
decompiler output for every function is translated mechanically into C that
Emscripten compiles, and hand fixes are layered on top. This gets the whole
game building at once, then each subsystem is debugged and cleaned up in
place.

## Pieces

| Piece | Where | What it does |
|---|---|---|
| Ghidra export | `tools/ghidra/ExportPort.java`, `DumpExternals.java` | Per function: decompiled C, signature and the global symbols the decompiler used, with its inferred types. Also `types.h` (all program types) and `image.bin` (the loaded image). Can rebase a DLL first. |
| Translator | `tools/translate/translate.py`, `<module>.json` | Turns the export into `build/gen/<module>/*.c` (see below). |
| Runtime | `src/port/runtime.c`, `ghidra_prelude.h` | Loads images at their original addresses, maps code addresses to callable wrappers (icall), Ghidra helper macros. |
| CRT | `src/port/crt.c`, `native.h` | Native replacements for the MSVC CRT functions the game calls. |
| Files | `src/port/fs.c` | Case-insensitive DOS paths over `/game`, fetched from the server on first open. Files the game writes (saves, autosaves, profile) are mirrored to IndexedDB (`/persist`, IDBFS) and restored over `/game` at start. |
| Win32 | `src/port/win32.c` | The Win32 API subset golf.exe and jgld.dll call. |
| Entry | `src/port/port_main.c` | Replaces the CRT startup: image, static initializers, WinMain. |

## Key decisions

- **Original addresses.** wasm32 is 32-bit and little-endian like x86, so each
  module's image (its `.data`, `.rdata`, even `.text` bytes) is copied into
  linear memory at its original virtual address. Globals stay at their
  addresses, so `DAT_004e6d20` becomes `(*(undefined (*))0x4e6d20)` with the
  type Ghidra inferred in that function (macros are defined and undefined per
  function). Pointer comparisons against absolute addresses
  (`(int)p < 0x56e950`) keep working. Emscripten's own data starts at
  `GLOBAL_BASE` = 32 MB, above every image.
- **Code addresses stay addresses.** Vtables and callbacks in the image hold
  x86 code addresses. A function name used as a value becomes its address;
  every call through a pointer, `(*p)(args)`, becomes `ICALLn(p, args)`, which
  looks the address up in a sorted table and calls a generated wrapper that
  takes eight `uint32_t` slots (the x86 stack layout). That sidesteps wasm's
  strict indirect-call signature checks. `__thiscall` methods take `this` as
  the first slot.
- **DLLs.** jgld.dll (graphsy) is translated the same way rather than
  reimplemented, at its own image base 0x10000000 (Emscripten's data starts
  above it, at 272 MB; untouched pages cost address space only), and its GDI
  calls are implemented in `src/port/gdi.c`. Terrain.dll is the native port in
  `src/platform/terrain_*.cpp`. sound.dll fails to load on purpose and golf's
  sound helpers are overridden onto `src/platform/sound.cpp`
  (`src/port/golf/sound.c`).
- **The CRT is not translated.** Ghidra's view of it is wrong in ways that
  matter (it drops the `this` passed to constructors in
  `eh vector constructor iterator`, x87 operands of `__ftol`). The CRT
  functions the game calls are mapped by address to `crt_*` in
  `tools/translate/golf.json`. stdio keeps MSVC's `FILE` layout because the
  game inlines `getc`.
- **Integer types.** `undefined1`/`undefined2` are signed, because Ghidra
  prints equality tests on them against negative constants (`!= -1`).
  Generated code compiles with `-fwrapv -fno-strict-aliasing`.
- **Blocking main loop.** WinMain runs as in the original. The Win32 message
  functions yield to the browser through JSPI (`-sJSPI`), so the game loop
  doesn't have to be restructured. (Needs a JSPI browser: Chrome 137+.)

## Fixing Ghidra's view before export

The raw decompile doesn't compile into a working program: call sites and
definitions disagree, virtual calls lose `this`, and some functions are
mis-split. `tools/ghidra/prepare-golf.sh` and `prepare-jgld.sh` fix the
program in a throwaway project copy, in this order:

1. `FixJumpTables.java` (golf): the 73 KB main loop `FUN_0040f5c0` and 8 small
   functions had switch tables Ghidra never resolved; their cases had been
   turned into bogus functions. The script deletes those, writes jump-table
   overrides and regrows the bodies. It also gives MSVC's `_chkstk`
   (0x4a6070) its `alloca_probe` call fixup, without which big-frame functions
   lose track of ESP.
2. `ApplySignatures.java`: exact signatures for the CRT functions with native
   replacements and for the variadic ones (`fprintf`, `sprintf`, `sscanf`);
   `tools/translate/{golf,jgld}_sigs.txt`.
3. `SetCdecl.java` (golf): functions whose `ret` Ghidra never found (stack
   purge unknown, so it assumed `__stdcall`) but that only `ret` become
   `__cdecl`; otherwise their callers' stack accesses after the call are
   shifted and grow phantom parameters.
4. `CommitSignatures.java`: commits the decompiler's own parameter and return
   types, callees first, so every call site matches its definition.
5. `OverrideVcalls.java`: MSVC virtual calls (`mov r,[ecx]; call [r+n]`) get a
   `__thiscall` call-site signature, so the decompiler passes `this` and knows
   the callee pops its arguments (otherwise later stack accesses are shifted).
   The argument count comes from the purges (`ret N`) of the methods at that
   slot in every vtable (`VtablePurges.java`). jgld calls golf's app object
   (`DAT_1012872c`, window-message forwarding) whose vtable is in golf.exe:
   `extpurges=` uses golf's purges for those calls.
6. `ExportPort.java` with `nopcalls=` for jgld's debug `__chkesp`, which the
   decompiler thinks clobbers EAX (functions "returned" garbage).

## Hand fixes

- `tools/translate/<module>.json` `patches`: text replacements applied to a
  function's decompiled C before translation, for small fixes.
- A function rewritten by hand goes in `src/port/` (or a subsystem folder)
  with a `// @override FUN_xxxxxxxx` comment line; the translator then skips
  its generated version.
- `build/gen/<module>/report.txt` lists what needs review per function:
  undefined register values (`in_EAX`, `unaff_ESI`, `extraout_EAX`), `__ftol()`
  calls that lost their operand, unrecovered switch tables, and Ghidra warnings.

## Rebuilding

```sh
tools/translate/run.sh          # Ghidra export (slow, ~30-40 min) only if missing, then translate
source ~/projects/emsdk/emsdk_env.sh
emcmake cmake -B build/web -S . && cmake --build build/web --target golf
node tools/boxedwine/serve.mjs build/web 8081   # open http://localhost:8081/golf.html
```

Nothing under `build/gen` may be committed: it's derived from the decompile.

## Running and debugging

```sh
node tools/boxedwine/serve.mjs build/web 8081 &          # serves build/web (game/ is a symlink)
node tools/port/run.mjs --ms 15000 --every 3000          # headless Chrome, screenshots in /tmp/golf-shots
node tools/port/run.mjs --url 'http://localhost:8081/golf.html?apitrace' ...   # log the first 400 Win32 calls
node tools/port/run.mjs --pause 5000 ...                 # print the JS/wasm stack at 5 s (finds hangs)
node tools/port/run.mjs --profile 20000-30000 ...        # CPU profile, self time per function
node tools/port/run.mjs --eval 'expr' ...                # evaluate in the page at the end
node tools/port/run.mjs --reload 30000 ...               # reload the page at 30 s (persistence)
node tools/port/record-preload.mjs                       # re-record web/preload.txt (files prefetched at startup)
MS=60000 tools/port/crash.sh --click 103,87@6000 --click 450,122@9000 --click 632,553@12000
                                                         # load the first autosave; first crash, symbolized
cd build/web && emsymbolizer -s sourcemap golf.wasm 0x1b0d81   # wasm offset from a stack -> gen/*.c:line
```

- Every generated function starts with `PORT_TRACE(addr)`; `port_abort` and a
  WinMain return print the last functions entered (`port_trace_dump`).
- `Module.fsDebug` (on in `web/shell.html`) logs each game file fetched.
- Generated code is built with `-O1 -fno-delete-null-pointer-checks
  -fno-finite-loops -fwrapv-pointer` (address 0 is ordinary memory here, the
  game spins on globals, and it indexes global arrays with negative indices:
  without `-fwrapv-pointer` LLVM folds the array address into the wasm load
  offset, and wasm traps instead of wrapping). `undefined4` is signed, like
  `undefined1/2`: Ghidra prints signed arithmetic on it without casts. Reads of variables the decompiler never assigned are UB and clang
  turns them into `unreachable` traps: when a trap points at such a read, the
  real fix is in Ghidra (usually a callee that lost its return value; see
  `void_used.txt` and `SetReturnInt.java`).
