// Lists functions referencing an address (data or code) and decompiles the smallest ones,
// or decompiles the function containing the address when called with "at".
// Headless usage (read-only):
//   ... -postScript AddressXrefDecompile.java xrefs <hexAddress> [maxFunctions]
//   ... -postScript AddressXrefDecompile.java at <hexAddress>
// @category SACore

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class AddressXrefDecompile extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String mode = args[0];
        Address target = toAddr(Long.parseLong(args[1].replace("0x", ""), 16));
        int maxFunctions = args.length > 2 ? Integer.parseInt(args[2]) : 3;
        long imageBase = currentProgram.getImageBase().getOffset();

        List<Function> toDecompile = new ArrayList<>();
        if (mode.equals("at")) {
            Function f = getFunctionContaining(target);
            if (f != null) {
                toDecompile.add(f);
            }
        } else {
            Set<Function> functions = new LinkedHashSet<>();
            int refs = 0;
            for (Reference ref : getReferencesTo(target)) {
                refs++;
                Function f = getFunctionContaining(ref.getFromAddress());
                if (f != null) {
                    functions.add(f);
                }
            }
            println(String.format("%d references from %d functions to %s", refs, functions.size(), target));
            List<Function> sorted = new ArrayList<>(functions);
            sorted.sort(Comparator.comparingLong(f -> f.getBody().getNumAddresses()));
            for (Function f : sorted) {
                println(String.format("  %s @ %s size %d", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses()));
            }
            toDecompile.addAll(sorted.subList(0, Math.min(maxFunctions, sorted.size())));
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        for (Function f : toDecompile) {
            DecompileResults results = decompiler.decompileFunction(f, 60, monitor);
            println("==== FUNCTION " + f.getName() + " @ " + f.getEntryPoint() + " (rva "
                    + String.format("%#x", f.getEntryPoint().getOffset() - imageBase) + ")");
            println(results.decompileCompleted() ? results.getDecompiledFunction().getC() : "<decompile failed>");
        }
        decompiler.dispose();
    }
}
