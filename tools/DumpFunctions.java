import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

public class DumpFunctions extends ghidra.app.script.GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("usage: DumpFunctions.java <output-file>");
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(false);
        if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException("Could not open program in decompiler");
        File output = new File(args[0]);
        try (PrintWriter writer = new PrintWriter(output, StandardCharsets.UTF_8)) {
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext() && !monitor.isCancelled()) {
                Function function = functions.next();
                DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                writer.println("// FUNCTION " + function.getEntryPoint() + " " + function.getName());
                if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
                    writer.println(result.getDecompiledFunction().getC());
                } else {
                    writer.println("// DECOMPILATION FAILED: " + result.getErrorMessage());
                }
                writer.println();
                writer.flush();
            }
        } finally {
            decompiler.dispose();
        }
    }
}
