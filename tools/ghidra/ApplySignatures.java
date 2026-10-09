// Sets known signatures (all-int parameters, __cdecl) on functions before
// CommitSignatures runs, so call sites to the CRT and variadic functions
// decompile with the right arguments. Input lines:
//   <hexaddr> <name> <nparams | types like "dd"> <varargs 0|1> <ret void|int|double>
// Usage: -postScript ApplySignatures.java <file>   (not -readOnly)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class ApplySignatures extends GhidraScript {
  public void run() throws Exception {
    int n = 0;
    for (String line : Files.readAllLines(Paths.get(getScriptArgs()[0]))) {
      line = line.trim();
      if (line.isEmpty() || line.startsWith("#")) continue;
      String[] p = line.split("\\s+");
      Function f = getFunctionAt(toAddr(Long.parseLong(p[0], 16)));
      if (f == null) { println("no function at " + p[0]); continue; }
      // nparams: a count of int parameters, or a string of types: i int, d double
      String spec = p[2].matches("\\d+") ? "i".repeat(Integer.parseInt(p[2])) : p[2];
      List<Variable> params = new ArrayList<>();
      for (int i = 0; i < spec.length(); i++)
        params.add(new ParameterImpl("a" + i, spec.charAt(i) == 'd' ? DoubleDataType.dataType : IntegerDataType.dataType, currentProgram));
      DataType ret = p[4].equals("void") ? VoidDataType.dataType
          : p[4].equals("double") ? DoubleDataType.dataType : IntegerDataType.dataType;
      f.updateFunction("__cdecl", new ReturnParameterImpl(ret, currentProgram), params,
          Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.USER_DEFINED);
      f.setVarArgs(p[3].equals("1"));
      if (!p[1].equals("-")) f.setName(p[1], SourceType.USER_DEFINED);
      n++;
    }
    println("applied " + n + " signatures");
  }
}
