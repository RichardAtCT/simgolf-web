#!/bin/sh
# Rebuilds the golf.exe export from scratch (see docs/porting.md):
#   copy of the analyzed project -> FixJumpTables -> ApplySignatures -> SetCdecl ->
#   OverrideVcalls -> CommitSignatures -> SetReturnInt -> OverrideVcalls (again,
#   with settled callee signatures) -> parallel ExportPort.
# Takes about an hour and a half. Output: $W/port (functions.jsonl, types.h, image.bin).
set -e
export JAVA_HOME=${JAVA_HOME:-/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home}
H=/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless
W=${W:-$HOME/ghidra_work}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
P=$W/projects_fix
rm -rf "$P"; mkdir -p "$P"
cp -R "$W/projects/SimGolfDeps.rep" "$W/projects/SimGolfDeps.gpr" "$P/"
run() { "$H" "$P" SimGolfDeps -process golf_nocd.exe -noanalysis -scriptPath "$here" -postScript "$@"; }
run FixJumpTables.java "$W/fixjumptables.txt" 40f5c0:421618 40d890 40d760 421b30 407d30 46f180 40b680 406f90 406f20:406f90 allocaprobe=4a6070 > "$W/prepare-1.log" 2>&1
run FixFtol.java > "$W/prepare-1b.log" 2>&1
# exit, _exit, _amsg_exit, the fatal-error helper
run MarkNoReturn.java 4a5108 4a5119 4a6937 4a695c > "$W/prepare-1c.log" 2>&1
run ApplySignatures.java "$root/tools/translate/golf_sigs.txt" > "$W/prepare-2.log" 2>&1
# stdcall functions whose RET was never found (purge unknown) but only `ret`
run SetCdecl.java > "$W/prepare-2c.log" 2>&1
# virtual calls first: until they pop their arguments, stack accesses after
# them are mis-mapped and CommitSignatures would drop real parameters
"$H" "$W/projects" SimGolfDeps -process golf_nocd.exe -noanalysis -readOnly -scriptPath "$here" \
  -postScript VtablePurges.java "$W/purges_golf.txt" > "$W/prepare-2a.log" 2>&1
PURGES="purges=$W/purges_golf.txt,$W/purges_jgld.txt"
run OverrideVcalls.java 4 60 401000-4a4f10 "$PURGES" > "$W/prepare-2b.log" 2>&1
run CommitSignatures.java 2 30 4a4f10-4c0000 > "$W/prepare-3.log" 2>&1
if [ -f "$root/build/gen/golf/void_used.txt" ]; then
  # accumulate across runs: each pass uncovers more
  cat "$root/build/gen/golf/void_used.txt" "$W/void_used_golf.txt" 2>/dev/null | sort -u > "$W/void_used_golf.tmp"
  mv "$W/void_used_golf.tmp" "$W/void_used_golf.txt"
  run SetReturnInt.java "$W/void_used_golf.txt" > "$W/prepare-3b.log" 2>&1
fi
run OverrideVcalls.java 4 60 401000-4a4f10 "$PURGES" > "$W/prepare-4.log" 2>&1
grep -h "round\|__cdecl on\|applied\|vcall\|no return\|ERROR" "$W"/prepare-*.log
for i in 1 2 3 4 5 6; do
  rm -rf "$W/projects_c$i"; mkdir -p "$W/projects_c$i"
  cp -R "$P/SimGolfDeps.rep" "$P/SimGolfDeps.gpr" "$W/projects_c$i/"
done
rm -rf "$W/port"
TIMEOUT=400 "$here/export-parallel.sh" "$W/projects_c" golf_nocd.exe "$W/port" \
  401000 40f5c0 40f5c0 40f5c1 40f5c1 440000 440000 460000 460000 480000 480000 4c0000
