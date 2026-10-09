// Create functions Ghidra's auto-analysis missed: targets of pointer tables (vtables, callback
// arrays) in data sections, and disassembled code that belongs to no function. Repeats until
// nothing new is found, running auto-analysis on the changes between rounds.
// Args: <reportfile> [dry]   ("dry" only reports; run without -readOnly to keep the changes)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class RecoverFunctions extends GhidraScript {
  PrintWriter w;
  boolean dry;
  Memory mem;
  Listing lst;

  boolean inText(Address a) {
    MemoryBlock b = mem.getBlock(a);
    return b != null && b.isExecute() && b.isInitialized();
  }

  // A table target is usable if it is not inside an existing function body or data,
  // and (if already disassembled) it starts an instruction.
  String rejectReason(Address t) {
    if (getFunctionAt(t) != null) return "has-function";
    if (getFunctionContaining(t) != null) return "inside-function";
    Data d = lst.getDefinedDataContaining(t);
    if (d != null && d.isDefined()) return "data";
    Instruction ins = lst.getInstructionContaining(t);
    if (ins != null && !ins.getAddress().equals(t)) return "mid-instruction";
    try {
      int b0 = mem.getByte(t) & 0xff;
      if (b0 == 0xcc || b0 == 0x00) return "padding";
    } catch (MemoryAccessException e) { return "unreadable"; }
    return null;
  }

  // Runs of >= minRun consecutive aligned dwords pointing into .text, in non-executable blocks.
  List<Address[]> findTables(int minRun) throws Exception {
    List<Address[]> tables = new ArrayList<>();
    for (MemoryBlock b : mem.getBlocks()) {
      if (b.isExecute() || !b.isInitialized()) continue;
      Address a = b.getStart();
      long end = b.getEnd().getOffset();
      List<Address> run = new ArrayList<>();
      Address runStart = null;
      for (long off = (a.getOffset() + 3) & ~3L; off + 4 <= end + 1; off += 4) {
        Address s = a.getNewAddress(off);
        Address t = null;
        try { t = toAddr(mem.getInt(s) & 0xffffffffL); } catch (Exception e) { }
        if (t != null && inText(t)) {
          if (run.isEmpty()) runStart = s;
          run.add(t);
        } else {
          if (run.size() >= minRun) { Address[] r = new Address[run.size() + 1]; r[0] = runStart;
            for (int i = 0; i < run.size(); i++) r[i + 1] = run.get(i); tables.add(r); }
          run.clear();
        }
      }
    }
    return tables;
  }

  boolean isReferenced(Address a) {
    return getReferencesTo(a).length > 0;
  }

  int vtablePass(int round) throws Exception {
    int made = 0;
    Map<String,Integer> rej = new TreeMap<>();
    List<Address[]> tables = findTables(2);
    int refd = 0;
    for (Address[] t : tables) {
      // Long runs are kept even if unreferenced (constructor refs are sometimes missing);
      // pairs need a reference to the table start, which rules out most coincidental pairs.
      boolean ref = isReferenced(t[0]);
      if (ref) refd++;
      if (!ref && t.length - 1 < 3) continue;
      for (int i = 1; i < t.length; i++) {
        Address tgt = t[i];
        String why = rejectReason(tgt);
        if (why != null) {
          rej.merge(why, 1, Integer::sum);
          if (!why.equals("has-function")) w.println("  skip " + tgt + "  (" + why + ", table " + t[0] + "[" + (i - 1) + "])");
          continue;
        }
        if (create(tgt, String.format("table %s[%d]%s", t[0], i - 1, ref ? "" : " (unreferenced)"))) made++;
        else rej.merge("create-failed", 1, Integer::sum);
      }
    }
    w.printf("round %d vtable pass: %d pointer tables (%d referenced), %d functions created, skipped %s%n",
        round, tables.size(), refd, made, rej);
    return made;
  }

  // Disassembled instructions with no function: start a function at each block head, i.e. an
  // instruction that is not reached by fall-through from the previous instruction.
  int orphanPass(int round) throws Exception {
    List<Address> heads = new ArrayList<>();
    int orphanIns = 0;
    InstructionIterator it = lst.getInstructions(true);
    while (it.hasNext()) {
      Instruction ins = it.next();
      if (!inText(ins.getAddress()) || getFunctionContaining(ins.getAddress()) != null) continue;
      orphanIns++;
      Instruction prev = lst.getInstructionBefore(ins.getAddress());
      boolean fallsIn = prev != null && prev.getFallThrough() != null && prev.getFallThrough().equals(ins.getAddress())
          && getFunctionContaining(prev.getAddress()) == null;
      if (!fallsIn) heads.add(ins.getAddress());
    }
    int made = 0, branch = 0;
    for (Address h : heads) {
      if (getFunctionContaining(h) != null) continue; // swallowed by an earlier head this pass
      if (isJumpTargetFromFunction(h)) { branch++; w.println("  skip " + h + "  (jump target inside a function)"); continue; }
      if (create(h, "orphan code")) made++;
    }
    w.printf("round %d orphan pass: %d orphan instructions, %d block heads, %d functions created, %d jump targets skipped%n",
        round, orphanIns, heads.size(), made, branch);
    return made;
  }

  // Code a function jumps to (not calls) is part of that function, e.g. a switch case Ghidra
  // didn't follow; making it a separate function would split the real one.
  boolean isJumpTargetFromFunction(Address a) {
    for (Reference r : getReferencesTo(a)) {
      RefType t = r.getReferenceType();
      if (t.isFlow() && !t.isCall() && getFunctionContaining(r.getFromAddress()) != null) return true;
    }
    return false;
  }

  boolean create(Address a, String why) {
    if (dry) { w.println("  would create " + a + "  (" + why + ")"); return true; }
    if (lst.getInstructionAt(a) == null) {
      if (!disassemble(a)) { w.println("  disassemble failed " + a + "  (" + why + ")"); return false; }
    }
    Function f = createFunction(a, null);
    if (f == null) { w.println("  createFunction failed " + a + "  (" + why + ")"); return false; }
    w.println("  created " + f.getName() + "  size=" + f.getBody().getNumAddresses() + "  (" + why + ")");
    return true;
  }

  public void run() throws Exception {
    String[] args = getScriptArgs();
    dry = args.length > 1 && args[1].equals("dry");
    mem = currentProgram.getMemory();
    lst = currentProgram.getListing();
    FunctionManager fm = currentProgram.getFunctionManager();
    try (PrintWriter pw = new PrintWriter(new FileWriter(args[0]))) {
      w = pw;
      int before = fm.getFunctionCount();
      w.println("functions before: " + before + (dry ? "  (dry run)" : ""));
      for (int round = 1; round <= 10; round++) {
        int made = vtablePass(round);
        if (!dry) analyzeChanges(currentProgram);
        made += orphanPass(round);
        if (!dry) analyzeChanges(currentProgram);
        if (made == 0 || dry) break;
      }
      w.println("functions after: " + fm.getFunctionCount() + "  (+" + (fm.getFunctionCount() - before) + ")");
    }
  }
}
