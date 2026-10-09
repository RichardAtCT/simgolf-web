// Prints a function's body ranges and the instruction before each gap.
// Usage: -postScript BodyRanges.java <hexaddr>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;

public class BodyRanges extends GhidraScript {
  public void run() throws Exception {
    Function f = getFunctionAt(toAddr(Long.parseLong(getScriptArgs()[0], 16)));
    for (AddressRange r : f.getBody()) {
      Instruction last = getInstructionContaining(r.getMaxAddress());
      println(r.getMinAddress() + "-" + r.getMaxAddress() + "  ends with " + (last == null ? "?" : last.getAddress() + " " + last + " flow=" + last.getFlowType()));
    }
  }
}
