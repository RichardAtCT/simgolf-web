// Replaces every CALL to the given function with NOPs and saves the program.
// For jgld.dll's debug-CRT __chkesp (0x1007e780): it preserves EAX, but the
// decompiler assumes calls clobber it, so return values and signatures come
// out wrong unless the calls are gone before CommitSignatures runs.
// With "fill" as a second argument it also NOPs the debug prologue's stack
// fill (mov ecx,N; mov eax,0xCCCCCCCC; rep stosd): the decompiler loses the
// `this` that the prologue saves around it with push/pop ecx.
// Usage: -postScript NopCalls.java <hexaddr> [fill]   (not -readOnly)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import java.util.*;

public class NopCalls extends GhidraScript {
  void nopFills() throws Exception {
    ghidra.program.model.mem.MemoryBlock text = currentProgram.getMemory().getBlock(".text");
    byte[] b = new byte[(int) text.getSize()];
    text.getBytes(text.getStart(), b);
    int n = 0;
    for (int i = 0; i + 12 <= b.length; i++) {
      if ((b[i] & 0xff) != 0xb9 || (b[i + 5] & 0xff) != 0xb8) continue;
      if ((b[i + 6] & 0xff) != 0xcc || (b[i + 7] & 0xff) != 0xcc || (b[i + 8] & 0xff) != 0xcc
          || (b[i + 9] & 0xff) != 0xcc || (b[i + 10] & 0xff) != 0xf3 || (b[i + 11] & 0xff) != 0xab) continue;
      Address a = text.getStart().add(i);
      if (getInstructionAt(a) == null) continue;
      // methods save ECX around the fill: push ecx; lea edi,[ebp-X]; <fill>; pop ecx.
      // The decompiler loses `this` through that pair, so it goes too.
      int start = i, end = i + 12;
      int lea = (b[i - 3] & 0xff) == 0x8d && (b[i - 2] & 0xff) == 0x7d ? i - 3
          : (b[i - 6] & 0xff) == 0x8d && (b[i - 5] & 0xff) == 0xbd ? i - 6 : -1;
      if (lea > 0 && (b[lea - 1] & 0xff) == 0x51 && (b[i + 12] & 0xff) == 0x59) {
        start = lea - 1;
        end = i + 13;
      }
      Address s = text.getStart().add(start);
      clearListing(s, text.getStart().add(end - 1));
      byte[] nops = new byte[end - start];
      Arrays.fill(nops, (byte) 0x90);
      setBytes(s, nops);
      disassemble(s);
      n++;
    }
    println("nopped " + n + " debug stack fills");
  }

  public void run() throws Exception {
    Address t = toAddr(Long.parseLong(getScriptArgs()[0], 16));
    List<Address> sites = new ArrayList<>();
    for (Reference r : getReferencesTo(t))
      if (r.getReferenceType().isCall()) sites.add(r.getFromAddress());
    int n = 0;
    for (Address from : sites) {
      Instruction ins = getInstructionAt(from);
      if (ins == null || ins.getLength() != 5) continue;
      clearListing(from, from.add(4));
      setBytes(from, new byte[] {(byte) 0x90, (byte) 0x90, (byte) 0x90, (byte) 0x90, (byte) 0x90});
      disassemble(from);
      n++;
    }
    println("nopped " + n + " calls to " + t);
    if (getScriptArgs().length > 1 && getScriptArgs()[1].equals("fill")) nopFills();
  }
}
