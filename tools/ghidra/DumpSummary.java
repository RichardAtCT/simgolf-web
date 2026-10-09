import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.util.*;

public class DumpSummary extends GhidraScript {
  public void run() throws Exception {
    String out = getScriptArgs()[0];
    try (PrintWriter w = new PrintWriter(new FileWriter(out + "/summary.txt"))) {
      w.println("Program: " + currentProgram.getName() + "  lang=" + currentProgram.getLanguageID() + "  compiler=" + currentProgram.getCompiler());
      w.println("Image base: " + currentProgram.getImageBase());
      for (MemoryBlock b : currentProgram.getMemory().getBlocks())
        w.printf("Section %-8s %s-%s size=%d %s%s%s%n", b.getName(), b.getStart(), b.getEnd(), b.getSize(), b.isRead()?"r":"-", b.isWrite()?"w":"-", b.isExecute()?"x":"-");
      FunctionManager fm = currentProgram.getFunctionManager();
      w.println("Functions: " + fm.getFunctionCount());
      w.println("\n== Imports ==");
      SymbolTable st = currentProgram.getSymbolTable();
      Map<String,List<String>> imps = new TreeMap<>();
      for (Symbol s : st.getExternalSymbols()) {
        String lib = s.getParentNamespace().getName();
        imps.computeIfAbsent(lib, k -> new ArrayList<>()).add(s.getName());
      }
      for (var e : imps.entrySet()) { w.println(e.getKey() + " (" + e.getValue().size() + "): " + String.join(", ", e.getValue())); }
      w.println("\n== Named / entry functions ==");
      for (Function f : fm.getFunctions(true)) if (!f.getName().startsWith("FUN_")) w.println(f.getEntryPoint() + " " + f.getName());
    }
    try (PrintWriter w = new PrintWriter(new FileWriter(out + "/strings.txt"))) {
      for (Data d : currentProgram.getListing().getDefinedData(true)) { if (!d.hasStringValue()) continue;
        Object v = d.getValue(); if (v != null && v.toString().length() >= 5) w.println(d.getAddress() + "  " + v.toString().replace("\n","\\n"));
      }
    }
    DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
    try (PrintWriter w = new PrintWriter(new FileWriter(out + "/decomp_key.c"))) {
      FunctionManager fm = currentProgram.getFunctionManager();
      for (Function f : fm.getFunctions(true)) {
        String n = f.getName();
        if (n.equals("entry") || n.toLowerCase().contains("winmain")) {
          DecompileResults r = di.decompileFunction(f, 60, monitor);
          if (r.decompileCompleted()) w.println("// ==== " + n + " @ " + f.getEntryPoint() + "\n" + r.getDecompiledFunction().getC());
        }
      }
    }
    // Full decompile of all functions
    try (PrintWriter w = new PrintWriter(new FileWriter(out + "/decomp_all.c"))) {
      for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
        if (f.isExternal() || f.isThunk()) continue;
        DecompileResults r = di.decompileFunction(f, 30, monitor);
        if (r.decompileCompleted()) w.println("// ==== " + f.getName() + " @ " + f.getEntryPoint() + "\n" + r.getDecompiledFunction().getC());
      }
    }
  }
}
