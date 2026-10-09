// Marks functions as non-returning and ends the flow at every call to them.
// MSVC drops the stack cleanup after calls to exit() & co., so if Ghidra lets
// the flow fall through, the stack depth at the next merge point is off by the
// pushed arguments and later stack parameters show up as unaff_retaddr etc.
// Usage: -postScript MarkNoReturn.java <hexaddr>...   (not -readOnly)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;

public class MarkNoReturn extends GhidraScript {
  public void run() throws Exception {
    for (String a : getScriptArgs()) {
      Address t = toAddr(Long.parseLong(a, 16));
      Function f = getFunctionAt(t);
      if (f == null) { println("no function at " + a); continue; }
      f.setNoReturn(true);
      int n = 0;
      for (Reference r : getReferencesTo(t)) {
        if (!r.getReferenceType().isCall()) continue;
        Instruction ins = getInstructionAt(r.getFromAddress());
        if (ins == null) continue;
        ins.setFlowOverride(FlowOverride.CALL_RETURN);
        n++;
      }
      println(f.getName() + ": no return, " + n + " call sites end the flow");
    }
  }
}
