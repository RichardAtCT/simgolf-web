#!/usr/bin/env python3
# Finds virtual calls where Ghidra's decompile passes fewer arguments than the
# machine code pushes (it sometimes drops them, e.g. the (x, y) of graphsy's
# GetPixelPtr calls). jgld.dll is an MSVC debug build: every call is bracketed
# by `mov esi, esp` ... `call [reg + slot]` ... `call _chkesp`, so the pushes
# in between are that call's arguments.
#
#   objdump -d --no-show-raw-insn --x86-asm-syntax=intel game/jgld.dll > jgld.asm
#   tools/translate/audit-vcall-args.py jgld.asm ~/ghidra_work/port_jgld/functions.jsonl tools/translate/jgld.json
#
# Prints function, vtable offset, push counts in the machine code and argument
# counts in the decompile (with jgld.json's patches applied). Known false
# alarms: a push for an outer call interleaved before an inner vcall (the
# FillRect(GetClipRect(), colour) pattern, MessageBox/BitBlt arguments), and
# calls whose `this` Ghidra wrote as the first argument.
import bisect, collections, json, re, sys

asm_path, funcs_path, cfg_path = sys.argv[1:4]
patches = json.load(open(cfg_path)).get("patches", {})
ins = []
for l in open(asm_path):
    m = re.match(r"\s*([0-9a-f]{8}):\s+(.*)", l)
    if m:
        ins.append((int(m.group(1), 16), m.group(2).strip()))
funcs = []
for line in open(funcs_path):
    d = json.loads(line)
    if "c" in d:
        c = d["c"]
        for old, new in patches.get(d["name"], []):
            c = c.replace(old, new)
        funcs.append((int(d["addr"], 16), d["name"], c))
funcs.sort()
starts = [f[0] for f in funcs]

pushed = collections.defaultdict(collections.Counter)  # name -> (offset, pushes) -> n
for i, (a, t) in enumerate(ins):
    m = re.match(r"call\s+dword ptr \[e[a-z]x \+ (0x[0-9a-f]+)\]$", t)
    if not m:
        continue
    j, n = i - 1, 0
    while j > 0 and i - j < 40 and ins[j][1] != "mov\tesi, esp":
        n += ins[j][1].startswith("push")
        j -= 1
    if ins[j][1] != "mov\tesi, esp":
        continue
    k = bisect.bisect_right(starts, a) - 1
    if k >= 0:
        pushed[funcs[k][1]][(int(m.group(1), 16), n)] += 1

def nargs(s):
    depth, n = 0, 1
    for ch in s:
        depth += ch == "("
        depth -= ch == ")"
        n += ch == "," and depth == 0
    return n - 1  # minus `this`

found = 0
for _, name, c in funcs:
    if name not in pushed:
        continue
    seen = collections.Counter()
    for m in re.finditer(r"\(\*\*\(code \*\*\)\(\*\(?(?:int \*\))?[\w\[\]\.]+\)? \+ (0x[0-9a-f]+)\)\)\s*\(([^;]*?)\);", c, re.S):
        seen[(int(m.group(1), 16), nargs(m.group(2)))] += 1
    asm = pushed[name]
    for off in sorted({o for o, _ in asm}):
        a = sorted(n for (o, n), k in asm.items() if o == off for _ in range(k))
        g = sorted(n for (o, n), k in seen.items() if o == off for _ in range(k))
        if len(a) == len(g) and any(x < y for x, y in zip(g, a)):
            found += 1
            print(name, hex(off), "pushes", a, "decompile", g)
print(found, "mismatches")
