/* Java ground truth for MC 1.11.2 WorldProvider brightness tables and
 * EntityRenderer.updateLightmap across overworld, Nether, and End.
 *
 * This is a dependency-free transcription of the cited vanilla methods. Output
 * floats are raw IEEE-754 bits and the final texel is signed Java ARGB.
 */
public class Golden {
    static float brightness(int dim, int level) {
        float f1 = 1.0F - (float)level / 15.0F;
        if (dim == -1)
            return (1.0F - f1) / (f1 * 3.0F + 1.0F) * 0.9F + 0.1F;
        return (1.0F - f1) / (f1 * 3.0F + 1.0F) * 1.0F + 0.0F;
    }

    static float clamp(float v) {
        if (v > 1.0F) return 1.0F;
        if (v < 0.0F) return 0.0F;
        return v;
    }

    static float finish(float v, float gamma) {
        float inv = 1.0F - v;
        float bright = 1.0F - inv * inv * inv * inv;
        v = v * (1.0F - gamma) + bright * gamma;
        v = v * 0.96F + 0.03F;
        return clamp(v);
    }

    static float[] lightmap(int dim, int sky, int block, float torch, float gamma) {
        float sun = dim == -1 ? 0.2F : 1.0F;
        float f1 = sun * 0.95F + 0.05F;
        float f2 = brightness(dim, sky) * f1;
        float f3 = brightness(dim, block) * (torch * 0.1F + 1.5F);
        float sunMix = sun * 0.65F + 0.35F;
        float f4 = f2 * sunMix;
        float f5 = f2 * sunMix;
        float f6 = f3 * ((f3 * 0.6F + 0.4F) * 0.6F + 0.4F);
        float f7 = f3 * (f3 * f3 * 0.6F + 0.4F);
        float r = (f4 + f3) * 0.96F + 0.03F;
        float g = (f5 + f6) * 0.96F + 0.03F;
        float b = (f2 + f7) * 0.96F + 0.03F;
        if (dim == 1) {
            r = 0.22F + f3 * 0.75F;
            g = 0.28F + f6 * 0.75F;
            b = 0.25F + f7 * 0.75F;
        }
        r = finish(clamp(r), gamma);
        g = finish(clamp(g), gamma);
        b = finish(clamp(b), gamma);
        return new float[] {r, g, b};
    }

    public static void main(String[] args) {
        int[] dims = {-1, 0, 1};
        for (int dim : dims)
            for (int i = 0; i < 16; ++i)
                System.out.println("TABLE " + dim + " " + i + " "
                    + Float.floatToRawIntBits(brightness(dim, i)));

        for (int dim : dims) {
            for (int sky = 0; sky < 16; ++sky) {
                for (int block = 0; block < 16; ++block) {
                    float[] c = lightmap(dim, sky, block, 0.0F, 0.0F);
                    int r = (int)(c[0] * 255.0F);
                    int g = (int)(c[1] * 255.0F);
                    int b = (int)(c[2] * 255.0F);
                    int argb = 0xff000000 | r << 16 | g << 8 | b;
                    System.out.println("RGB " + dim + " " + sky + " " + block + " "
                        + Float.floatToRawIntBits(c[0]) + " "
                        + Float.floatToRawIntBits(c[1]) + " "
                        + Float.floatToRawIntBits(c[2]) + " " + argb);
                }
            }
        }
    }
}
