/* GOLDEN: verbatim decompiled MC BlockModelRenderer pure packed-brightness helpers.
 * Source: src/net/minecraft/client/renderer/BlockModelRenderer.java
 *   getAoBrightness(int,int,int,int)   ~:519
 *   getVertexBrightness(int,int,int,int,float,float,float,float) ~:539
 * Both are pure bit/float math on packed brightness ints; no instance state read.
 * Driver: each input line = "br1 br2 br3 br4 f1hex f2hex f3hex f4hex" where the 4 ints are
 * decimal (signed int32) and the 4 floats are raw IEEE-754 bits in hex (so both sides read
 * the exact same float). Prints "<getAoBrightness> <getVertexBrightness>" (two decimal ints). */
import java.io.BufferedReader;
import java.io.InputStreamReader;

public class Golden {
    // verbatim from BlockModelRenderer
    private static int getAoBrightness(int br1, int br2, int br3, int br4) {
        if (br1 == 0) {
            br1 = br4;
        }

        if (br2 == 0) {
            br2 = br4;
        }

        if (br3 == 0) {
            br3 = br4;
        }

        return br1 + br2 + br3 + br4 >> 2 & 16711935;
    }

    private static int getVertexBrightness(int p_178203_1_, int p_178203_2_, int p_178203_3_, int p_178203_4_, float p_178203_5_, float p_178203_6_, float p_178203_7_, float p_178203_8_) {
        int i = (int)((float)(p_178203_1_ >> 16 & 255) * p_178203_5_ + (float)(p_178203_2_ >> 16 & 255) * p_178203_6_ + (float)(p_178203_3_ >> 16 & 255) * p_178203_7_ + (float)(p_178203_4_ >> 16 & 255) * p_178203_8_) & 255;
        int j = (int)((float)(p_178203_1_ & 255) * p_178203_5_ + (float)(p_178203_2_ & 255) * p_178203_6_ + (float)(p_178203_3_ & 255) * p_178203_7_ + (float)(p_178203_4_ & 255) * p_178203_8_) & 255;
        return i << 16 | j;
    }

    public static void main(String[] args) throws Exception {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = br.readLine()) != null) {
            if (line.isEmpty()) continue;
            String[] t = line.trim().split("\\s+");
            int b1 = Integer.parseInt(t[0]);
            int b2 = Integer.parseInt(t[1]);
            int b3 = Integer.parseInt(t[2]);
            int b4 = Integer.parseInt(t[3]);
            float f1 = Float.intBitsToFloat((int) Long.parseLong(t[4], 16));
            float f2 = Float.intBitsToFloat((int) Long.parseLong(t[5], 16));
            float f3 = Float.intBitsToFloat((int) Long.parseLong(t[6], 16));
            float f4 = Float.intBitsToFloat((int) Long.parseLong(t[7], 16));
            int ao = getAoBrightness(b1, b2, b3, b4);
            int vb = getVertexBrightness(b1, b2, b3, b4, f1, f2, f3, f4);
            sb.append(ao).append(' ').append(vb).append('\n');
        }
        System.out.print(sb);
    }
}
