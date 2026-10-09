// Writes, for every vtable slot offset, the stack purges (ret N) of the
// methods found at that offset in the program's vtables:
//   <offset hex> <purge> [<purge> ...]
// Vtable starts are the immediates constructors store into objects
// (mov dword ptr [reg(+0)], <address in a pointer table>); each vtable runs
// until the next start or the end of its table. OverrideVcalls.java uses
// this to count a virtual call's arguments from the callee side, because the
// caller side is ambiguous when a push belongs to a later call.
// Usage: -postScript VtablePurges.java <outfile>   (read-only is fine)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.scalar.Scalar;
import java.io.*;
import java.util.*;

public class VtablePurges extends GhidraScript {
  public void run() throws Exception {
    Memory mem = currentProgram.getMemory();
    MemoryBlock text = mem.getBlock(".text");
    Listing listing = currentProgram.getListing();
    FunctionManager fm = currentProgram.getFunctionManager();
    // candidate vtable starts: immediates stored by MOV [r], imm
    TreeSet<Long> starts = new TreeSet<>();
    InstructionIterator it = listing.getInstructions(text.getStart(), true);
    while (it.hasNext()) {
      Instruction ins = it.next();
      if (!text.contains(ins.getAddress())) break;
      if (!ins.getMnemonicString().equalsIgnoreCase("MOV")) continue;
      String d = ins.getDefaultOperandRepresentation(0);
      if (!d.startsWith("dword ptr [")) continue;
      for (Object o : ins.getOpObjects(1)) {
        if (!(o instanceof Scalar)) continue;
        long v = ((Scalar) o).getUnsignedValue();
        Address a = toAddr(v);
        MemoryBlock b = mem.getBlock(a);
        if (b == null || b.isExecute()) continue;
        if (isCodePtr(a)) starts.add(v);
      }
    }
    Map<Long, TreeSet<Integer>> purges = new TreeMap<>();
    for (long s : starts) {
      Long next = starts.higher(s);
      for (int i = 0; i < 512; i++) {
        long at = s + i * 4L;
        if (next != null && at >= next) break;
        Address a = toAddr(at);
        if (!isCodePtr(a)) break;
        Function f = fm.getFunctionAt(toAddr(mem.getInt(a) & 0xffffffffL));
        if (f == null) continue;
        if (f.isThunk()) f = f.getThunkedFunction(true);
        int p = f.getStackPurgeSize();
        if (p < 0 || p > 0x100) continue;   // unknown
        purges.computeIfAbsent((long) i * 4, k -> new TreeSet<>()).add(p);
      }
    }
    try (PrintWriter w = new PrintWriter(new FileWriter(getScriptArgs()[0]))) {
      for (var e : purges.entrySet()) {
        StringBuilder sb = new StringBuilder(Long.toHexString(e.getKey()));
        for (int p : e.getValue()) sb.append(' ').append(p);
        w.println(sb);
      }
    }
    println("vtables: " + starts.size() + ", slot offsets: " + purges.size());
  }

  boolean isCodePtr(Address a) {
    try {
      long v = currentProgram.getMemory().getInt(a) & 0xffffffffL;
      MemoryBlock b = currentProgram.getMemory().getBlock(toAddr(v));
      return b != null && b.isExecute() && currentProgram.getListing().getInstructionAt(toAddr(v)) != null;
    } catch (Exception e) {
      return false;
    }
  }
}
