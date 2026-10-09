// Gives MSVC virtual calls a __thiscall call-site signature.
//
// For `call [reg+off]` Ghidra knows neither the convention nor the stack
// purge, so it drops `this` (ECX) and assumes the caller pops the arguments.
// MSVC methods pop their own (ret N), so every stack access after such a call
// in the same function is mis-mapped (args show up as unaff_ESI,
// unaff_retaddr, ...). For each indirect call whose vtable register was loaded
// through ECX (mov r,[ecx] or mov ecx,s / mov r,[s]), this script writes a
// call-site override: __thiscall with the number of stack arguments the
// decompiler currently sees. It repeats per function until stable, because
// fixing one call changes the arguments seen at the next.
//
// Usage: -postScript OverrideVcalls.java [rounds] [timeout] [lo-hi] [purges=f1,f2]   (not -readOnly)
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class OverrideVcalls extends GhidraScript {
  Map<Address, Integer> done = new HashMap<>();
  Map<Long, java.util.TreeSet<Integer>> slotPurges = new HashMap<>();
  // extpurges=<file>@<global>: objects loaded from that global belong to the
  // other module (jgld calls golf's app object), so use its vtables' purges
  Map<Long, java.util.TreeSet<Integer>> extPurges = new HashMap<>();
  String extGlobal = null;
  long debugLo = 0, debugHi = 0;

  // Argument count from the callee side, or -1 if unknown. The methods at
  // this slot offset (any class) pop one of a few sizes; the caller-side guess
  // can only be too high (pushes for a later call look like ours), so take
  // the largest candidate that doesn't exceed it.
  int argsFromVtables(Instruction call, int guess) {
    if (slotPurges.isEmpty()) return -1;
    String op = call.getDefaultOperandRepresentation(0);
    java.util.regex.Matcher m = java.util.regex.Pattern.compile("\\[[A-Z]{3}(?: ?\\+ ?0x([0-9a-f]+))?\\]").matcher(op);
    if (!m.find()) return -1;
    long off = m.group(1) == null ? 0 : Long.parseLong(m.group(1), 16);
    java.util.TreeSet<Integer> set = slotPurges.get(off);
    if (extGlobal != null) {
      Instruction p = call.getPrevious();
      for (int i = 0; i < 8 && p != null; i++, p = p.getPrevious()) {
        if (p.getFlowType().isCall() || p.getFlowType().isJump()) break;
        if (dst(p).equals("ECX")) {
          if (src(p).equalsIgnoreCase("dword ptr [" + extGlobal + "]")) set = extPurges.get(off);
          break;
        }
      }
    }
    if (set == null || set.isEmpty()) return -1;
    if (set.size() == 1) return set.first() / 4;
    Integer best = set.floor(guess * 4);
    return best == null ? set.first() / 4 : best / 4;
  }

  // Is the indirect call at `call` a C++ virtual call with this in ECX?
  // Within the block before the call: the vtable register was loaded as
  // mov vreg,[B] and ECX holds the same object as B (B is ECX, or both were
  // loaded from the same operand, or ECX was copied from B).
  static String dst(Instruction i) { return i.getDefaultOperandRepresentation(0); }
  static String src(Instruction i) { return i.getDefaultOperandRepresentation(1); }

  static boolean calleeSaved(String r) {
    return r.equals("EBX") || r.equals("ESI") || r.equals("EDI") || r.equals("EBP");
  }

  boolean isVcall(Instruction call) {
    if (!call.getMnemonicString().equalsIgnoreCase("CALL")) return false;
    String op = call.getDefaultOperandRepresentation(0);
    java.util.regex.Matcher m = java.util.regex.Pattern.compile("\\[([A-Z]{3})(?: ?\\+ ?0x[0-9a-f]+)?\\]").matcher(op);
    if (!m.find()) return false;
    String vreg = m.group(1);
    // Walk back in straight-line code (stop at jumps/labels with other
    // predecessors would be stricter; MSVC sets these up close by).
    String ecxDef = null, base = null, baseDef = null;
    boolean crossedCall = false;
    Instruction ins = call.getPrevious();
    for (int i = 0; i < 48 && ins != null; i++, ins = ins.getPrevious()) {
      if (ins.getFlowType().isJump() || ins.getFlowType().isTerminal()) break;
      if (ins.getFlowType().isCall()) {
        crossedCall = true;
        if (ecxDef == null && base == null) return false;   // ECX would be stale
        continue;
      }
      String mn = ins.getMnemonicString();
      if (!mn.equalsIgnoreCase("MOV") && !mn.equalsIgnoreCase("LEA")) continue;
      String d = dst(ins), sr = src(ins);
      if (ecxDef == null && d.equals("ECX")) {
        if (crossedCall) return false;
        ecxDef = mn + " " + sr;
        continue;
      }
      if (base == null && d.equals(vreg) && mn.equalsIgnoreCase("MOV")) {
        if (crossedCall && !calleeSaved(vreg)) return false;
        java.util.regex.Matcher mb = java.util.regex.Pattern.compile("^dword ptr \\[([A-Z]{3})\\]$").matcher(sr);
        if (!mb.find()) return false;
        base = mb.group(1);
        if (base.equals("ECX") && ecxDef == null) return true;   // mov r,[ecx]; call [r+n]
        continue;
      }
      if (base != null && baseDef == null && d.equals(base)) {
        baseDef = mn + " " + sr;
        break;
      }
    }
    if (base == null || ecxDef == null) return false;
    if (ecxDef.equals("MOV " + base)) return true;          // mov ecx, base
    return baseDef != null && baseDef.equals(ecxDef);       // both loaded from the same operand
  }

  void readPurges(String file, Map<Long, java.util.TreeSet<Integer>> into) throws Exception {
    for (String line : java.nio.file.Files.readAllLines(java.nio.file.Paths.get(file))) {
      String[] p = line.trim().split("\\s+");
      if (p.length < 2) continue;
      java.util.TreeSet<Integer> set = into.computeIfAbsent(Long.parseLong(p[0], 16), k -> new java.util.TreeSet<>());
      for (int i = 1; i < p.length; i++) set.add(Integer.parseInt(p[i]));
    }
  }

  public void run() throws Exception {
    String[] args = getScriptArgs();
    // purges=<file>[,<file>]: VtablePurges.java output; a slot offset whose
    // methods all pop the same number of bytes fixes the argument count
    for (String a : args) {
      if (!a.startsWith("extpurges=")) continue;
      String[] fg = a.substring(10).split("@");
      extGlobal = fg[1].toLowerCase();
      if (!extGlobal.startsWith("0x")) extGlobal = "0x" + extGlobal;
      readPurges(fg[0], extPurges);
    }
    for (String a : args) {
      if (!a.startsWith("purges=")) continue;
      for (String file : a.substring(7).split(",")) {
        for (String line : java.nio.file.Files.readAllLines(java.nio.file.Paths.get(file))) {
          String[] p = line.trim().split("\\s+");
          if (p.length < 2) continue;
          long off = Long.parseLong(p[0], 16);
          java.util.TreeSet<Integer> set = slotPurges.computeIfAbsent(off, k -> new java.util.TreeSet<>());
          for (int i = 1; i < p.length; i++) set.add(Integer.parseInt(p[i]));
        }
      }
    }
    for (String a : args)
      if (a.startsWith("debug=")) {
        String[] r = a.substring(6).split("-");
        debugLo = Long.parseLong(r[0], 16);
        debugHi = Long.parseLong(r[1], 16);
      }
    args = java.util.Arrays.stream(args).filter(a -> !a.startsWith("purges=") && !a.startsWith("extpurges=") && !a.startsWith("debug=")).toArray(String[]::new);
    int rounds = args.length > 0 ? Integer.parseInt(args[0]) : 3;
    int timeout = args.length > 1 ? Integer.parseInt(args[1]) : 30;
    long lo = 0, hi = Long.MAX_VALUE;
    if (args.length > 2) {
      String[] r = args[2].split("-");
      lo = Long.parseLong(r[0], 16);
      hi = Long.parseLong(r[1], 16);
    }
    DecompInterface di = new DecompInterface();
    di.openProgram(currentProgram);
    DataTypeManager dtm = currentProgram.getDataTypeManager();
    int total = 0, funcs = 0;
    for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
      if (monitor.isCancelled()) break;
      long ea = f.getEntryPoint().getOffset();
      if (f.isThunk() || f.isExternal() || ea < lo || ea >= hi) continue;
      boolean any = false;
      for (int round = 0; round < rounds; round++) {
        DecompileResults res = di.decompileFunction(f, timeout, monitor);
        if (!res.decompileCompleted()) break;
        int changed = 0;
        Iterator<PcodeOpAST> ops = res.getHighFunction().getPcodeOps();
        while (ops.hasNext()) {
          PcodeOpAST op = ops.next();
          if (op.getOpcode() != PcodeOp.CALLIND) continue;
          Address at = op.getSeqnum().getTarget();
          Instruction ins = getInstructionAt(at);
          if (ins == null || !isVcall(ins)) continue;
          // inputs: target, then args; if an override exists the first arg is this
          int nargs = op.getNumInputs() - 1;
          Integer prev = done.get(at);
          if (prev == null) {
            // an override from an earlier run of this script
            for (Symbol sym : currentProgram.getSymbolTable().getSymbols(at)) {
              if (!sym.getName().startsWith("prt")) continue;
              DataTypeSymbol ds = HighFunctionDBUtil.readOverride(sym);
              if (ds != null && ds.getDataType() instanceof FunctionSignature)
                prev = ((FunctionSignature) ds.getDataType()).getArguments().length - 1;
            }
            if (prev != null) done.put(at, prev);
          }
          int stackArgs = prev == null ? nargs : nargs - 1;
          int fromCallee = argsFromVtables(ins, stackArgs);
          if (debugLo <= at.getOffset() && at.getOffset() < debugHi)
            println("vcall at " + at + ": decompiler sees " + nargs + " inputs, prev " + prev + ", from vtables " + fromCallee);
          if (fromCallee >= 0) stackArgs = fromCallee;
          if (prev != null && prev == stackArgs) continue;
          FunctionDefinitionDataType sig = new FunctionDefinitionDataType("vcall_" + at, dtm);
          ParameterDefinition[] ps = new ParameterDefinition[stackArgs + 1];
          ps[0] = new ParameterDefinitionImpl("this", new PointerDataType(VoidDataType.dataType), null);
          for (int i = 0; i < stackArgs; i++)
            ps[i + 1] = new ParameterDefinitionImpl("a" + i, Undefined4DataType.dataType, null);
          sig.setArguments(ps);
          sig.setReturnType(Undefined4DataType.dataType);
          sig.setCallingConvention("__thiscall");
          int tx = currentProgram.startTransaction("vcall override");
          try {
            HighFunctionDBUtil.writeOverride(f, at, sig);
            done.put(at, stackArgs);
            changed++;
          } catch (Exception e) {
            println("override failed at " + at + ": " + e.getMessage());
          } finally {
            currentProgram.endTransaction(tx, true);
          }
        }
        if (changed == 0) break;
        any = true;
        total += changed;
      }
      if (any) funcs++;
    }
    println("vcall overrides: " + done.size() + " call sites in " + funcs + " functions (" + total + " writes)");
  }
}
