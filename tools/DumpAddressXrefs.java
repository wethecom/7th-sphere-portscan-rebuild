import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

public class DumpAddressXrefs extends ghidra.app.script.GhidraScript {
    @Override protected void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter out = new PrintWriter(new File(args[0]), StandardCharsets.UTF_8)) {
            for (int i = 1; i < args.length; i++) {
                long offset = Long.parseUnsignedLong(args[i], 16);
                Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(offset);
                out.println("TARGET " + target);
                ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(target);
                while (refs.hasNext()) {
                    Reference ref = refs.next();
                    Function f = currentProgram.getFunctionManager().getFunctionContaining(ref.getFromAddress());
                    Instruction ins = currentProgram.getListing().getInstructionAt(ref.getFromAddress());
                    out.println("  REF " + ref.getFromAddress() + " type=" + ref.getReferenceType() + " function=" + (f == null ? "<none>" : f.getEntryPoint()+" "+f.getName()) + " ins=" + (ins == null ? "<none>" : ins.toString()));
                }
            }
        }
    }
}
