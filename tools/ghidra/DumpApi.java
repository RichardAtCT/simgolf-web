import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class DumpApi extends GhidraScript {
  public void run() throws Exception {
    String out = getScriptArgs()[0] + "/" + currentProgram.getName() + ".api.txt";
    try (PrintWriter w = new PrintWriter(new FileWriter(out))) {
      FunctionManager fm = currentProgram.getFunctionManager();
      w.println("# " + currentProgram.getName() + " functions=" + fm.getFunctionCount());
      SymbolTable st = currentProgram.getSymbolTable();
      Map<String,List<String>> imps = new TreeMap<>();
      for (Symbol s : st.getExternalSymbols())
        imps.computeIfAbsent(s.getParentNamespace().getName(), k -> new ArrayList<>()).add(s.getName());
      w.println("## Imports");
      for (var e : imps.entrySet()) { Collections.sort(e.getValue()); w.println(e.getKey() + " (" + e.getValue().size() + "): " + String.join(", ", e.getValue())); }
      w.println("## Exports");
      ghidra.program.model.address.AddressIterator it = st.getExternalEntryPointIterator();
      List<String> ex = new ArrayList<>();
      while (it.hasNext()) { Symbol s = st.getPrimarySymbol(it.next()); if (s != null) ex.add(s.getName()); }
      Collections.sort(ex); for (String e : ex) w.println(e);
    }
  }
}
