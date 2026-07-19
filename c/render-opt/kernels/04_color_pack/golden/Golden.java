// GOLDEN: verbatim decompiled Minecraft 1.11.2 source. DO NOT "clean up" or optimize.
// Source: src/net/minecraft/util/math/MathHelper.java
//   rgb(int,int,int) (~418) and multiplyColor(int,int) (~426).
// Reads one record per line from stdin: five ints "r g b colorA colorB".
// Prints two ints per line (space separated): rgb(r,g,b)  multiplyColor(colorA,colorB).
// Discrete (packed-color) output -> bitwise compare.
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

public class Golden {
    // --- verbatim from MathHelper ---
    public static int rgb(int rIn, int gIn, int bIn)
    {
        int lvt_3_1_ = (rIn << 8) + gIn;
        lvt_3_1_ = (lvt_3_1_ << 8) + bIn;
        return lvt_3_1_;
    }

    public static int multiplyColor(int p_180188_0_, int p_180188_1_)
    {
        int i = (p_180188_0_ & 16711680) >> 16;
        int j = (p_180188_1_ & 16711680) >> 16;
        int k = (p_180188_0_ & 65280) >> 8;
        int l = (p_180188_1_ & 65280) >> 8;
        int i1 = (p_180188_0_ & 255) >> 0;
        int j1 = (p_180188_1_ & 255) >> 0;
        int k1 = (int)((float)i * (float)j / 255.0F);
        int l1 = (int)((float)k * (float)l / 255.0F);
        int i2 = (int)((float)i1 * (float)j1 / 255.0F);
        return p_180188_0_ & -16777216 | k1 << 16 | l1 << 8 | i2;
    }
    // --- /verbatim ---

    public static void main(String[] args) throws IOException {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            String[] t = line.split("\\s+");
            int r = Integer.parseInt(t[0]);
            int g = Integer.parseInt(t[1]);
            int b = Integer.parseInt(t[2]);
            int ca = Integer.parseInt(t[3]);
            int cb = Integer.parseInt(t[4]);
            sb.append(rgb(r, g, b)).append(' ')
              .append(multiplyColor(ca, cb)).append('\n');
        }
        System.out.print(sb);
    }
}
