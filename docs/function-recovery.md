# Recovering functions Ghidra missed (v1.02 golf.exe)

Ghidra's auto-analysis of `golf_nocd.exe` found 2358 functions, but left about 8,000 disassembled
instructions outside any function. Most of it is C++ methods reached only through golf.exe's own
vtables, which Ghidra doesn't treat as call targets. On 2026-10-07 `tools/ghidra/RecoverFunctions.java`
was run on the `SimGolfDeps` project and saved, bringing it to **3108 functions (+750)**.
`~/ghidra_work/out_v102/` was re-exported from it (2877 functions decompiled, up from 2180; thunks are skipped, and the export now takes ~20 minutes because some new functions hit the 30 s decompile timeout); the previous export is kept in
`~/ghidra_work/out_v102_before_recover/`, and the pre-change project in `~/ghidra_work/backup/`.

## What the script does

1. **Pointer tables.** It scans the non-executable sections for runs of aligned dwords that all point
   into `.text`. Runs of 3 or more are kept; pairs are kept only if something references the table
   start. There are 42 such tables, 28 of them referenced from code. Most are vtables in `.rdata` from
   0x004ba26c to 0x004bc510 (e.g. `GsApp` at 0x004ba87c). golf.exe has no RTTI, so neighbouring
   vtables run together and one "table" can be several classes. The 96-entry table at 0x004c1004 in
   `.data` is the CRT static-initializer list. Every target that isn't already a function, isn't inside
   one, and isn't data, padding or the middle of an instruction becomes a function: 391 in the first round.
2. **Orphan code.** After auto-analysis runs on those changes, any remaining instruction outside a
   function that isn't reached by fall-through starts a new function, unless a function jumps to it
   (that's an unfollowed switch case and belongs to its function). 312 were created this way, mostly
   callees of the new methods and SEH unwind funclets.
3. It repeats until a round creates nothing (two rounds here).

Afterwards every reference to the `Graphsy*` (0x0083ad50) and `GsScreen` (0x0083a7b8) globals is inside
a function, including the ones `graphsy.md` listed as orphaned (0x0047e7eb, 0x00480e51, 0x00479a74).

Left over: 25 orphan instructions at two jump targets (0x0046786a, 0x00470330) that sit inside
functions Ghidra didn't fully follow, and one table entry pointing inside an existing function.

## Reproducing

```sh
export JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home
H=/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless
# report only (read-only)
$H ~/ghidra_work/projects SimGolfDeps -process golf_nocd.exe -noanalysis -readOnly \
  -scriptPath tools/ghidra -postScript RecoverFunctions.java /tmp/recover.txt dry
# apply and save (drop -readOnly), then re-export
$H ~/ghidra_work/projects SimGolfDeps -process golf_nocd.exe -noanalysis \
  -scriptPath tools/ghidra -postScript RecoverFunctions.java ~/ghidra_work/recover_functions.txt
$H ~/ghidra_work/projects SimGolfDeps -process golf_nocd.exe -noanalysis -readOnly \
  -scriptPath tools/ghidra -postScript DumpSummary.java ~/ghidra_work/out_v102
```

The full log of what was created is `~/ghidra_work/recover_functions.txt` (outside the repo).
