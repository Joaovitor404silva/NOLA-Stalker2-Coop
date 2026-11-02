// Desliga analisadores lentos para uma importação rápida.
import ghidra.app.script.GhidraScript;

public class FastOpts extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] off = {"Decompiler Parameter ID", "Decompiler Switch Analysis", "Stack", "Aggressive Instruction Finder",
                        "Non-Returning Functions - Discovered", "PDB Universal", "Demangler Microsoft", "Windows x86 PE Exception Handling"};
        for (String o : off) { try { setAnalysisOption(currentProgram, o, "false"); println("off: " + o); } catch (Exception e) { println("ignorado: " + o); } }
    }
}
