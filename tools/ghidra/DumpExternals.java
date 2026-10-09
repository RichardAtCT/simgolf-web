// Prints the prototype of every external (imported) function, one per line:
//   <library> <name> <C prototype>
// Usage: -postScript DumpExternals.java <outfile>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpExternals extends GhidraScript {
  public void run() throws Exception {
    try (PrintWriter w = new PrintWriter(new FileWriter(getScriptArgs()[0]))) {
      for (Function f : currentProgram.getFunctionManager().getExternalFunctions()) {
        String lib = f.getParentNamespace().getName();
        w.println(lib + "\t" + f.getName() + "\t" + f.getCallingConventionName() + "\t"
            + f.getPrototypeString(true, false));
      }
    }
  }
}
