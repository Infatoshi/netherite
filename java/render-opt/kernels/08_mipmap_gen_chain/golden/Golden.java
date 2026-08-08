// GOLDEN: verbatim decompiled Minecraft 1.11.2 source. DO NOT "clean up" or optimize.
// Source: src/net/minecraft/client/renderer/texture/TextureUtil.java
//   generateMipmapData() (~57), blendColors() (~108), blendColorComponent() (~158),
//   getColorGamma() (~31), and COLOR_GAMMAS init from the static block (~402-407).
// Reads records "width maxLevel <width*width ARGB ints>" and prints every mip-level pixel
// (level 0 = base, then 1..maxLevel), one signed ARGB int per line, all levels concatenated.
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.StreamTokenizer;
import java.util.ArrayList;

public class Golden {
    private static final float[] COLOR_GAMMAS;
    private static final int[] MIPMAP_BUFFER;

    // --- verbatim from TextureUtil ---
    public static int[][] generateMipmapData(int p_147949_0_, int p_147949_1_, int[][] p_147949_2_)
    {
        int[][] aint = new int[p_147949_0_ + 1][];
        aint[0] = p_147949_2_[0];

        if (p_147949_0_ > 0)
        {
            boolean flag = false;

            for (int i = 0; i < p_147949_2_.length; ++i)
            {
                if (p_147949_2_[0][i] >> 24 == 0)
                {
                    flag = true;
                    break;
                }
            }

            for (int l1 = 1; l1 <= p_147949_0_; ++l1)
            {
                if (p_147949_2_[l1] != null)
                {
                    aint[l1] = p_147949_2_[l1];
                }
                else
                {
                    int[] aint1 = aint[l1 - 1];
                    int[] aint2 = new int[aint1.length >> 2];
                    int j = p_147949_1_ >> l1;
                    if (j > 0) { // FORGE: forcing higher mipmap levels on odd textures needs this check
                    int k = aint2.length / j;
                    int l = j << 1;

                    for (int i1 = 0; i1 < j; ++i1)
                    {
                        for (int j1 = 0; j1 < k; ++j1)
                        {
                            int k1 = 2 * (i1 + j1 * l);
                            aint2[i1 + j1 * j] = blendColors(aint1[k1 + 0], aint1[k1 + 1], aint1[k1 + 0 + l], aint1[k1 + 1 + l], flag);
                        }
                    }
                    } // end if (j > 0)

                    aint[l1] = aint2;
                }
            }
        }

        return aint;
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

    private static float getColorGamma(int p_188543_0_)
    {
        return COLOR_GAMMAS[p_188543_0_ & 255];
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
        StreamTokenizer st = new StreamTokenizer(new BufferedReader(new InputStreamReader(System.in)));
        st.resetSyntax();
        st.wordChars('0', '9');
        st.wordChars('-', '-');
        st.whitespaceChars(0, ' ');
        StringBuilder sb = new StringBuilder();
        while (st.nextToken() != StreamTokenizer.TT_EOF) {
            int width = Integer.parseInt(st.sval);
            st.nextToken(); int maxLevel = Integer.parseInt(st.sval);
            int n = width * width;
            int[] base = new int[n];
            for (int p = 0; p < n; ++p) {
                st.nextToken();
                base[p] = Integer.parseInt(st.sval);
            }
            int[][] in = new int[maxLevel + 1][];   // rows 1..maxLevel left null on purpose
            in[0] = base;
            int[][] out = generateMipmapData(maxLevel, width, in);
            for (int lvl = 0; lvl < out.length; ++lvl) {
                int[] a = out[lvl];
                for (int p = 0; p < a.length; ++p) {
                    sb.append(a[p]).append('\n');
                }
            }
        }
        System.out.print(sb);
    }
}
