// Finds defined strings containing the given text and decompiles every function that references them.
// Headless usage (read-only):
//   analyzeHeadless <projectDir> <project> -process <program> -noanalysis -readOnly
//       -scriptPath tools/ghidra -postScript StringXrefDecompile.java "<text>" [maxFunctions]
// @category SACore

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

import java.util.LinkedHashSet;
import java.util.Set;

public class StringXrefDecompile extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String needle = args.length > 0 ? args[0] : "";
        int maxFunctions = args.length > 1 ? Integer.parseInt(args[1]) : 5;
        long imageBase = currentProgram.getImageBase().getOffset();

        Set<Function> functions = new LinkedHashSet<>();
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Data data = it.next();
            Object value = data.getValue();
            if (!(value instanceof String) || !((String) value).contains(needle)) {
                continue;
            }
            println(String.format("STRING %s (rva %#x) \"%s\"", data.getAddress(),
                    data.getAddress().getOffset() - imageBase, value));
            for (Reference ref : getReferencesTo(data.getAddress())) {
                Address from = ref.getFromAddress();
                Function f = getFunctionContaining(from);
                println(String.format("  XREF from %s in %s", from, f == null ? "<no function>" : f.getName() + "@" + f.getEntryPoint()));
                if (f != null) {
                    functions.add(f);
                }
            }
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        int count = 0;
        for (Function f : functions) {
            if (count++ >= maxFunctions) {
                break;
            }
            DecompileResults results = decompiler.decompileFunction(f, 60, monitor);
            println("==== FUNCTION " + f.getName() + " @ " + f.getEntryPoint() + " (rva "
                    + String.format("%#x", f.getEntryPoint().getOffset() - imageBase) + ")");
            println(results.decompileCompleted() ? results.getDecompiledFunction().getC() : "<decompile failed: " + results.getErrorMessage() + ">");
        }
        decompiler.dispose();
    }
}
