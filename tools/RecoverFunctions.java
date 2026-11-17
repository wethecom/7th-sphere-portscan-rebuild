import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

public class RecoverFunctions extends ghidra.app.script.GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("usage: RecoverFunctions.java <output> <address>...");
        for (int i = 1; i < args.length; i++) {
            Address addr = currentProgram.getAddressFactory().getAddress(args[i]);
            if (currentProgram.getFunctionManager().getFunctionAt(addr) == null) {
                disassemble(addr);
                createFunction(addr, null);
            }
        }
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(false);
        decompiler.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(new File(args[0]), StandardCharsets.UTF_8)) {
            for (int i = 1; i < args.length; i++) {
                Address addr = currentProgram.getAddressFactory().getAddress(args[i]);
                Function f = currentProgram.getFunctionManager().getFunctionAt(addr);
                if (f == null) { out.println("// no function at " + addr); continue; }
                DecompileResults r = decompiler.decompileFunction(f, 90, monitor);
                out.println("// FUNCTION " + f.getEntryPoint() + " " + f.getName());
                out.println(r.decompileCompleted() && r.getDecompiledFunction() != null ? r.getDecompiledFunction().getC() : "// failed: " + r.getErrorMessage());
                out.println();
            }
        } finally { decompiler.dispose(); }
    }
}
