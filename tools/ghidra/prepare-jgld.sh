#!/bin/sh
# Rebuilds the jgld.dll export on a copy of the jgld project
# (where RecoverFunctions already ran): NopCalls (debug __chkesp and stack
# fills), ApplySignatures (CRT math), SetThiscall force (methods Ghidra marked
# __stdcall), OverrideVcalls, ExportPort. Signatures: everything is reset to
# "unknown" and re-inferred (the original analysis had void returns and
# __stdcall methods), then methods that read ECX first get `this` back.
# (where RecoverFunctions already ran), then ExportPort with __chkesp calls
# nopped out. Output: $W/port_jgld.
set -e
export JAVA_HOME=${JAVA_HOME:-/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home}
H=/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless
W=${W:-$HOME/ghidra_work}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
P=$W/projects_jgld
rm -rf "$P"; mkdir -p "$P"
cp -R "$W/projects_copy/SimGolfDeps.rep" "$W/projects_copy/SimGolfDeps.gpr" "$P/"
"$H" "$P" SimGolfDeps -process jgld.dll -noanalysis -scriptPath "$here" \
  -postScript NopCalls.java 1007e780 fill > "$W/prepare-jgld-n.log" 2>&1
grep -h "nopped\|ERROR" "$W/prepare-jgld-n.log"
"$H" "$P" SimGolfDeps -process jgld.dll -noanalysis -scriptPath "$here" \
  -postScript FixFtol.java > "$W/prepare-jgld-x.log" 2>&1
# exit, _amsg_exit
"$H" "$P" SimGolfDeps -process jgld.dll -noanalysis -scriptPath "$here" \
  -postScript MarkNoReturn.java 1007f1a0 10082870 > "$W/prepare-jgld-m.log" 2>&1
"$H" "$P" SimGolfDeps -process jgld.dll -noanalysis -scriptPath "$here" \
  -postScript ApplySignatures.java "$root/tools/translate/jgld_sigs.txt" > "$W/prepare-jgld-a.log" 2>&1
"$H" "$P" SimGolfDeps -process jgld.dll -noanalysis -scriptPath "$here" \
  -postScript SetThiscall.java 10001000-1007e780 reset > "$W/prepare-jgld-r.log" 2>&1
"$H" "$W/projects_copy" SimGolfDeps -process jgld.dll -noanalysis -readOnly -scriptPath "$here" \
  -postScript VtablePurges.java "$W/purges_jgld.txt" > "$W/prepare-jgld-p.log" 2>&1
"$H" "$P" SimGolfDeps -process jgld.dll -noanalysis -scriptPath "$here" \
  -postScript OverrideVcalls.java 4 60 10001000-1007e780 "purges=$W/purges_jgld.txt" "extpurges=$W/purges_golf.txt@1012872c" > "$W/prepare-jgld-o.log" 2>&1
"$H" "$P" SimGolfDeps -process jgld.dll -noanalysis -scriptPath "$here" \
  -postScript CommitSignatures.java 2 30 1007e780-10200000 > "$W/prepare-jgld-0.log" 2>&1
grep -h "round\|ERROR" "$W/prepare-jgld-0.log"
"$H" "$P" SimGolfDeps -process jgld.dll -noanalysis -scriptPath "$here" \
  -postScript SetThiscall.java 10001000-1007e780 force > "$W/prepare-jgld-f.log" 2>&1
grep -h "thiscall:\|ERROR" "$W/prepare-jgld-f.log"
# void functions whose callers use EAX (written by tools/translate)
if [ -f "$root/build/gen/jgld/void_used.txt" ]; then
  # accumulate across runs: each pass uncovers more
  cat "$root/build/gen/jgld/void_used.txt" "$W/void_used_jgld.txt" 2>/dev/null | sort -u > "$W/void_used_jgld.tmp"
  mv "$W/void_used_jgld.tmp" "$W/void_used_jgld.txt"
  "$H" "$P" SimGolfDeps -process jgld.dll -noanalysis -scriptPath "$here" \
    -postScript SetReturnInt.java "$W/void_used_jgld.txt" > "$W/prepare-jgld-v.log" 2>&1
  grep -h "returns\|ERROR" "$W/prepare-jgld-v.log"
fi
"$H" "$P" SimGolfDeps -process jgld.dll -noanalysis -scriptPath "$here" \
  -postScript OverrideVcalls.java 4 60 10001000-1007e780 "purges=$W/purges_jgld.txt" "extpurges=$W/purges_golf.txt@1012872c" > "$W/prepare-jgld-1.log" 2>&1
grep -h "vcall\|ERROR" "$W/prepare-jgld-1.log"
rm -rf "$W/port_jgld"
"$H" "$P" SimGolfDeps -process jgld.dll -noanalysis -readOnly -scriptPath "$here" \
  -postScript ExportPort.java "$W/port_jgld" from=1 > "$W/prepare-jgld-2.log" 2>&1
mv "$W/port_jgld/functions-00000001.jsonl" "$W/port_jgld/functions.jsonl"
grep -h "nopped\|done\|ERROR" "$W/prepare-jgld-2.log"
