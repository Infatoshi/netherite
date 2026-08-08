/* Phase-A deductive proof for the heavy-buffer lightmap drop-in (no game needed).
 * Loads libqlm.so, feeds it kernel 11's captured golden inputs (the exact scalars +
 * 16-float brightness table real Minecraft read), gets back the int[256] via the same
 * JNI marshaling the in-game path uses, and compares bit-for-bit to kernel 11's
 * golden.txt (256 ARGB ints captured from the running game).
 *
 * Build: javac TestJniLm.java
 * Run:   java -Djava.library.path=. TestJniLm <kernel11 golden dir>
 */
import java.io.*;
import java.util.*;

public class TestJniLm {
    public static native void nlightmap(float f, float gamma, float torchFlickerX,
                                        int lastLightning, int dimId,
                                        float[] brightnessTable, int[] out);

    public static void main(String[] args) throws Exception {
        System.load(new File(args.length > 1 ? args[1] : "libqlm.so").getAbsolutePath());
        String dir = args[0];
        // parse inputs.txt: "<label> <value>" pairs, then "brightnessTable <16 ints>"
        Map<String, Long> sc = new HashMap<>();
        float[] tbl = new float[16];
        try (Scanner s = new Scanner(new File(dir, "inputs.txt"))) {
            while (s.hasNext()) {
                String label = s.next();
                if (label.equals("brightnessTable")) {
                    for (int i = 0; i < 16; i++) tbl[i] = Float.intBitsToFloat(s.nextInt());
                } else {
                    sc.put(label, (long) s.nextInt());
                }
            }
        }
        float f       = Float.intBitsToFloat(sc.get("sunBrightness").intValue());
        float gamma   = Float.intBitsToFloat(sc.get("gamma").intValue());
        float torch   = Float.intBitsToFloat(sc.get("torchFlickerX").intValue());
        int lastLight = sc.get("lastLightningBolt").intValue();
        int dimId     = sc.get("dimId").intValue();

        int[] out = new int[256];
        nlightmap(f, gamma, torch, lastLight, dimId, tbl, out);

        int[] golden = new int[256];
        try (Scanner s = new Scanner(new File(dir, "golden.txt"))) {
            for (int i = 0; i < 256; i++) golden[i] = s.nextInt();
        }
        int mism = 0, first = -1;
        for (int i = 0; i < 256; i++)
            if (out[i] != golden[i]) { mism++; if (first < 0) first = i; }
        System.out.println("checked=256 mismatches=" + mism
            + (mism == 0 ? "  -> PASS (heavy-buffer int[256] bit-exact vs vanilla)"
                         : "  -> FAIL first@" + first + " native=" + out[first] + " golden=" + golden[first]));
        System.exit(mism == 0 ? 0 : 1);
    }
}
