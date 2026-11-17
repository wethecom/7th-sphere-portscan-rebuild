import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.address.Address;
import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

public class DumpListing extends ghidra.app.script.GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3) throw new IllegalArgumentException("usage: DumpListing.java <output> <start> <end>");
        Address start = currentProgram.getAddressFactory().getAddress(args[1]);
        Address end = currentProgram.getAddressFactory().getAddress(args[2]);
        try (PrintWriter out = new PrintWriter(new File(args[0]), StandardCharsets.UTF_8)) {
            for (CodeUnit cu : currentProgram.getListing().getCodeUnits(start, true)) {
                if (cu.getAddress().compareTo(end) >= 0) break;
                byte[] bytes = cu.getBytes();
                StringBuilder hex = new StringBuilder();
                for (byte b : bytes) hex.append(String.format("%02x ", b & 0xff));
                out.printf("%s  %-30s  %s%n", cu.getAddress(), hex, cu.toString());
            }
        }
    }
}
