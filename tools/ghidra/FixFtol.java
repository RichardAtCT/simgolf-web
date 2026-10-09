// Gives MSVC's __ftol an explicit ST0 parameter (and an int return in EAX) and
// saves it, so the decompiler shows the value being converted. Without this,
// __ftol() looks argument-less and CommitSignatures drops parameters whose only
// use is feeding it. ExportPort.java does the same in its read-only session.
// Usage: -postScript FixFtol.java   (not -readOnly)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;

public class FixFtol extends GhidraScript {
  public void run() throws Exception {
    for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
      if (!f.getName().equals("__ftol")) continue;
      Register st0 = currentProgram.getRegister("ST0");
      Register eax = currentProgram.getRegister("EAX");
      ParameterImpl p = new ParameterImpl("x", new Float10DataType(),
          new VariableStorage(currentProgram, st0), currentProgram);
      f.updateFunction("__cdecl", new ReturnParameterImpl(IntegerDataType.dataType,
          new VariableStorage(currentProgram, eax), currentProgram),
          java.util.List.of(p), Function.FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.USER_DEFINED);
      println("__ftol at " + f.getEntryPoint() + " now takes ST0");
    }
  }
}
