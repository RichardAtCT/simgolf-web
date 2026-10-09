#!/usr/bin/env python3
"""Mechanical translation of a Ghidra export (tools/ghidra/ExportPort.java)
into C that Emscripten can compile.

The translated code keeps the original module's memory layout: the module's
image is loaded at its original virtual address in wasm linear memory, so
every global is reached through its address (DAT_004e6d20 becomes
(*(undefined (*))0x4e6d20)). Code addresses stored in data (vtables,
callbacks) stay as original addresses; indirect calls go through
icall_lookup(), which maps an address to a wrapper with a uniform
8 x uint32 signature.

Usage:
  translate.py --export DIR --module golf --out build/gen/golf \
      --range 0x401000-0x4a4f10 [--overrides src/port] [--config cfg.json]

Outputs in --out:
  <module>_types.h    Ghidra's types, adjusted for clang/wasm32
  <module>_protos.h   prototypes, thunk aliases, native replacements
  <module>_NN.c       translated functions, ~150 per file
  <module>_dispatch.c address -> wrapper table for icall
  report.txt          per-function issues to review by hand
"""
import argparse
import json
import os
import re
import sys

ap = argparse.ArgumentParser()
ap.add_argument("--export", required=True)
ap.add_argument("--module", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--range", action="append", default=[],
                help="address range whose functions are roots (lo-hi, hex)")
ap.add_argument("--overrides", action="append", default=[],
                help="directory scanned for hand-written FUN_xxxxxxxx definitions")
ap.add_argument("--config", help="JSON: native replacements, skips, patches")
ap.add_argument("--externals", help="DumpExternals.java output")
ap.add_argument("--per-file", type=int, default=150)
args = ap.parse_args()

mod = args.module
os.makedirs(args.out, exist_ok=True)
cfg = {}
if args.config:
    with open(args.config) as f:
        cfg = json.load(f)
native = {int(k, 16): v for k, v in cfg.get("native", {}).items()}   # addr -> C name
skip = {int(k, 16) for k in cfg.get("skip", [])}
patches = cfg.get("patches", {})                                    # name -> [[old, new], ...]

funcs = []
with open(os.path.join(args.export, "functions.jsonl")) as f:
    for line in f:
        funcs.append(json.loads(line))
byaddr = {int(d["addr"], 16): d for d in funcs}
byname = {}
for d in funcs:
    byname[d["name"]] = d
    byname.setdefault(re.sub(r"[^\w]", "_", d["name"]), d)

# ---------------------------------------------------------------- overrides
override_names = set()
override_sigs = {}   # name -> prototype taken from the override's definition
FUNDEF = re.compile(r"^[A-Za-z_][\w \*]*?\b((?:FUN|thunk_FUN)_[0-9a-f]{8}|[A-Za-z_]\w*)\s*\([^;]*$")
for od in args.overrides:
    for root, _, files in os.walk(od):
        for fn in files:
            if not fn.endswith((".c", ".cpp")):
                continue
            with open(os.path.join(root, fn)) as f:
                lines = f.read().split("\n")
            for i, line in enumerate(lines):
                m = re.match(r"^\s*//\s*@override\s+(\S+)", line)
                if not m:
                    continue
                override_names.add(m.group(1))
                # the definition that follows: lines up to the opening brace
                sig = []
                for j in range(i + 1, min(i + 30, len(lines))):
                    t = lines[j].strip()
                    if t.startswith("//") or not t:
                        continue
                    sig.append(t.split("{")[0].strip())
                    if "{" in t:
                        break
                override_sigs[m.group(1)] = " ".join(sig).strip()

# ---------------------------------------------------------------- C helpers
def c_ident(name):
    """Ghidra names to C identifiers, matching how call sites are rewritten:
    Class::method -> Class_method, Class::~Class -> Class_dtor."""
    n = name
    if "::" in n:
        parts = n.split("::")
        cls, meth = parts[-2], parts[-1]
        if cls.endswith(".DLL") or cls.endswith(".dll") or cls == "<EXTERNAL>":
            n = meth
        else:
            n = cls + "_" + ("dtor" if meth.startswith("~") else meth)
    return re.sub(r"[^\w]", "_", n)

def strip_comments(c):
    out, i, n = [], 0, len(c)
    while i < n:
        if c.startswith("/*", i):
            j = c.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(" " * 0)
            i = j
        elif c.startswith("//", i):
            j = c.find("\n", i)
            i = n if j < 0 else j
        elif c[i] in "\"'":
            q = c[i]
            j = i + 1
            while j < n and c[j] != q:
                j += 2 if c[j] == "\\" else 1
            out.append(c[i:j + 1])
            i = j + 1
        else:
            out.append(c[i])
            i += 1
    return "".join(out)

def match_paren(s, i):
    """s[i] == '(' ; returns index of matching ')'."""
    depth = 0
    j = i
    n = len(s)
    while j < n:
        ch = s[j]
        if ch in "\"'":
            q = ch
            j += 1
            while j < n and s[j] != q:
                j += 2 if s[j] == "\\" else 1
        elif ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return j
        j += 1
    return -1

def split_args(s):
    """Top-level comma split."""
    out, depth, cur, i = [], 0, [], 0
    while i < len(s):
        ch = s[i]
        if ch in "\"'":
            q = ch
            j = i + 1
            while j < len(s) and s[j] != q:
                j += 2 if s[j] == "\\" else 1
            cur.append(s[i:j + 1])
            i = j + 1
            continue
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
        i += 1
    last = "".join(cur).strip()
    if last or out:
        out.append(last)
    return out

def vcall_object(inner):
    """For `*(code **)(*OBJ + OFF)` or `*(code **)*OBJ` return OBJ."""
    m = re.match(r"^\s*\*\s*\(code \*\*\)\s*", inner)
    if not m:
        return None
    rest = inner[m.end():].strip()
    if rest.startswith("("):
        e = match_paren(rest, 0)
        if e != len(rest) - 1:
            return None
        content = rest[1:-1].strip()
        # last top-level " + "
        depth, cut = 0, -1
        for i, ch in enumerate(content):
            if ch in "([":
                depth += 1
            elif ch in ")]":
                depth -= 1
            elif depth == 0 and content.startswith(" + ", i):
                cut = i
        if cut < 0 or not re.match(r"^(?:0x[0-9a-fA-F]+|\d+)$", content[cut + 3:].strip()):
            return None
        left = content[:cut].strip()
    else:
        left = rest
    if not left.startswith("*"):
        return None
    obj = left[1:].strip()
    return obj or None

def rewrite_icalls(c):
    """(*EXPR)(args) -> ICALL(n, EXPR, args). Repeats until none are left so
    nested calls inside arguments are handled too."""
    changed = True
    while changed:
        changed = False
        i = 0
        while True:
            i = c.find("(*", i)
            if i < 0:
                break
            j = match_paren(c, i)
            if j < 0:
                i += 2
                continue
            j2 = j + 1
            while j2 < len(c) and c[j2] in " \t\n":
                j2 += 1
            if j2 >= len(c) or c[j2] != "(":
                i += 2
                continue
            # (*x)( must not be a cast like (*(type *)p) used in an expression:
            # a cast group ends in a type, but "(*" followed by an expression
            # and then "(" is a call in Ghidra output.
            inner = c[i + 2:j]
            k = match_paren(c, j2)
            if k < 0:
                i += 2
                continue
            argstr = c[j2 + 1:k]
            a = split_args(argstr) if argstr.strip() else []
            obj = vcall_object(inner)
            def bare(x):
                return re.sub(r"^(?:\(\s*[A-Za-z_][\w\s\*]*\)\s*)+", "", x.strip())
            if obj is not None and a and bare(a[0]) == bare(obj):
                # the call-site override passes `this` explicitly; icall_this
                # still decides, because some methods ignore (and don't
                # declare) it
                a = a[1:]
            if obj is not None and len(a) < 15:
                # a virtual call: Ghidra drops the `this` MSVC passes in ECX;
                # icall_this passes it when the target is a __thiscall method
                new = "ICALLT%d(%s, %s%s)" % (len(a), inner, obj, "".join(", " + x for x in a))
            else:
                new = "ICALL%d(%s%s)" % (len(a), inner, "".join(", " + x for x in a))
            c = c[:i] + new + c[k + 1:]
            changed = True
            i += len("ICALL")
    return c

PIECE = re.compile(r"((?:[A-Za-z_]\w*)(?:(?:->|\.)(?!_\d+_\d+_)[A-Za-z_]\w*|\[[^\[\]]*\])*)\._(\d+)_(\d+)_")

def rewrite_pieces(c):
    return PIECE.sub(lambda m: "PIECE(%s,%s,%s)" % (m.group(1), m.group(2), m.group(3)), c)

FLOAT_CASE = re.compile(r"\bcase\s+(-?\d+(?:\.\d+)?e[-+]?\d+|-?\d+\.\d+)\s*:")

PTR_CASE = re.compile(r"\bcase\s+\((?:[\w\s]+\*+|[A-Z][A-Z0-9_]*)\)\s*(-?(?:0x[0-9a-fA-F]+|\d+))\s*:")

def fix_float_puns(c, d, issues):
    """Ghidra sometimes types an int variable float (a register or global the
    compiler reused, or a parameter slot it stored float bits into). Then
    `(int)v` and `v = (float)(x)` are bit reinterpretations, not
    conversions (real float->int goes through __ftol in this code). Applies to
    float names that appear under an (int)/(uint) cast."""
    names = set(g["name"] for g in d.get("globals", []) if g.get("type") == "float")
    names |= set(re.findall(r"\bfloat\s+(\w+)\s*[,);]", c))
    puns = sorted(v for v in names if re.search(r"\(u?int\)\s*%s\b" % re.escape(v), c))
    if not puns:
        return c
    issues.append("float puns: " + " ".join(puns))
    for v in puns:
        ev = re.escape(v)
        c = re.sub(r"\(int\)\s*%s\b" % ev, "F2I(%s)" % v, c)
        c = re.sub(r"\(uint\)\s*%s\b" % ev, "F2U(%s)" % v, c)
        # v = (float)<operand>  ->  v = I2F(<operand>)
        out, i = [], 0
        for m in re.finditer(r"\b%s = \(float\)" % ev, c):
            if m.start() < i:
                continue
            out.append(c[i:m.start()])
            j = m.end()
            if c[j] == "(":
                depth = 0
                k = j
                while True:
                    if c[k] == "(":
                        depth += 1
                    elif c[k] == ")":
                        depth -= 1
                        if depth == 0:
                            break
                    k += 1
                arg = c[j:k + 1]
                i = k + 1
            else:
                k = j
                while c[k] not in ";,)":
                    k += 1
                arg = "(" + c[j:k] + ")"
                i = k
            out.append("%s = I2F%s" % (v, arg))
        out.append(c[i:])
        c = "".join(out)
    return c


def fix_float_switches(c, issues):
    """Ghidra sometimes types a switch variable as float and prints the case
    labels as tiny floats (2.8026e-45 is the int 2). Use the bit patterns."""
    if not FLOAT_CASE.search(c) and not PTR_CASE.search(c):
        return c
    import struct
    def to_int(m):
        v = struct.unpack("<i", struct.pack("<f", float(m.group(1))))[0]
        return "case %d:" % v
    out, i = [], 0
    for m in re.finditer(r"\bswitch\s*\(", c):
        if m.start() < i:
            continue
        start = m.end() - 1
        end = match_paren(c, start)
        if end < 0:
            continue
        b = c.find("{", end)
        be = b
        depth = 0
        while be < len(c):
            if c[be] == "{": depth += 1
            elif c[be] == "}":
                depth -= 1
                if depth == 0: break
            be += 1
        block = c[b:be]
        if PTR_CASE.search(block):
            out.append(c[i:start + 1])
            out.append("(int)(uintptr_t)(" + c[start + 1:end] + ")")
            out.append(c[end:b])
            out.append(PTR_CASE.sub(r"case \1:", block))
            i = be
            continue
        if FLOAT_CASE.search(block):
            out.append(c[i:start + 1])
            out.append("F2U(" + c[start + 1:end] + ")")
            out.append(c[end:b])
            out.append(FLOAT_CASE.sub(to_int, block))
            i = be
            issues.append("switch on a float-typed value (cases converted to bit patterns)")
    out.append(c[i:])
    return "".join(out)

ADDR_NAME = re.compile(r"(?<!goto )(?:\b(?:FUN|thunk_FUN)_|&LAB_)([0-9a-f]{8})\b(?!\s*[\(:])")

# ---------------------------------------------------------------- signatures
SIG = re.compile(r"^(.*?)\b([A-Za-z_][\w@]*)\s*\((.*)\)\s*;?\s*$", re.S)
CONVS = ("__cdecl", "__stdcall", "__thiscall", "__fastcall", "__vectorcall")

def parse_sig(sig):
    sig = sig.strip().rstrip(";").strip()
    sig = re.sub(r"\b([A-Za-z_]\w*)::\s*~\s*\w+\s*\(", r"\1_dtor(", sig)
    sig = re.sub(r"\b([A-Za-z_]\w*)::\s*([A-Za-z_]\w*)\s*\(", r"\1_\2(", sig)
    for cv in CONVS:
        sig = sig.replace(cv + " ", "")
    m = SIG.match(sig)
    if not m:
        return None
    ret, name, params = m.group(1).strip(), m.group(2), m.group(3).strip()
    plist = [] if params in ("", "void") else split_args(params)
    return ret, name, plist

def param_type(p):
    p = p.strip()
    if p == "...":
        return "..."
    m = re.match(r"^(.*?)([A-Za-z_]\w*)\s*((?:\[\d*\])*)$", p)
    if not m:
        return p
    t = m.group(1).strip()
    if m.group(3):
        t += " *"
    return t if t else p

def slots_for(t):
    t = t.replace(" ", "")
    if t in ("double", "float10", "longlong", "ulonglong", "undefined8", "longdouble",
             "LONGLONG", "ULONGLONG", "__int64", "int64_t", "uint64_t", "QWORD"):
        return 2
    return 1

def arg_from_slots(t, idx):
    ts = t.replace(" ", "")
    if ts == "float":
        return "U2F(a%d)" % idx, 1
    if ts in ("double", "float10", "longdouble"):
        return "U2D(a%d,a%d)" % (idx, idx + 1), 2
    if slots_for(ts) == 2:
        return "U2Q(a%d,a%d)" % (idx, idx + 1), 2
    return "(%s)a%d" % (t, idx), 1

# ---------------------------------------------------------------- selection
lo_hi = []
for r in args.range:
    lo, hi = r.split("-")
    lo_hi.append((int(lo, 16), int(hi, 16)))

def in_roots(a):
    return any(lo <= a < hi for lo, hi in lo_hi)

def resolve(d):
    seen = 0
    while d and "thunk" in d and seen < 8:
        t = byaddr.get(int(d["thunkAddr"], 16))
        if not t:
            return d
        d = t
        seen += 1
    return d

CALLREF = re.compile(r"\b((?:thunk_)?FUN_[0-9a-f]{8}|[A-Za-z_][\w@]*)\b")

selected = {}
work = [a for a in byaddr if in_roots(a)]
while work:
    a = work.pop()
    if a in selected or a in skip:
        continue
    d = byaddr.get(a)
    if not d or d["name"].startswith("Unwind@"):
        continue
    selected[a] = d
    if a in native:
        continue
    if "thunk" in d:
        if not d.get("thunkExternal"):
            work.append(int(d["thunkAddr"], 16))
        continue
    if "c" not in d or c_ident(d["name"]) in override_names:
        continue
    for m in re.finditer(r"\b(?:thunk_)?FUN_([0-9a-f]{8})\b(\s*\()?", d["c"]):
        t = int(m.group(1), 16)
        # Address-taken functions outside the roots (SEH handlers, CRT
        # internals) only become table entries if something calls them.
        if m.group(2) or in_roots(t):
            work.append(t)
    for m in re.finditer(r"\b([A-Za-z_][\w@]*)\s*\(", d["c"]):
        t = byname.get(m.group(1))
        if t and not t["name"].startswith("Unwind@"):
            work.append(int(t["addr"], 16))

# ---------------------------------------------------------------- void returns used
# Ghidra infers each function's return type on its own, so a function it
# decided is void can still have callers that use EAX. Those get an
# undefined4 return (0 where the body has no value) and a report line.
def resolve_name(n):
    d = byname.get(n)
    if d and "thunk" in d and not d.get("thunkExternal"):
        t = resolve(d)
        return t["name"] if t else n
    return n

used_result = set()
STMT_CALL = re.compile(r"^\s*([A-Za-z_]\w*)\s*\(")
for a, d in selected.items():
    c = d.get("c")
    if not c:
        continue
    for m in re.finditer(r"\b((?:thunk_)?FUN_[0-9a-f]{8})\s*\(", c):
        ls = c.rfind("\n", 0, m.start()) + 1
        if c[ls:m.start()].strip() == "":
            continue   # statement call: result unused
        used_result.add(resolve_name(m.group(1)))

void_fixed = set()
for a, d in selected.items():
    if d.get("sig") and a not in native and d["name"] in used_result:
        ps = parse_sig(d["sig"])
        if ps and ps[0].strip() == "void":
            void_fixed.add(d["name"])

with open(os.path.join(args.out, "void_used.txt"), "w") as f:
    for a in sorted(a for a, d in selected.items() if d["name"] in void_fixed):
        f.write("%08x\n" % a)

# ---------------------------------------------------------------- types.h
with open(os.path.join(args.export, "types.h")) as f:
    types = f.read()
types = "\n".join(l for l in types.split("\n") if not re.match(r"^\s*[A-Za-z_]\w*\s*$", l))
types = re.sub(r"^typedef [^;]*\b(u?int(?:8|16|32|64|ptr)_t|intptr_t|uintptr_t)\s*;\s*$", "", types, flags=re.M)
types = types.replace("typedef unsigned long long    GUID;", "typedef struct { uint32_t d[4]; } GUID;") if False else types
for ph in re.findall(r"struct (\w+) \{ /\* PlaceHolder Structure \*/\s*\};", types):
    types = re.sub(r"struct %s \{ /\* PlaceHolder Structure \*/\s*\};" % ph, "", types)
    types = re.sub(r"typedef struct %s %s\b" % (ph, ph), "typedef undefined %s" % ph, types)
    types = re.sub(r"\bstruct %s\b" % ph, ph, types)
types += "\n" + cfg.get("extra_types", "") + "\n"
types = re.sub(r"typedef unsigned char\s+undefined;", "typedef signed char undefined;", types)
types = re.sub(r"typedef unsigned char\s+undefined1;", "typedef signed char undefined1;", types)
types = re.sub(r"typedef unsigned short\s+undefined2;", "typedef short undefined2;", types)
# undefined4 too: Ghidra prints signed arithmetic on undefined4 values without
# casts (sar/idiv), and C would otherwise promote whole expressions to unsigned
types = re.sub(r"typedef unsigned int\s+undefined4;", "typedef int undefined4;", types)
types = re.sub(r"typedef unsigned char\s+bool;", "typedef unsigned char gbool;\n#define bool gbool", types)
with open(os.path.join(args.out, mod + "_types.h"), "w") as f:
    f.write("// Generated from the Ghidra export by tools/translate. Do not edit.\n")
    f.write("#pragma once\n#include \"port/ghidra_prelude.h\"\n")
    f.write(types)

# ---------------------------------------------------------------- protos
report = []
thiscall_addrs = set()
protos = []
wrappers = []
dispatch = []
emitted_names = set()

externals = {}
if args.externals:
    with open(args.externals) as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 4:
                externals[parts[1]] = parts

def widen_params(text, c):
    """`_param_N` means a 4-byte access to a parameter Ghidra typed narrower;
    the stack slot is 4 bytes, so make the parameter undefined4."""
    for n in set(re.findall(r"\b_param_(\d+)\b", c or "")):
        text = re.sub(r"\b(?:char|byte|undefined1?|undefined2|short|ushort|bool|gbool|uchar)\s+param_%s\b" % n,
                      "undefined4 param_%s" % n, text)
    return text

def proto_of(d):
    if d.get("sig"):
        sig = d["sig"]
        for old, new in patches.get(c_ident(d["name"]), []):
            sig = sig.replace(old, new)
        sig = widen_params(sig, d.get("c"))
        return sig.strip().rstrip(";")
    return None

for a in sorted(selected):
    d = selected[a]
    name = c_ident(d["name"])
    if a in native:
        protos.append("#define %s %s" % (name, native[a]))
        dispatch.append((a, "W_" + native[a], True))
        continue
    if "thunk" in d:
        t = resolve(d)
        tname = c_ident(t["name"])
        if name != tname:
            protos.append("#define %s %s" % (name, tname))
        if d.get("thunkExternal"):
            pass   # imports aren't called through pointers
        else:
            ta = int(t["addr"], 16)
            dispatch.append((a, None, ta))
        continue
    sig = proto_of(d)
    if not sig:
        report.append("%s %s: decompile failed: %s" % (d["addr"], name, d.get("error")))
        continue
    ps = parse_sig(sig)
    if not ps:
        report.append("%s %s: unparsed signature %r" % (d["addr"], name, sig))
        continue
    ret, sname, plist = ps
    sname = c_ident(sname)
    if d["name"] in void_fixed:
        ret = "undefined4"
        report.append("%s %s: void, but callers use its result (returns 0 at plain returns)" % (d["addr"], name))
    if not re.match(r"^FUN_[0-9a-f]{8}$", sname):
        # named functions (FID, Ghidra) can repeat across modules
        protos.append("#define %s %s_%s" % (sname, mod, sname))
    if sname in override_sigs:
        # hand-written replacement: its own signature is the one callers see
        protos.append(override_sigs[sname] + ";")
        ps2 = parse_sig(override_sigs[sname])
        if ps2:
            ret, _, plist = ps2
    else:
        protos.append("%s %s(%s);" % (ret, sname, ", ".join(plist) if plist else "void"))
    # wrapper with the uniform icall signature
    call_args, idx = [], 0
    for p in plist:
        t = param_type(p)
        if t == "...":
            while idx < 16:
                call_args.append("a%d" % idx)
                idx += 1
            break
        if idx >= 16:
            report.append("%s %s: more than 16 argument slots" % (d["addr"], name))
            call_args.append("0")
            continue
        expr, n = arg_from_slots(t, idx)
        call_args.append(expr)
        idx += n
    call = "%s(%s)" % (sname, ", ".join(call_args))
    r = ret.replace(" ", "")
    if r == "void":
        body = "%s; return 0;" % call
    elif r in ("float", "double", "float10"):
        body = "g_fret = %s; return 0;" % call
    elif slots_for(r) == 2:
        body = "uint64_t v = (uint64_t)%s; g_edx = (uint32_t)(v >> 32); return (uint32_t)v;" % call
    elif ret.strip().startswith(("struct", "union")) and "*" not in ret:
        body = "%s; return 0; /* struct return */" % call
        report.append("%s %s: returns a struct by value" % (d["addr"], name))
    else:
        body = "return (uint32_t)(uintptr_t)%s;" % call
    wrappers.append("static uint32_t W_%s(ICALL_PARAMS) { %s }" % (sname, body))
    dispatch.append((a, "W_" + sname, False))
    # __fastcall too: a method that never touches EDX looks like
    # fastcall(ECX) to Ghidra, and its ECX is still `this`
    if "__thiscall" in sig or "__fastcall" in sig:
        thiscall_addrs.add(a)
    emitted_names.add(sname)

ext_protos = []
ext_info = {}
for nm, parts in sorted(externals.items()):
    ps = parse_sig(parts[3])
    if not ps:
        report.append("external %s: unparsed prototype %r" % (nm, parts[3]))
        continue
    ret, sname, plist = ps
    ret = ret.replace("~", "").strip() or "void"
    lib, conv = parts[0], parts[2]
    cname = c_ident(nm)
    if lib in cfg.get("class_libs", []):
        cname = lib + "_" + ("dtor" if nm.startswith("~") else cname)
    m = re.search(r"@(\d+)$", nm)
    if ret == "undefined" and not plist and m:
        # unknown prototype (Bink): call sites disagree on the count
        ret, plist = "uint32_t", ["nu a0", "..."]
    if conv == "__thiscall":
        plist = ["void *this"] + plist
    ext_protos.append("%s %s(%s);  /* %s */" % (ret, cname, ", ".join(plist) if plist else "void", parts[0]))
    ext_info[cname] = (ret, plist)
with open(os.path.join(args.out, mod + "_externs.h"), "w") as f:
    f.write("// Generated by tools/translate: imported functions. Do not edit.\n#pragma once\n")
    f.write("#include \"%s_types.h\"\n\n" % mod)
    f.write("\n".join(ext_protos) + "\n")

with open(os.path.join(args.out, mod + "_protos.h"), "w") as f:
    f.write("// Generated by tools/translate. Do not edit.\n#pragma once\n")
    f.write("#include \"%s_types.h\"\n#include \"%s_externs.h\"\n#include \"%s_exrefs.h\"\n#include \"port/native.h\"\n\n" % (mod, mod, mod))
    f.write("\n".join(protos))
    f.write("\n")

# ---------------------------------------------------------------- functions
REGVAR = re.compile(r"\b((?:in|unaff|extraout)_[A-Za-z0-9_]+|stack0x[0-9a-f]+|register0x[0-9a-f]+)\b")

exrefs = set()

FRAME_DECL = re.compile(r"^(\s*)([A-Za-z_][\w ]*?[\w\*])\s+(\**)((?:local|[a-zA-Z]+Stack)_[0-9a-f]+)\s*((?:\[\d+\])*)\s*;\s*$")

def frame_locals(c, fname):
    """Put Ghidra's stack locals at their real offsets in one frame buffer.
    Ghidra sizes a local by how it's accessed, but the code may copy a whole
    struct over it (a 128-byte PCX header into `undefined4 local_80`) or read
    a neighbour through it; a C local would overflow. local_80 lives 0x80
    bytes below the return address, so the frame reproduces that layout."""
    brace = c.find("{")
    if brace < 0:
        return c
    body_start = brace + 1
    lines = c[body_start:].split("\n")
    decls, end = [], None
    for i, line in enumerate(lines):
        if i > 0 and line.strip() == "":
            end = i
            break
        m = FRAME_DECL.match(line)
        if m:
            decls.append((i, m.group(2).strip() + (" " + m.group(3) if m.group(3) else ""), m.group(4), m.group(5)))
    if not decls:
        return c
    maxoff = max(int(n.split("_")[-1], 16) for _, _, n, _ in decls)
    size = (maxoff + 16 + 15) & ~15
    defs = []
    for i, typ, name, dims in decls:
        off = int(name.split("_")[-1], 16)
        if dims:
            ptr = "%s (*)%s" % (typ, dims)
        else:
            ptr = "%s *" % typ
        defs.append("#define %s (*(%s)(port_frame + %d))" % (name, ptr, size - off))
        lines[i] = None
    lines = [l for l in lines if l is not None]
    lines.insert(1 if lines and lines[0].strip() == "" else 0,
                 "  char port_frame[%d] __attribute__((aligned(16)));" % size)
    head = c[:body_start]
    # macros go before the function so they cover its whole body
    return "\n".join(defs) + "\n" + head + "\n".join(lines), [n for _, _, n, _ in decls]

def translate_fn(d):
    name = c_ident(d["name"])
    c = d["c"]
    issues = []
    for old, new in patches.get(name, []):
        if old not in c:
            issues.append("patch not applied: %r" % old[:60])
        c = c.replace(old, new)
    c = widen_params(c, c)
    c = re.sub(r"\b_(param_\d+)\b", r"\1", c)
    c = strip_comments(c)
    if d["name"] in void_fixed:
        c = re.sub(r"\bvoid(\s+(?:__\w+\s+)?%s\s*\()" % re.escape(d["name"]), r"undefined4\1", c, count=1)
        c = re.sub(r"\breturn\s*;", "return 0;", c)
    # calling conventions
    for cv in CONVS:
        c = c.replace(cv + " ", "")
    # Ghidra prints imported decorated names with @ -> _
    c = re.sub(r"\b(_\w+)@(\d+)\b", r"\1_\2", c)
    # 64-bit constants from 32-bit wraparound (psVar[-0xffffffff0000000b]
    # means index -11): keep the low 32 bits, signed
    c = re.sub(r"-?0x[0-9a-f]{9,16}\b", lambda m: "((int)(%s))" % m.group(0), c)
    c = re.sub(r"\b([A-Za-z_]\w*)::~\w+\b", r"\1_dtor", c)
    c = re.sub(r"\b([A-Za-z_]\w*)::([A-Za-z_]\w*)\b", r"\1_\2", c)
    # debug-build frame fill (0xCC over the whole frame): in C it would
    # overrun the array it starts at, so drop it
    c = re.sub(r"for \((\w+) = \w+; \1 != 0; \1 = \1 \+ -1\) \{\s*\*(\w+) = 0xcccccccc;\s*\2 = \2 \+ 1;\s*\}", "", c)
    framed = []
    fr = frame_locals(c, d["name"])
    if isinstance(fr, tuple):
        c, framed = fr
    c = fix_float_switches(c, issues)
    c = fix_float_puns(c, d, issues)
    c = rewrite_pieces(c)
    if re.search(r"\b_(local_|[a-zA-Z]+Stack_?)[0-9a-f]+\b", c):
        issues.append("wide access to a stack local (_local_X)")
    c = re.sub(r"\b_((?:local_|[a-zA-Z]+Stack_?)[0-9a-f]+)\b", r"PIECE(\1,0,4)", c)
    for x in set(re.findall(r"\b(\w+)_exref\b", c)):
        exrefs.add(x)
    c = re.sub(r"\b(\w+)_exref\b", r"EXREF_\1", c)
    c = rewrite_icalls(c)
    # function and label names used as values are original addresses
    c = ADDR_NAME.sub(lambda m: "((code *)0x%s)" % m.group(1).lstrip("0"), c)
    # undeclared register / stack pseudo-variables
    regs = sorted(set(REGVAR.findall(c)))
    if regs:
        brace = c.find("{")
        decls = "".join("  int %s = 0; /* PORT: undefined register/stack value */\n" % r for r in regs
                        if not re.search(r"\b\w[\w\s\*]*\b%s\s*(?:\[|;|=)" % re.escape(r), c[:c.find("\n\n", brace) if c.find("\n\n", brace) > 0 else len(c)]))
        c = c[:brace + 1] + "\n" + decls + c[brace + 1:]
        issues.append("register/stack pseudo-vars: " + " ".join(regs))
    defs, undefs = [], []
    widened_globals = []
    if "__ftol()" in c:
        issues.append("__ftol() without argument (x87 stack value lost)")
    if "switchdataD" in c:
        issues.append("unrecovered switch jump table")
    if "halt_" in c:
        issues.append("halt_* (bad data or unimplemented instruction)")
    for w in re.findall(r"WARNING: ([^*]*)\*/", d["c"]):
        issues.append("warning: " + w.strip()[:100])
    # _DAT_x: an access wider than the symbol at x; take the decompiler's
    # widest varnode there
    widest = {}
    for addr, size, typ in d.get("mem", []):
        a = int(addr, 16)
        if a not in widest or size > widest[a][0]:
            widest[a] = (size, typ)
    for m in sorted(set(re.findall(r"\b_((?:DAT|PTR_DAT|PTR)_([0-9a-f]{8}))\b", c))):
        a = int(m[1], 16)
        size, typ = widest.get(a, (4, ""))
        if not typ:
            typ = {1: "undefined1 (*)", 2: "undefined2 (*)", 8: "undefined8 (*)"}.get(size, "undefined4 (*)")
        defs.append("#define _%s (*(%s)0x%x)" % (m[0], typ, a))
        undefs.append("#undef _%s" % m[0])
    for g in d.get("globals", []):
        gname = g["name"]
        if not g["addr"] or not re.match(r"^[0-9a-f]+$", g["addr"]):
            continue
        if re.match(r"^(?:FUN|LAB|thunk_FUN)_[0-9a-f]{8}$", gname) or gname == "ExceptionList":
            continue
        # A symbol Ghidra left untyped (undefined*): the decompiler's varnode
        # type at that address is more useful (e.g. a pointer it indexes).
        if re.match(r"^undefined\d?\s*\(\*\)$", g["ptr"] or ""):
            a = int(g["addr"], 16)
            better = [t for (ad, sz, t) in d.get("mem", []) if int(ad, 16) == a and sz == g["size"] and t and not t.startswith("undefined")]
            if better:
                g = dict(g, ptr=better[0])
            elif (g["size"] == 1 and not re.search(r"&%s\b|\b_%s\b" % (re.escape(gname), re.escape(gname)), c)
                  and not any(int(ad, 16) == a and sz == 1 for (ad, sz, t) in d.get("mem", []))):
                # 1-byte untyped symbol only ever read/written as a value:
                # use the widest access the decompiler made there
                wide = sorted(((sz, t) for (ad, sz, t) in d.get("mem", []) if int(ad, 16) == a and sz > 1),
                              key=lambda x: (x[0], not x[1].startswith("undefined")))
                if wide:
                    g = dict(g, ptr=wide[-1][1] or {2: "undefined2 (*)", 8: "undefined8 (*)"}.get(wide[-1][0], "undefined4 (*)"))
                    widened_globals.append(gname)
        if not re.search(r"\b%s\b" % re.escape(gname), c):
            continue
        ptr = g["ptr"] or "undefined4 (*)"
        ptr = re.sub(r"^(?:string|TerminatedCString|pascal\w*)\s*\(\*\)$", "char (*)[1]", ptr)
        if re.match(r"^s_", gname) and re.match(r"^(?:char|undefined1?|byte)\s*\(\*\)$", ptr):
            ptr = "char (*)[1]"
        ptr = re.sub(r"^(?:unicode|TerminatedUnicode|unicode32)\s*\(\*\)$", "wchar16 (*)[1]", ptr)
        defs.append("#define %s (*(%s)0x%s)" % (gname, ptr, g["addr"].lstrip("0") or "0"))
        undefs.append("#undef %s" % gname)
    if widened_globals:
        issues.append("untyped 1-byte globals widened: " + " ".join(widened_globals))
    # names the decompiler printed but didn't list as globals
    defined = {x.split()[1] for x in defs}
    for m in sorted(set(re.findall(r"\b(switchD_[0-9a-f]{8}_switchdataD_([0-9a-f]{8}))\b", c))):
        defs.append("#define %s (*(uint32_t *)0x%x)" % (m[0], int(m[1], 16)))
        undefs.append("#undef %s" % m[0])
        issues.append("switch through a raw jump table (cases are code addresses)")
    memtype = {}
    for addr, size, typ in d.get("mem", []):
        memtype.setdefault(int(addr, 16), typ)
    for m in sorted(set(re.findall(r"\b([a-z]+Ram([0-9a-f]{8}))\b", c))):
        nm, a = m
        if nm in defined:
            continue
        t = memtype.get(int(a, 16)) or "undefined4 (*)"
        defs.append("#define %s (*(%s)0x%x)" % (nm, t, int(a, 16)))
        undefs.append("#undef %s" % nm)
        defined.add(nm)
    for m in sorted(set(re.findall(r"\b((?:s|u|PTR_s|PTR_u|PTR_DAT|PTR|DAT|BYTE|WORD|DWORD|PTR_FUN)_\w*?([0-9a-f]{8}))\b", c))):
        nm, a = m
        if nm in defined or not re.search(r"\b%s\b" % re.escape(nm), c):
            continue
        if re.match(r"^(?:s|u)_", nm):
            t = "char (*)[1]"
        else:
            t = "undefined4 (*)"
        defs.append("#define %s (*(%s)0x%s)" % (nm, t, a.lstrip("0") or "0"))
        undefs.append("#undef %s" % nm)
        defined.add(nm)
    # PORT_TRACE_RETURNS=FUN_a,FUN_b: log every return value of those functions
    if name in os.environ.get("PORT_TRACE_RETURNS", "").split(","):
        c = re.sub(r"\breturn\s+([^;]+);", lambda m: "return PORT_RET(0x%s, %d, %s);" % (d["addr"], m.start(), m.group(1)), c)
    # trace hook: first statement of the body
    brace = c.find("{")
    if brace >= 0:
        c = c[:brace + 1] + "\n  PORT_TRACE(0x%s);" % d["addr"] + c[brace + 1:]
    out = "/* %s @ %s */\n" % (name, d["addr"])
    if defs:
        out += "\n".join(defs) + "\n"
    out += c.strip() + "\n"
    undefs += ["#undef %s" % n for n in framed]
    if undefs:
        out += "\n".join(undefs) + "\n"
    return out, issues

chunks = []
cur = []
for a in sorted(selected):
    d = selected[a]
    name = c_ident(d["name"])
    if a in native or "thunk" in d or "c" not in d:
        continue
    if name in override_names:
        continue
    text, issues = translate_fn(d)
    for i in issues:
        report.append("%s %s: %s" % (d["addr"], name, i))
    cur.append(text)
    if len(cur) >= args.per_file:
        chunks.append(cur)
        cur = []
if cur:
    chunks.append(cur)

old = [f for f in os.listdir(args.out) if re.match(r"%s_\d+\.c$" % mod, f)]
for f in old:
    os.remove(os.path.join(args.out, f))
for i, ch in enumerate(chunks):
    with open(os.path.join(args.out, "%s_%02d.c" % (mod, i)), "w") as f:
        f.write("// Generated by tools/translate from the Ghidra decompile. Do not edit.\n")
        f.write("#include \"%s_protos.h\"\n\n" % mod)
        f.write("\n".join(ch))

# Imports whose address the code takes (X_exref): a fake code address that
# icall resolves to a wrapper calling the import.
exref_entries = []
with open(os.path.join(args.out, mod + "_exrefs.h"), "w") as f:
    f.write("// Generated by tools/translate. Do not edit.\n#pragma once\n#include <stdint.h>\n")
    for i, x in enumerate(sorted(exrefs)):
        f.write("extern uint32_t EXREF_%s;\n" % x)
        exref_entries.append((x, 0xE0000000 + (hash(mod) & 0xff) * 0x10000 + i * 16))
with open(os.path.join(args.out, mod + "_dispatch.c"), "w") as f:
    f.write("// Generated by tools/translate. Do not edit.\n#include \"%s_protos.h\"\n\n" % mod)
    for x, fake in exref_entries:
        f.write("uint32_t EXREF_%s = 0x%x;\n" % (x, fake))
        ret, plist = ext_info.get(x, ("uint32_t", ["..."]))
        args_ = []
        for k, prm in enumerate(plist):
            if prm.strip() == "...":
                break
            args_.append("(%s)a%d" % (param_type(prm), k))
        call = "%s(%s)" % (x, ", ".join(args_))
        body = "%s; return 0;" % call if ret.strip() == "void" else "return (uint32_t)(uintptr_t)%s;" % call
        wrappers.append("static uint32_t W_ext_%s(ICALL_PARAMS) { %s }" % (x, body))
        dispatch.append((fake, "W_ext_" + x, False))
    ext_wrappers = sorted({w for a, w, ext in dispatch if isinstance(ext, bool) and ext})
    for w in ext_wrappers:
        f.write("uint32_t %s(ICALL_PARAMS);  /* port code */\n" % w)
    f.write("\n".join(wrappers))
    f.write("\n\nconst IcallEntry %s_icall_table[] = {\n" % mod)
    names = {a: w for a, w, ext in dispatch if w}
    for a, w, ext in sorted(dispatch, key=lambda x: x[0]):
        if w is None:
            w = names.get(ext)
            if w is None:
                continue
        if isinstance(ext, bool) and ext and w.startswith("W_"):
            # natives and externals provide their own W_ wrapper in port code
            pass
        flag = 1 if (a in thiscall_addrs or (isinstance(ext, int) and not isinstance(ext, bool) and ext in thiscall_addrs)) else 0
        f.write("  {0x%x, %s, %d},\n" % (a, w, flag))
    f.write("};\nconst unsigned %s_icall_count = sizeof(%s_icall_table) / sizeof(%s_icall_table[0]);\n"
            % (mod, mod, mod))

with open(os.path.join(args.out, mod + "_external_wrappers.txt"), "w") as f:
    for a, w, ext in sorted(dispatch, key=lambda x: x[0]):
        if isinstance(ext, bool) and ext:
            f.write("0x%x %s\n" % (a, w))

with open(os.path.join(args.out, "report.txt"), "w") as f:
    f.write("\n".join(report) + "\n")
print("%s: %d functions selected, %d translated in %d files, %d issues"
      % (mod, len(selected), sum(len(c) for c in chunks), len(chunks), len(report)))
