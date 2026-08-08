/* Java ground truth for MC 1.11.2 WorldProvider brightness tables and
 * EntityRenderer.updateLightmap across overworld, Nether, and End.
 *
 * This is a dependency-free transcription of the cited vanilla methods. Output
 * floats are raw IEEE-754 bits and the final texel is signed Java ARGB.
 */
public class Golden {
    static final float[] SIN_TABLE = new float[65536];

    static {
        for (int i = 0; i < SIN_TABLE.length; ++i)
            SIN_TABLE[i] = (float)Math.sin((double)i * Math.PI * 2.0D / 65536.0D);
    }

    static float mcSin(float value) {
        return SIN_TABLE[(int)(value * 10430.378F) & 65535];
    }

    static float nightVisionBrightness(int duration, float partialTicks) {
        if (duration <= 0) return 0.0F;
        return duration > 200 ? 1.0F
            : 0.7F + mcSin(((float)duration - partialTicks)
                * (float)Math.PI * 0.2F) * 0.3F;
    }

    static float blindnessFogEnd(int duration, float farPlane) {
        if (duration <= 0) return farPlane;
        float f1 = 5.0F;
        if (duration < 20)
            f1 = 5.0F + (farPlane - 5.0F)
                * (1.0F - (float)duration / 20.0F);
        return f1;
    }

    static float[] voidBlindness(float r, float g, float b, int duration,
                                 double feetY, double voidFactor) {
        double d1 = feetY * voidFactor;
        if (duration > 0) {
            if (duration < 20)
                d1 *= (double)(1.0F - (float)duration / 20.0F);
            else
                d1 = 0.0D;
        }
        if (d1 < 1.0D) {
            if (d1 < 0.0D) d1 = 0.0D;
            d1 = d1 * d1;
            r = (float)((double)r * d1);
            g = (float)((double)g * d1);
            b = (float)((double)b * d1);
        }
        return new float[] {r, g, b};
    }

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

    static float[] lightmap(int dim, int sky, int block, float torch,
                            float gamma, float nightVision) {
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
        if (nightVision > 0.0F) {
            float scale = 1.0F / r;
            if (scale > 1.0F / g) scale = 1.0F / g;
            if (scale > 1.0F / b) scale = 1.0F / b;
            r = r * (1.0F - nightVision) + r * scale * nightVision;
            g = g * (1.0F - nightVision) + g * scale * nightVision;
            b = b * (1.0F - nightVision) + b * scale * nightVision;
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
                    float[] c = lightmap(dim, sky, block, 0.0F, 0.0F, 0.0F);
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

        for (int dim : dims) {
            for (int sky = 0; sky < 16; ++sky) {
                for (int block = 0; block < 16; ++block) {
                    float[] c = lightmap(dim, sky, block, 0.0F, 0.0F, 1.0F);
                    int r = (int)(c[0] * 255.0F);
                    int g = (int)(c[1] * 255.0F);
                    int b = (int)(c[2] * 255.0F);
                    int argb = 0xff000000 | r << 16 | g << 8 | b;
                    System.out.println("NVRGB " + dim + " " + sky + " " + block + " "
                        + Float.floatToRawIntBits(c[0]) + " "
                        + Float.floatToRawIntBits(c[1]) + " "
                        + Float.floatToRawIntBits(c[2]) + " " + argb);
                }
            }
        }

        int[] durations = {1000, 201, 200, 199, 181, 21, 20, 19, 2, 1};
        for (int duration : durations) {
            float amount = nightVisionBrightness(duration, 1.0F);
            System.out.println("NVAMOUNT " + duration + " "
                + Float.floatToRawIntBits(amount));
        }

        int[] blindDurations = {0, 1, 2, 19, 20, 21, 200};
        for (int duration : blindDurations) {
            float fogEnd = blindnessFogEnd(duration, 128.0F);
            System.out.println("BLINDFAR " + duration + " "
                + Float.floatToRawIntBits(fogEnd));
        }

        float[][] fogBases = {
            {0.2F, 0.03F, 0.03F},
            {0.02F, 0.02F, 0.2F},
            {0.6F, 0.1F, 0.0F}
        };
        double[] feetYs = {-4.0D, 0.0D, 4.0D, 16.0D, 31.5D, 32.0D, 64.0D};
        double[] voidFactors = {1.0D, 0.03125D};
        int[] colorDurations = {0, 1, 19, 20, 21};
        for (int base = 0; base < fogBases.length; ++base)
            for (int yi = 0; yi < feetYs.length; ++yi)
                for (int factor = 0; factor < voidFactors.length; ++factor)
                    for (int duration : colorDurations) {
                        float[] c = voidBlindness(
                            fogBases[base][0], fogBases[base][1],
                            fogBases[base][2], duration, feetYs[yi],
                            voidFactors[factor]);
                        System.out.println("BLINDRGB " + base + " " + yi
                            + " " + factor + " " + duration + " "
                            + Float.floatToRawIntBits(c[0]) + " "
                            + Float.floatToRawIntBits(c[1]) + " "
                            + Float.floatToRawIntBits(c[2]));
                    }
    }
}
