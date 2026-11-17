import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

public class DumpStringsAndXrefs extends ghidra.app.script.GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("usage: DumpStringsAndXrefs.java <output-file>");
        Listing listing = currentProgram.getListing();
        ReferenceManager refs = currentProgram.getReferenceManager();
        try (PrintWriter out = new PrintWriter(new File(args[0]), StandardCharsets.UTF_8)) {
            out.println("FUNCTIONS");
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext()) {
                Function f = functions.next();
                if (f.getEntryPoint().getAddressSpace().getName().equals("ram")) {
                    out.println(f.getEntryPoint() + " size=" + f.getBody().getNumAddresses() + " " + f.getName());
                }
            }
            out.println("\nSELECTED STRINGS AND XREFS");
            DataIterator data = listing.getDefinedData(true);
            while (data.hasNext()) {
                Data d = data.next();
                String value = d.getDefaultValueRepresentation();
                if (value == null || !value.matches("(?is).*?(SCAN|PORT|socket|connect|resolve|hostname|START|HALT|RESET|DEBUG|SAVE|CLEAR).*?")) continue;
                out.println("STRING " + d.getAddress() + " " + value);
                ReferenceIterator incoming = refs.getReferencesTo(d.getAddress());
                while (incoming.hasNext()) {
                    Reference ref = incoming.next();
                    Function f = currentProgram.getFunctionManager().getFunctionContaining(ref.getFromAddress());
                    out.println("  XREF " + ref.getFromAddress() + " " + ref.getReferenceType() + " in=" + (f == null ? "<no function>" : f.getEntryPoint() + " " + f.getName()));
                }
            }
        }
    }
}
