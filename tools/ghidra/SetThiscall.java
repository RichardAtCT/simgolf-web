// Re-derives calling conventions for FUN_* functions: __thiscall when the
// function reads ECX before writing it (MSVC's `this`), otherwise "unknown",
// and unlocks their signatures so CommitSignatures re-infers them. jgld.dll's
// analysis had marked its methods __stdcall, which hides `this` from the
// decompiler.
// With "force" (after CommitSignatures): functions that read ECX first are
// rewritten as __thiscall with their committed parameters plus the `this`
// auto-parameter, locked, because the decompiler otherwise still drops it.
// "reset" sets every FUN_* to "unknown" and unlocks it (before CommitSignatures).
// Usage: -postScript SetThiscall.java [lo-hi] [force|reset]   (not -readOnly)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;

public class SetThiscall extends GhidraScript {
  // First use of ECX along the fall-through path from the entry: true if read.
  boolean readsEcxFirst(Function f) {
    Instruction ins = getInstructionAt(f.getEntryPoint());
    for (int i = 0; i < 40 && ins != null; i++) {
      boolean reads = false, writes = false;
      for (Object o : ins.getInputObjects()) if (o instanceof Register && isEcx((Register) o)) reads = true;
      for (Object o : ins.getResultObjects()) if (o instanceof Register && isEcx((Register) o)) writes = true;
      String m = ins.getMnemonicString();
      // xor ecx,ecx / sub ecx,ecx are writes, not reads
      if ((m.equals("XOR") || m.equals("SUB")) && ins.getNumOperands() == 2
          && ins.getDefaultOperandRepresentation(0).equals(ins.getDefaultOperandRepresentation(1))) reads = false;
      if (reads) return true;
      if (writes) return false;
      if (ins.getFlowType().isCall() || ins.getFlowType().isJump() || ins.getFlowType().isTerminal()) return false;
      ins = ins.getNext();
    }
    return false;
  }

  boolean isEcx(Register r) {
    String n = r.getName();
    return n.equals("ECX") || n.equals("CX") || n.equals("CL");
  }

  public void run() throws Exception {
    String[] args = getScriptArgs();
    long lo = 0, hi = Long.MAX_VALUE;
    if (args.length > 0) {
      String[] r = args[0].split("-");
      lo = Long.parseLong(r[0], 16);
      hi = Long.parseLong(r[1], 16);
    }
    boolean force = args.length > 1 && args[1].equals("force");
    boolean reset = args.length > 1 && args[1].equals("reset");
    int th = 0, other = 0;
    for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
      long ea = f.getEntryPoint().getOffset();
      if (!f.getName().startsWith("FUN_") || f.isThunk() || ea < lo || ea >= hi) continue;
      boolean t = readsEcxFirst(f);
      if (reset) t = false;   // everything "unknown" so CommitSignatures infers stack params
      if (force) {
        if (!t) continue;
        java.util.List<ghidra.program.model.listing.Variable> ps = new java.util.ArrayList<>();
        for (Parameter p : f.getParameters()) {
          if (p.isAutoParameter() || p.getName().equals("this")) continue;
          if (p.isRegisterVariable() && isEcx(p.getRegister())) continue;
          ps.add(new ParameterImpl(p.getName(), p.getDataType(), currentProgram));
        }
        // a float10 return would become a hidden return pointer under the
        // thiscall model; MSVC returns it in ST0, which double maps to
        Parameter ret = f.getReturn();
        if (ret.getDataType() instanceof ghidra.program.model.data.Float10DataType)
          ret = new ReturnParameterImpl(ghidra.program.model.data.DoubleDataType.dataType, currentProgram);
        ps.removeIf(v -> v.getName().equals("__return_storage_ptr__"));
        f.updateFunction("__thiscall", ret, ps,
            Function.FunctionUpdateType.DYNAMIC_STORAGE_FORMAL_PARAMS, true, SourceType.USER_DEFINED);
        th++;
        continue;
      }
      f.setCallingConvention(t ? "__thiscall" : "unknown");
      f.setSignatureSource(SourceType.DEFAULT);
      if (t) th++; else other++;
    }
    println("thiscall: " + th + ", other: " + other);
  }
}
