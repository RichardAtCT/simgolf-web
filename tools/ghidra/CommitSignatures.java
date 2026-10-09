// Commits the decompiler's parameter and return types to every function, so
// call sites and definitions agree when the program is decompiled again (the
// same thing Ghidra's "Decompiler Parameter ID" analyzer does). Functions are
// processed callees first, in `rounds` passes.
// Usage: -postScript CommitSignatures.java [rounds] [timeout] [skiplo-skiphi] [unlock]   (not -readOnly)
// Functions with user-defined signatures or varargs are left alone.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class CommitSignatures extends GhidraScript {
  List<Function> order = new ArrayList<>();
  Set<Function> seen = new HashSet<>();

  void visit(Function f) {
    Deque<Object[]> stack = new ArrayDeque<>();
    stack.push(new Object[] {f, null});
    while (!stack.isEmpty()) {
      Object[] top = stack.peek();
      Function cur = (Function) top[0];
      @SuppressWarnings("unchecked")
      Iterator<Function> it = (Iterator<Function>) top[1];
      if (it == null) {
        if (!seen.add(cur)) { stack.pop(); continue; }
        it = cur.getCalledFunctions(monitor).iterator();
        top[1] = it;
      }
      if (it.hasNext()) {
        Function c = it.next();
        if (!seen.contains(c)) stack.push(new Object[] {c, null});
      } else {
        stack.pop();
        order.add(cur);
      }
    }
  }

  public void run() throws Exception {
    String[] args = getScriptArgs();
    int rounds = args.length > 0 ? Integer.parseInt(args[0]) : 2;
    int timeout = args.length > 1 ? Integer.parseInt(args[1]) : 20;
    // optional third argument lo-hi: functions not to touch (the CRT)
    long skipLo = 0, skipHi = 0;
    if (args.length > 2) {
      String[] r = args[2].split("-");
      skipLo = Long.parseLong(r[0], 16);
      skipHi = Long.parseLong(r[1], 16);
    }
    // "unlock" (4th argument): FUN_* functions with a locked signature (set by
    // earlier scripts) get it re-inferred like any other.
    if (args.length > 3 && args[3].equals("unlock")) {
      int n = 0;
      int tx = currentProgram.startTransaction("unlock");
      for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
        if (f.getName().startsWith("FUN_") && !f.isThunk()) {
          // also forget the calling convention: a stale __stdcall stops the
          // decompiler from seeing `this` in ECX
          try { f.setCallingConvention("unknown"); } catch (Exception e) {}
          f.setSignatureSource(SourceType.DEFAULT);
          n++;
        }
      }
      currentProgram.endTransaction(tx, true);
      println("unlocked " + n + " signatures");
    }
    for (Function f : currentProgram.getFunctionManager().getFunctions(true)) visit(f);
    DecompInterface di = new DecompInterface();
    di.openProgram(currentProgram);
    for (int round = 0; round < rounds; round++) {
      int ok = 0, failed = 0;
      for (Function f : order) {
        if (monitor.isCancelled()) return;
        if (f.isExternal() || f.isThunk()) continue;
        if (f.getSignatureSource() == SourceType.USER_DEFINED || f.getSignatureSource() == SourceType.IMPORTED) continue;
        if (f.hasVarArgs()) continue;
        long ea = f.getEntryPoint().getOffset();
        if (ea >= skipLo && ea < skipHi) continue;
        DecompileResults r = di.decompileFunction(f, timeout, monitor);
        if (!r.decompileCompleted()) { failed++; continue; }
        HighFunction hf = r.getHighFunction();
        int tx = currentProgram.startTransaction("commit signature");
        try {
          HighFunctionDBUtil.commitParamsToDatabase(hf, true,
              HighFunctionDBUtil.ReturnCommitOption.COMMIT, SourceType.ANALYSIS);
          ok++;
        } catch (Exception e) {
          failed++;
        } finally {
          currentProgram.endTransaction(tx, true);
        }
      }
      println("round " + round + ": committed " + ok + ", failed " + failed);
    }
  }
}
