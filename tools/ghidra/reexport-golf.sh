#!/bin/sh
# Quick iteration on the golf export without redoing prepare-golf.sh: applies
# the translator's accumulated void_used list to the prepared project
# ($W/projects_fix) and re-exports in parallel (~10 minutes).
set -e
export JAVA_HOME=${JAVA_HOME:-/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home}
H=/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless
W=${W:-$HOME/ghidra_work}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
P=$W/projects_fix
cat "$root/build/gen/golf/void_used.txt" "$W/void_used_golf.txt" 2>/dev/null | sort -u > "$W/void_used_golf.tmp"
mv "$W/void_used_golf.tmp" "$W/void_used_golf.txt"
"$H" "$P" SimGolfDeps -process golf_nocd.exe -noanalysis -scriptPath "$here" \
  -postScript SetReturnInt.java "$W/void_used_golf.txt" > "$W/reexport-1.log" 2>&1
grep -h "returns\|ERROR" "$W/reexport-1.log"
for i in 1 2 3 4 5 6; do
  rm -rf "$W/projects_c$i"; mkdir -p "$W/projects_c$i"
  cp -R "$P/SimGolfDeps.rep" "$P/SimGolfDeps.gpr" "$W/projects_c$i/"
done
rm -rf "$W/port"
TIMEOUT=400 "$here/export-parallel.sh" "$W/projects_c" golf_nocd.exe "$W/port" \
  401000 40f5c0 40f5c0 40f5c1 40f5c1 440000 440000 460000 460000 480000 480000 4c0000
