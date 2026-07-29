import java.io.File;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

/* Phase-A bit-exactness harness for the AO drop-in: calls native naoBrightness() over
 * kernel 13's input stream and checks bitwise equality vs the real-MC Golden output
 * (first column = getAoBrightness). Usage:
 *   java -cp . TestJniAO <inputs.txt> <golden.txt> <libqao.so> */
public class TestJniAO {
    public static native int naoBrightness(int br1, int br2, int br3, int br4);

    public static void main(String[] args) throws Exception {
        String inputs = args[0], golden = args[1], lib = args[2];
        System.load(new File(lib).getAbsolutePath());
        List<String> in = Files.readAllLines(Paths.get(inputs));
        List<String> gd = Files.readAllLines(Paths.get(golden));
        int checked = 0, mism = 0;
        for (int i = 0; i < in.size() && i < gd.size(); i++) {
            String line = in.get(i).trim();
            if (line.isEmpty()) continue;
            String[] t = line.split("\\s+");
            int got = naoBrightness(Integer.parseInt(t[0]), Integer.parseInt(t[1]),
                                    Integer.parseInt(t[2]), Integer.parseInt(t[3]));
            int exp = Integer.parseInt(gd.get(i).trim().split("\\s+")[0]); // first col = ao
            checked++;
            if (got != exp) { mism++; if (mism <= 5) System.err.println("mismatch line " + i + " got=" + got + " exp=" + exp); }
        }
        System.out.println("checked=" + checked + " mismatches=" + mism + (mism == 0 ? "  -> PASS (AO brightness bit-exact vs vanilla)" : "  -> FAIL"));
    }
}
