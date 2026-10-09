// Gives the listed functions an undefined4 return. The input is the
// translator's void_used.txt: functions the decompiler thought were void but
// whose callers use EAX (often they really return a value the decompiler lost,
// e.g. the result of a final call).
// Usage: -postScript SetReturnInt.java <file>   (not -readOnly)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.Undefined4DataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;

public class SetReturnInt extends GhidraScript {
  public void run() throws Exception {
    int n = 0;
    for (String line : Files.readAllLines(Paths.get(getScriptArgs()[0]))) {
      line = line.trim();
      if (line.isEmpty()) continue;
      Function f = getFunctionAt(toAddr(Long.parseLong(line, 16)));
      if (f == null || f.isThunk()) continue;
      try {
        f.setReturn(Undefined4DataType.dataType, new ghidra.program.model.listing.VariableStorage(currentProgram,
            currentProgram.getRegister("EAX")), SourceType.USER_DEFINED);
      } catch (Exception e) {
        println("can't set return of " + f.getName() + ": " + e.getMessage());
        continue;
      }
      n++;
    }
    println("set undefined4 returns on " + n + " functions");
  }
}
