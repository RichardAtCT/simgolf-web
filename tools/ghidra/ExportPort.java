// Exports golf.exe for the mechanical C translation (tools/translate).
// Writes to <outdir>:
//   types.h        every data type in the program (Ghidra's DataTypeWriter)
//   functions.jsonl one record per function: address, name, thunk target,
//                   decompiled signature and C, and the global symbols the
//                   decompiler referenced with their inferred types
//   image.bin      raw bytes of the loaded image from 0x400000, zero-filled
//
// Usage: -postScript ExportPort.java <outdir> [key=value ...]
//   from=HEX to=HEX  only functions in [from, to) (functions.jsonl gets a
//                    -from suffix so parallel runs can be merged)
//   timeout=SECONDS  per-function decompile timeout (default 30)
//   noimage          skip types.h and image.bin
// Before decompiling, __ftol gets an explicit ST0 parameter so the decompiler
// shows the value being converted (run with -readOnly; nothing is saved).
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.pcode.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.symbol.Reference;
import java.io.*;
import java.util.*;

public class ExportPort extends GhidraScript {
  static String esc(String s) {
    StringBuilder b = new StringBuilder("\"");
    for (char c : s.toCharArray()) {
      switch (c) {
        case '"': b.append("\\\""); break;
        case '\\': b.append("\\\\"); break;
        case '\n': b.append("\\n"); break;
        case '\r': b.append("\\r"); break;
        case '\t': b.append("\\t"); break;
        default:
          if (c < 0x20 || c > 0x7e) b.append(String.format("\\u%04x", (int) c));
          else b.append(c);
      }
    }
    return b.append('"').toString();
  }

  // C declarator for type dt wrapped around inner ("" for abstract).
  static String decl(DataType dt, String inner) {
    if (dt instanceof TypeDef && dt.getName().startsWith("undefined")) return dt.getName() + (inner.isEmpty() ? "" : " " + inner);
    if (dt instanceof Pointer) {
      DataType p = ((Pointer) dt).getDataType();
      String in = "*" + inner;
      if (p == null) return "void " + in;
      if (p instanceof Array || p instanceof FunctionDefinition) in = "(" + in + ")";
      return decl(p, in);
    }
    if (dt instanceof Array) {
      Array a = (Array) dt;
      return decl(a.getDataType(), inner + "[" + a.getNumElements() + "]");
    }
    if (dt instanceof FunctionDefinition) {
      return "code" + (inner.isEmpty() ? "" : " " + inner);
    }
    String n = dt.getDisplayName();
    if (dt instanceof Structure || dt instanceof Union) {
      // DataTypeWriter emits typedefs for these by name
    }
    return n + (inner.isEmpty() ? "" : " " + inner);
  }

  // Replaces every CALL to `target` with NOPs. Used for the debug CRT's
  // __chkesp, which preserves EAX but which the decompiler treats as
  // clobbering it, so functions that end with it "return" garbage.
  void nopCalls(long target) throws Exception {
    Address t = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(target);
    int tx = currentProgram.startTransaction("nop calls");
    int n = 0;
    try {
      for (Reference r : getReferencesTo(t)) {
        if (!r.getReferenceType().isCall()) continue;
        Address from = r.getFromAddress();
        Instruction ins = getInstructionAt(from);
        if (ins == null || ins.getLength() != 5) continue;
        clearListing(from, from.add(4));
        setBytes(from, new byte[] {(byte) 0x90, (byte) 0x90, (byte) 0x90, (byte) 0x90, (byte) 0x90});
        disassemble(from);
        n++;
      }
    } finally {
      currentProgram.endTransaction(tx, true);
    }
    println("nopped " + n + " calls to " + t);
  }

  void fixFtol() throws Exception {
    for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
      if (!f.getName().equals("__ftol")) continue;
      int tx = currentProgram.startTransaction("ftol");
      try {
        Register st0 = currentProgram.getRegister("ST0");
        Register eax = currentProgram.getRegister("EAX");
        ParameterImpl p = new ParameterImpl("x", new Float10DataType(),
            new VariableStorage(currentProgram, st0), currentProgram);
        f.updateFunction("__cdecl", new ReturnParameterImpl(IntegerDataType.dataType,
            new VariableStorage(currentProgram, eax), currentProgram),
            java.util.List.of(p), Function.FunctionUpdateType.CUSTOM_STORAGE, true,
            ghidra.program.model.symbol.SourceType.USER_DEFINED);
        println("__ftol at " + f.getEntryPoint() + " now takes ST0");
      } finally {
        currentProgram.endTransaction(tx, true);
      }
    }
  }

  public void run() throws Exception {
    String[] args = getScriptArgs();
    String out = args[0];
    long from = 0, to = Long.MAX_VALUE;
    int timeout = 30;
    boolean image = true;
    String style = null;
    for (int i = 1; i < args.length; i++) {
      String a = args[i];
      if (a.startsWith("from=")) from = Long.parseLong(a.substring(5), 16);
      else if (a.startsWith("to=")) to = Long.parseLong(a.substring(3), 16);
      else if (a.startsWith("timeout=")) timeout = Integer.parseInt(a.substring(8));
      else if (a.equals("noimage")) image = false;
      else if (a.startsWith("style=")) style = a.substring(6);
      else if (a.startsWith("nopcalls=")) nopCalls(Long.parseLong(a.substring(9), 16));
    }
    new File(out).mkdirs();
    fixFtol();
    if (image) {
    try (Writer w = new BufferedWriter(new FileWriter(out + "/types.h"))) {
      DataTypeWriter dtw = new DataTypeWriter(currentProgram.getDataTypeManager(), w);
      dtw.write(currentProgram.getDataTypeManager(), monitor);
    }

    Memory mem = currentProgram.getMemory();
    Address base = currentProgram.getImageBase();
    long end = 0;
    for (MemoryBlock b : mem.getBlocks())
      if (b.getStart().getOffset() >= base.getOffset() && b.getStart().getOffset() < base.getOffset() + 0x8000000L)
        end = Math.max(end, b.getEnd().getOffset() + 1);
    byte[] img = new byte[(int) (end - base.getOffset())];
    for (MemoryBlock b : mem.getBlocks()) {
      long s = b.getStart().getOffset();
      if (s < base.getOffset() || s >= base.getOffset() + 0x8000000L || !b.isInitialized()) continue;
      byte[] buf = new byte[(int) b.getSize()];
      int n = b.getBytes(b.getStart(), buf);
      System.arraycopy(buf, 0, img, (int) (s - base.getOffset()), n);
    }
    try (FileOutputStream f = new FileOutputStream(out + "/image.bin")) { f.write(img); }
    }

    DecompInterface di = new DecompInterface();
    DecompileOptions opt = new DecompileOptions();
    opt.grabFromProgram(currentProgram);
    opt.setMaxInstructions(2000000);
    opt.setMaxPayloadMBytes(1000);
    opt.setMaxJumpTableEntries(4096);
    di.setOptions(opt);
    if (style != null) di.setSimplificationStyle(style);
    di.openProgram(currentProgram);
    int count = 0;
    try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out + "/functions" + (from > 0 ? String.format("-%08x", from) : "") + ".jsonl")))) {
      for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
        if (monitor.isCancelled()) break;
        if (f.isExternal()) continue;
        long ea = f.getEntryPoint().getOffset();
        if (ea < from || ea >= to) continue;
        count++;
        StringBuilder r = new StringBuilder("{");
        r.append("\"addr\":\"").append(f.getEntryPoint()).append("\"");
        r.append(",\"name\":").append(esc(f.getName(true)));
        r.append(",\"conv\":").append(esc(f.getCallingConventionName()));
        r.append(",\"proto\":").append(esc(f.getPrototypeString(true, false)));
        r.append(",\"stackPurge\":").append(f.getStackPurgeSize());
        if (f.isThunk()) {
          Function t = f.getThunkedFunction(true);
          r.append(",\"thunk\":").append(esc(t.getName(true)));
          r.append(",\"thunkAddr\":\"").append(t.getEntryPoint()).append("\"");
          r.append(",\"thunkExternal\":").append(t.isExternal());
          w.println(r.append("}"));
          continue;
        }
        long t0 = System.currentTimeMillis();
        DecompileResults res = di.decompileFunction(f, timeout, monitor);
        long ms = System.currentTimeMillis() - t0;
        if (ms > 5000) println("slow: " + f.getEntryPoint() + " " + ms + " ms" + (res.decompileCompleted() ? "" : " FAILED"));
        if (!res.decompileCompleted()) {
          r.append(",\"error\":").append(esc(String.valueOf(res.getErrorMessage())));
          w.println(r.append("}"));
          continue;
        }
        DecompiledFunction df = res.getDecompiledFunction();
        r.append(",\"sig\":").append(esc(df.getSignature()));
        r.append(",\"c\":").append(esc(df.getC()));
        HighFunction hf = res.getHighFunction();
        r.append(",\"globals\":[");
        boolean first = true;
        Iterator<HighSymbol> it = hf.getGlobalSymbolMap().getSymbols();
        while (it.hasNext()) {
          HighSymbol hs = it.next();
          Address a = null;
          try { a = hs.getStorage().getMinAddress(); } catch (Exception e) {}
          DataType dt = hs.getDataType();
          if (!first) r.append(',');
          first = false;
          r.append("{\"name\":").append(esc(hs.getName()));
          r.append(",\"addr\":\"").append(a == null ? "" : a.toString()).append("\"");
          r.append(",\"size\":").append(hs.getSize());
          r.append(",\"type\":").append(esc(dt == null ? "" : decl(dt, "")));
          r.append(",\"ptr\":").append(esc(dt == null ? "" : decl(dt, "(*)")));
          r.append("}");
        }
        r.append("]");
        // Every memory varnode the function touches, with the size and type the
        // decompiler gave it: names like _DAT_x (a wider access than the
        // symbol at x) need these.
        r.append(",\"mem\":[");
        Map<String, String> mem = new TreeMap<>();
        Iterator<PcodeOpAST> ops = hf.getPcodeOps();
        while (ops.hasNext()) {
          PcodeOpAST op = ops.next();
          List<Varnode> vns = new ArrayList<>(Arrays.asList(op.getInputs()));
          if (op.getOutput() != null) vns.add(op.getOutput());
          for (Varnode vn : vns) {
            if (vn == null || !vn.getAddress().isMemoryAddress() || vn.getAddress().isStackAddress()) continue;
            if (!vn.getAddress().getAddressSpace().isLoadedMemorySpace()) continue;
            HighVariable hv = vn.getHigh();
            if (!(hv instanceof HighGlobal)) continue;
            DataType dt = hv.getDataType();
            String key = vn.getAddress().toString() + ":" + vn.getSize();
            mem.putIfAbsent(key, dt == null ? "" : decl(dt, "(*)"));
          }
        }
        boolean mf = true;
        for (var e : mem.entrySet()) {
          String[] k = e.getKey().split(":");
          if (!mf) r.append(',');
          mf = false;
          r.append("[\"").append(k[0]).append("\",").append(k[1]).append(",").append(esc(e.getValue())).append("]");
        }
        r.append("]");
        w.println(r.append("}"));
        w.flush();
        if (count % 200 == 0) println("exported " + count);
      }
    }
    println("done: " + count + " functions");
  }
}
