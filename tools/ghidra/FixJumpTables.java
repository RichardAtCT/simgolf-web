// Repairs switch statements whose jump tables Ghidra didn't recover.
//
// golf.exe's main loop (0x0040f5c0, ~70 KB of code) dispatches through ~20
// `jmp [4*reg + table]` tables. Ghidra missed them, so the cases were left
// outside any function and RecoverFunctions.java later turned them into bogus
// "functions" that can't be decompiled. For every such indirect jump this
// script reads the table (entries that point into .text), deletes functions
// that start at a case address, writes a jump-table override on the function
// that contains the jump, and regrows that function's body. Repeats until
// nothing changes.
//
// Usage: -postScript FixJumpTables.java <log> [owner:end ...]   (not -readOnly)
//   owner:end (hex) declares one function spanning [owner, end): every other
//   function starting inside the span is deleted first. golf.exe needs
//   40f5c0:421618 (the main loop; its jump tables start at 0x421618).
//   owner alone (no :end) creates the function if needed and ends the span
//   at the next function that has call references.
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.OperandType;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.pcode.JumpTable;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class FixJumpTables extends GhidraScript {
  PrintWriter log;

  public void run() throws Exception {
    String[] args = getScriptArgs();
    log = new PrintWriter(new FileWriter(args.length > 0 ? args[0] : "/tmp/fixjumptables.txt"));
    MemoryBlock text = currentProgram.getMemory().getBlock(".text");
    Listing listing = currentProgram.getListing();
    FunctionManager fm = currentProgram.getFunctionManager();
    List<Address[]> spans = new ArrayList<>();
    int limit = Integer.MAX_VALUE;
    boolean refsOnly = false;
    for (int i = 1; i < args.length; i++) {
      if (args[i].startsWith("limit=")) { limit = Integer.parseInt(args[i].substring(6)); continue; }
      if (args[i].equals("refsonly")) { refsOnly = true; continue; }
      if (args[i].startsWith("allocaprobe=")) {
        // MSVC _chkstk: without the call fixup Ghidra loses the stack pointer
        // after it, so callers with big frames decompile with no call arguments.
        Function ap = fm.getFunctionAt(toAddr(Long.parseLong(args[i].substring(12), 16)));
        ap.setName("__alloca_probe", SourceType.USER_DEFINED);
        ap.setCallFixup("alloca_probe");
        println("alloca_probe fixup on " + ap.getEntryPoint());
        continue;
      }
      String[] p = args[i].split(":");
      Address owner = toAddr(Long.parseLong(p[0], 16));
      Address end;
      if (p.length > 1) end = toAddr(Long.parseLong(p[1], 16));
      else {
        // owner only: the span ends at the next function that something calls
        // (case fragments are only ever jumped to)
        end = text.getEnd().add(1);
        for (Function g : fm.getFunctions(owner.add(1), true)) {
          boolean called = false;
          for (Reference r : currentProgram.getReferenceManager().getReferencesTo(g.getEntryPoint()))
            if (r.getReferenceType().isCall()) { called = true; break; }
          if (called) { end = g.getEntryPoint(); break; }
        }
        println("span " + owner + " ends at " + end);
      }
      if (fm.getFunctionAt(owner) == null) {
        if (listing.getInstructionAt(owner) == null) disassemble(owner);
        createFunction(owner, null);
        println("created function at " + owner);
      }
      List<Address> doomed = new ArrayList<>();
      for (Function g : fm.getFunctions(new AddressSet(owner.add(1), end.subtract(1)), true)) if (g.getEntryPoint().compareTo(owner) > 0 && g.getEntryPoint().compareTo(end) < 0) doomed.add(g.getEntryPoint());
      for (Address a : doomed) { fm.removeFunction(a); log.println("span " + owner + ": deleted function at " + a); }
      // jumps into the deleted fragments were marked as tail calls
      InstructionIterator ii = listing.getInstructions(new AddressSet(owner, end.subtract(1)), true);
      while (ii.hasNext()) {
        Instruction in = ii.next();
        if (in.getFlowOverride() != ghidra.program.model.listing.FlowOverride.NONE) {
          log.println("span " + owner + ": cleared flow override " + in.getFlowOverride() + " at " + in.getAddress());
          in.setFlowOverride(ghidra.program.model.listing.FlowOverride.NONE);
        }
      }
      spans.add(new Address[] {owner, end});
    }
    // all table starts, so one table's length stops at the next
    TreeSet<Long> tableStarts = new TreeSet<>();
    {
      InstructionIterator it0 = listing.getInstructions(text.getStart(), true);
      while (it0.hasNext()) {
        Instruction ins = it0.next();
        if (!text.contains(ins.getAddress())) break;
        if (!ins.getMnemonicString().equalsIgnoreCase("JMP")) continue;
        for (Object o : ins.getOpObjects(0)) if (o instanceof Scalar) {
          long v = ((Scalar) o).getUnsignedValue();
          if (v >= text.getStart().getOffset() && v <= text.getEnd().getOffset() && ins.toString().contains("*0x4")) tableStarts.add(v);
        }
      }
    }
    Set<Address> overridden = new HashSet<>();
    for (int round = 0; round < 40; round++) {
      int fixed = 0, deleted = 0;
      // a span owner's body grows by flow as its jump tables get resolved
      for (Address[] sp : spans) {
        Function f = fm.getFunctionAt(sp[0]);
        if (f != null) CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
      }
      InstructionIterator it = listing.getInstructions(text.getStart(), true);
      List<Instruction> jumps = new ArrayList<>();
      while (it.hasNext()) {
        Instruction ins = it.next();
        if (!text.contains(ins.getAddress())) break;
        if (!ins.getMnemonicString().equalsIgnoreCase("JMP")) continue;
        if ((ins.getOperandType(0) & OperandType.INDIRECT) == 0 && !ins.toString().contains("[")) continue;
        String s = ins.toString();
        if (!s.contains("*0x4") && !s.contains("*4")) continue;
        jumps.add(ins);
      }
      for (Instruction ins : jumps) {
        Object[] objs = ins.getOpObjects(0);
        long table = -1;
        for (Object o : objs) if (o instanceof Scalar) {
          long v = ((Scalar) o).getUnsignedValue();
          if (v >= text.getStart().getOffset() && v <= text.getEnd().getOffset()) table = v;
        }
        if (table < 0) continue;
        Function f = fm.getFunctionContaining(ins.getAddress());
        if (f == null) {
          log.println("no function contains jump at " + ins.getAddress());
          continue;
        }
        ArrayList<Address> dests = new ArrayList<>();
        Address ta = toAddr(table);
        Long nextTable = tableStarts.higher(table);
        for (int i = 0; i < 1024; i++) {
          Address ea = ta.add(i * 4L);
          if (nextTable != null && ea.getOffset() >= nextTable) break;
          if (i > 0 && (listing.getInstructionContaining(ea) != null && i > 0 && !isTableData(ea))) break;
          int v;
          try { v = currentProgram.getMemory().getInt(ea); } catch (Exception e) { break; }
          long uv = v & 0xffffffffL;
          if (uv < f.getEntryPoint().getOffset() || uv > text.getEnd().getOffset()) break;
          Address d = toAddr(uv);
          // entries must point at code inside or right after this function's span
          if (uv - f.getEntryPoint().getOffset() > 0x40000) break;
          dests.add(d);
        }
        if (dests.isEmpty()) continue;
        boolean change = false;
        for (Address d : dests) {
          Function g = fm.getFunctionAt(d);
          if (g != null && !g.equals(f)) {
            log.println("delete fragment " + g.getName() + " (case of jump at " + ins.getAddress() + " in " + f.getName() + ")");
            fm.removeFunction(d);
            deleted++;
            change = true;
          }
          if (listing.getInstructionAt(d) == null) {
            disassemble(d);
            change = true;
          }
          if (!f.getBody().contains(d)) change = true;
        }
        if (!change) continue;
        if (overridden.contains(ins.getAddress())) continue;
        if (overridden.size() >= limit) continue;
        if (refsOnly) overridden.add(ins.getAddress());
        else try {
          new JumpTable(ins.getAddress(), dests, true, 0).writeOverride(f);
          overridden.add(ins.getAddress());
        } catch (Exception e) {
          println("override failed at " + ins.getAddress() + " in " + f.getName() + " body " + f.getBody().getMinAddress() + "-" + f.getBody().getMaxAddress() + ": " + e.getMessage());
          continue;
        }
        for (Address d : dests)
          currentProgram.getReferenceManager().addMemoryReference(ins.getAddress(), d,
              RefType.COMPUTED_JUMP, SourceType.USER_DEFINED, 0);
        CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
        log.println("fixed jump at " + ins.getAddress() + " in " + f.getName() + ": " + dests.size() + " cases, table " + ta);
        fixed++;
      }
      println("round " + round + ": fixed " + fixed + " jumps, deleted " + deleted + " fragments");
      log.flush();
      if (fixed == 0 && deleted == 0) break;
    }
    log.close();
  }

  boolean isTableData(Address a) {
    Data d = currentProgram.getListing().getDataContaining(a);
    return d != null;
  }
}
