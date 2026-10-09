#!/bin/sh
# Translates the Ghidra exports into build/gen (see docs/porting.md).
# Exports live outside the repo (they're decompiler output):
#   $PORT_EXPORTS/port       golf_nocd.exe   (tools/ghidra/export-parallel.sh)
#   $PORT_EXPORTS/port_jgld  jgld.dll        (ExportPort.java from=1)
#   $PORT_EXPORTS/*_externals.txt            (DumpExternals.java)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
X=${PORT_EXPORTS:-$HOME/ghidra_work}
gen=$root/build/gen
mkdir -p "$gen/images"
cp "$X/port/image.bin" "$gen/images/golf.bin"
cp "$X/port_jgld/image.bin" "$gen/images/jgld.bin"
python3 "$here/translate.py" --export "$X/port" --module golf --out "$gen/golf" \
  --range 0x401000-0x4a4f10 --config "$here/golf.json" --externals "$X/golf_externals.txt" \
  --overrides "$root/src"
python3 "$here/translate.py" --export "$X/port_jgld" --module jgld --out "$gen/jgld" \
  --range 0x10001000-0x1007e780 --config "$here/jgld.json" --externals "$X/jgld_externals.txt" \
  --overrides "$root/src"
