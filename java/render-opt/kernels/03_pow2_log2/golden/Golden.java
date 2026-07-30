// GOLDEN: verbatim decompiled Minecraft 1.11.2 source. DO NOT "clean up" or optimize.
// Source: src/net/minecraft/util/math/MathHelper.java
//   smallestEncompassingPowerOfTwo() (~331), isPowerOfTwo() (~345), log2DeBruijn() (~355),
//   and the MULTIPLY_DE_BRUIJN_BIT_POSITION table from the static initializer (~620).
// Reads one int per line from stdin; prints two ints per line (space separated):
//   smallestEncompassingPowerOfTwo(v)  log2DeBruijn(v)
// Plain decimal ints (discrete output -> bitwise compare).
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

public class Golden {
    // --- verbatim from MathHelper ---
    private static final int[] MULTIPLY_DE_BRUIJN_BIT_POSITION;

    public static int smallestEncompassingPowerOfTwo(int value)
    {
        int i = value - 1;
        i = i | i >> 1;
        i = i | i >> 2;
        i = i | i >> 4;
        i = i | i >> 8;
        i = i | i >> 16;
        return i + 1;
    }

    private static boolean isPowerOfTwo(int value)
    {
        return value != 0 && (value & value - 1) == 0;
    }

    public static int log2DeBruijn(int value)
    {
        value = isPowerOfTwo(value) ? value : smallestEncompassingPowerOfTwo(value);
        return MULTIPLY_DE_BRUIJN_BIT_POSITION[(int)((long)value * 125613361L >> 27) & 31];
    }

    static
    {
        MULTIPLY_DE_BRUIJN_BIT_POSITION = new int[] {0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8, 31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9};
    }
    // --- /verbatim ---

    public static void main(String[] args) throws IOException {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            int v = Integer.parseInt(line);
            sb.append(smallestEncompassingPowerOfTwo(v)).append(' ')
              .append(log2DeBruijn(v)).append('\n');
        }
        System.out.print(sb);
    }
}
