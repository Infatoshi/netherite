import java.io.File;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

/* Phase-A bit-exactness harness for the biome-blend drop-in: calls the native
 * nblend() over kernel 18's captured real-MC golden and checks bitwise equality.
 * Usage: java -cp . TestJniBiome <golden_dir> <libqbiome.so> */
public class TestJniBiome {
    public static native int nblend(int[] colors);

    public static void main(String[] args) throws Exception {
        String goldenDir = args[0];
        String lib = args[1];
        System.load(new File(lib).getAbsolutePath());
        List<String> ins = Files.readAllLines(Paths.get(goldenDir, "inputs.txt"));
        List<String> outs = Files.readAllLines(Paths.get(goldenDir, "golden.txt"));
        int checked = 0, mism = 0;
        for (int i = 0; i < ins.size() && i < outs.size(); i++) {
            String line = ins.get(i).trim();
            if (line.isEmpty()) continue;
            String[] t = line.split("\\s+");
            if (t.length < 9) continue;
            int[] c = new int[9];
            for (int j = 0; j < 9; j++) c[j] = Integer.parseInt(t[j]);
            int got = nblend(c);
            int exp = Integer.parseInt(outs.get(i).trim());
            checked++;
            if (got != exp) { mism++; if (mism <= 5) System.err.println("mismatch line " + i + " got=" + got + " exp=" + exp); }
        }
        System.out.println("checked=" + checked + " mismatches=" + mism + (mism == 0 ? "  -> PASS (biome 3x3 blend bit-exact vs vanilla)" : "  -> FAIL"));
    }
}
