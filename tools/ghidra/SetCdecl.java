// Functions Ghidra left as __stdcall (or unknown) with an unknown stack purge (it never
// found their RET, usually because a switch table was resolved later by
// FixJumpTables) are really __cdecl when every RET in their body is a plain
// `ret`. As __stdcall, callers assume the callee pops its arguments, so every
// stack access after the call is shifted (e.g. FUN_00467a00 grew 6 phantom
// parameters from its call to FUN_00469b00).
// Usage: -postScript SetCdecl.java   (not -readOnly; before CommitSignatures)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;

public class SetCdecl extends GhidraScript {
  public void run() throws Exception {
    int n = 0;
    for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
      if (f.isThunk() || f.isExternal()) continue;
      String cc = f.getCallingConventionName();
      if (!"__stdcall".equals(cc) && !"unknown".equals(cc)) continue;
      if (f.getStackPurgeSize() != Function.UNKNOWN_STACK_DEPTH_CHANGE) continue;
      int plain = 0, imm = 0;
      for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
        if (!ins.getMnemonicString().equalsIgnoreCase("RET")) continue;
        if (ins.getNumOperands() > 0 && ins.getOpObjects(0).length > 0 && ins.getOpObjects(0)[0] instanceof Scalar
            && ((Scalar) ins.getOpObjects(0)[0]).getUnsignedValue() != 0) imm++;
        else plain++;
      }
      if (plain == 0 || imm != 0) continue;
      f.setCallingConvention("__cdecl");
      println("cdecl: " + f.getName());
      n++;
    }
    println("set __cdecl on " + n + " functions");
  }
}
