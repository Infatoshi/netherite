// GOLDEN: verbatim decompiled Minecraft 1.11.2 source. DO NOT "clean up" or optimize.
// Source: src/net/minecraft/client/renderer/texture/TextureAtlasSprite.java
//   interpolateColor() (lines ~242-245) + the per-pixel assembly from
//   updateAnimationInterpolated() (lines ~226-233).
// Reads records "d0 j1 k1" (one per line) and prints the final interpolated ARGB int.
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

public class Golden {
    // --- verbatim from TextureAtlasSprite ---
    private static int interpolateColor(double p_188535_1_, int p_188535_3_, int p_188535_4_)
    {
        return (int)(p_188535_1_ * (double)p_188535_3_ + (1.0D - p_188535_1_) * (double)p_188535_4_);
    }
    // --- /verbatim ---

    // The per-pixel assembly, copied verbatim from updateAnimationInterpolated()'s inner loop.
    private static int interpPixel(double d0, int j1, int k1) {
        int l1 = interpolateColor(d0, j1 >> 16 & 255, k1 >> 16 & 255);
        int i2 = interpolateColor(d0, j1 >> 8 & 255, k1 >> 8 & 255);
        int j2 = interpolateColor(d0, j1 & 255, k1 & 255);
        return j1 & -16777216 | l1 << 16 | i2 << 8 | j2;
    }

    public static void main(String[] args) throws IOException {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            String[] t = line.split("\\s+");
            double d0 = Double.parseDouble(t[0]);
            int j1 = Integer.parseInt(t[1]);
            int k1 = Integer.parseInt(t[2]);
            sb.append(interpPixel(d0, j1, k1)).append('\n');
        }
        System.out.print(sb);
    }
}
