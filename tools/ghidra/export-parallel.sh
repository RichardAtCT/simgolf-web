#!/bin/sh
# Exports one program with ExportPort.java in parallel address chunks, each in
# its own copy of the Ghidra project (a project can only be opened once).
# usage: export-parallel.sh <project-dir-prefix> <program> <outdir> <from> <to> [<from> <to> ...]
# e.g.   export-parallel.sh ~/ghidra_work/projects_c golf_nocd.exe ~/ghidra_work/port 401000 420000 420000 4c0000
# Expects copies named <prefix>1, <prefix>2, ... each holding SimGolfDeps.gpr.
set -e
export JAVA_HOME=${JAVA_HOME:-/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home}
H=/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless
prefix=$1 prog=$2 out=$3
shift 3
i=1
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"
while [ $# -ge 2 ]; do
  extra=noimage
  [ $i = 1 ] && extra=
  "$H" "$prefix$i" SimGolfDeps -process "$prog" -noanalysis -readOnly -scriptPath "$here" \
    -postScript ExportPort.java "$out" from=$1 to=$2 timeout=${TIMEOUT:-60} $extra > "$out/export-$i.log" 2>&1 &
  i=$((i + 1))
  shift 2
done
wait
cat "$out"/functions-*.jsonl > "$out/functions.jsonl"
grep -h "slow:\|done:\|ERROR" "$out"/export-*.log || true
