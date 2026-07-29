// GOLDEN: verbatim decompiled Minecraft 1.11.2 source. DO NOT "clean up" or optimize.
// Source: src/net/minecraft/client/renderer/texture/TextureUtil.java
//   blendColors() (~108), blendColorComponent() (~158), getColorGamma() (~31),
//   and the COLOR_GAMMAS table init from the static block (~402-407).
// Reads records "c0 c1 c2 c3 hasTransparency" (one per line) and prints the blended ARGB int.
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

public class Golden {
    private static final float[] COLOR_GAMMAS;
    private static final int[] MIPMAP_BUFFER;

    // --- verbatim from TextureUtil ---
    private static float getColorGamma(int p_188543_0_)
    {
        return COLOR_GAMMAS[p_188543_0_ & 255];
    }

    private static int blendColors(int p_147943_0_, int p_147943_1_, int p_147943_2_, int p_147943_3_, boolean p_147943_4_)
    {
        if (p_147943_4_)
        {
            MIPMAP_BUFFER[0] = p_147943_0_;
            MIPMAP_BUFFER[1] = p_147943_1_;
            MIPMAP_BUFFER[2] = p_147943_2_;
            MIPMAP_BUFFER[3] = p_147943_3_;
            float f = 0.0F;
            float f1 = 0.0F;
            float f2 = 0.0F;
            float f3 = 0.0F;

            for (int i1 = 0; i1 < 4; ++i1)
            {
                if (MIPMAP_BUFFER[i1] >> 24 != 0)
                {
                    f += getColorGamma(MIPMAP_BUFFER[i1] >> 24);
                    f1 += getColorGamma(MIPMAP_BUFFER[i1] >> 16);
                    f2 += getColorGamma(MIPMAP_BUFFER[i1] >> 8);
                    f3 += getColorGamma(MIPMAP_BUFFER[i1] >> 0);
                }
            }

            f = f / 4.0F;
            f1 = f1 / 4.0F;
            f2 = f2 / 4.0F;
            f3 = f3 / 4.0F;
            int i2 = (int)(Math.pow((double)f, 0.45454545454545453D) * 255.0D);
            int j1 = (int)(Math.pow((double)f1, 0.45454545454545453D) * 255.0D);
            int k1 = (int)(Math.pow((double)f2, 0.45454545454545453D) * 255.0D);
            int l1 = (int)(Math.pow((double)f3, 0.45454545454545453D) * 255.0D);

            if (i2 < 96)
            {
                i2 = 0;
            }

            return i2 << 24 | j1 << 16 | k1 << 8 | l1;
        }
        else
        {
            int i = blendColorComponent(p_147943_0_, p_147943_1_, p_147943_2_, p_147943_3_, 24);
            int j = blendColorComponent(p_147943_0_, p_147943_1_, p_147943_2_, p_147943_3_, 16);
            int k = blendColorComponent(p_147943_0_, p_147943_1_, p_147943_2_, p_147943_3_, 8);
            int l = blendColorComponent(p_147943_0_, p_147943_1_, p_147943_2_, p_147943_3_, 0);
            return i << 24 | j << 16 | k << 8 | l;
        }
    }

    private static int blendColorComponent(int p_147944_0_, int p_147944_1_, int p_147944_2_, int p_147944_3_, int p_147944_4_)
    {
        float f = getColorGamma(p_147944_0_ >> p_147944_4_);
        float f1 = getColorGamma(p_147944_1_ >> p_147944_4_);
        float f2 = getColorGamma(p_147944_2_ >> p_147944_4_);
        float f3 = getColorGamma(p_147944_3_ >> p_147944_4_);
        float f4 = (float)((double)((float)Math.pow((double)(f + f1 + f2 + f3) * 0.25D, 0.45454545454545453D)));
        return (int)((double)f4 * 255.0D);
    }
    // --- /verbatim ---

    static {
        // --- verbatim COLOR_GAMMAS init from TextureUtil static block (~402-407) ---
        COLOR_GAMMAS = new float[256];

        for (int i = 0; i < COLOR_GAMMAS.length; ++i)
        {
            COLOR_GAMMAS[i] = (float)Math.pow((double)((float)i / 255.0F), 2.2D);
        }

        MIPMAP_BUFFER = new int[4];
        // --- /verbatim ---
    }

    public static void main(String[] args) throws IOException {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            String[] t = line.split("\\s+");
            int c0 = Integer.parseInt(t[0]);
            int c1 = Integer.parseInt(t[1]);
            int c2 = Integer.parseInt(t[2]);
            int c3 = Integer.parseInt(t[3]);
            boolean ht = Integer.parseInt(t[4]) != 0;
            sb.append(blendColors(c0, c1, c2, c3, ht)).append('\n');
        }
        System.out.print(sb);
    }
}
