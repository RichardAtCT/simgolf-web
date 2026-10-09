// Decompile a function (following thunks), or every slot of a vtable.
// Args: <outfile> fn <name|address>...   or   <outfile> vt <address> <maxSlots>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpVtable extends GhidraScript {
  DecompInterface di;
  Function resolve(Address a) {
    Function f = getFunctionAt(a);
    if (f == null) { disassemble(a); f = createFunction(a, null); }
    while (f != null && f.isThunk()) f = f.getThunkedFunction(false);
    return f;
  }
  String decomp(Function f) throws Exception {
    DecompileResults r = di.decompileFunction(f, 60, monitor);
    return r.decompileCompleted() ? r.getDecompiledFunction().getC() : "/* decompile failed */\n";
  }
  public void run() throws Exception {
    String[] a = getScriptArgs();
    di = new DecompInterface(); di.openProgram(currentProgram);
    try (PrintWriter w = new PrintWriter(new FileWriter(a[0]))) {
      if (a[1].equals("fn")) {
        for (int k = 2; k < a.length; k++) {
          Address addr = null;
          for (Function f : currentProgram.getFunctionManager().getFunctions(true)) if (f.getName().equals(a[k])) addr = f.getEntryPoint();
          if (addr == null) addr = toAddr(a[k]);
          Function f = resolve(addr);
          w.println("// ==== " + f.getName() + " @ " + f.getEntryPoint() + "\n" + decomp(f));
        }
      } else {
        Address base = toAddr(a[2]); int n = Integer.parseInt(a[3]);
        for (int i = 0; i < n; i++) {
          Address slot = base.add(i * 4);
          Address target = toAddr(getInt(slot) & 0xffffffffL);
          var blk = currentProgram.getMemory().getBlock(target);
          if (blk == null || !blk.isExecute()) { w.printf("// ==== end of vtable at slot %d%n", i); break; }
          Function f = resolve(target);
          w.printf("// ==== slot %d (+0x%03x) -> %s @ %s%n", i, i * 4, f == null ? "?" : f.getName(), f == null ? target : f.getEntryPoint());
          if (f != null) w.println(decomp(f));
        }
      }
    }
  }
}
