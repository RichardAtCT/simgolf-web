// List references to the given addresses. Args: <outfile> <addr>...
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class DumpRefs extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs();
    try (PrintWriter w = new PrintWriter(new FileWriter(a[0]))) {
      for (int i = 1; i < a.length; i++) {
        Address t = toAddr(a[i]);
        w.println("== refs to " + t);
        for (Reference r : getReferencesTo(t)) {
          Function f = getFunctionContaining(r.getFromAddress());
          Instruction ins = getInstructionAt(r.getFromAddress());
          w.println("  " + r.getFromAddress() + " " + r.getReferenceType() + " in " + (f == null ? "?" : f.getName()) + "  " + (ins == null ? "" : ins.toString()));
        }
      }
    }
  }
}
