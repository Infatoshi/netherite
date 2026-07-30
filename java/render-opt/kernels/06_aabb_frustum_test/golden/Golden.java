// GOLDEN: verbatim decompiled Minecraft 1.11.2 from
//   src/net/minecraft/client/renderer/culling/ClippingHelper.java
//   isBoxInFrustum() (lines ~22-35) + dot() (lines ~14-17).
// The frustum plane array is taken as INPUT (in the real game it is set by ClippingHelperImpl.init();
// here it is provided directly so this is a pure test). Op order preserved for bit-exactness.
//
// Input (per line): 24 raw-float-bits hex ints (the 6 planes x 4 coeffs), then 6 raw-double-bits
//   hex longs = the AABB (minX, minY, minZ, maxX, maxY, maxZ), matching the call
//   isBoxInFrustum(minX, minY, minZ, maxX, maxY, maxZ).
// Output: "1" if the box is inside the frustum, else "0".
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

public class Golden {
    private static float[][] frustum;

    // --- verbatim from ClippingHelper.dot ---
    private static double dot(float[] p_178624_1_, double p_178624_2_, double p_178624_4_, double p_178624_6_) {
        return (double)p_178624_1_[0] * p_178624_2_ + (double)p_178624_1_[1] * p_178624_4_ + (double)p_178624_1_[2] * p_178624_6_ + (double)p_178624_1_[3];
    }

    // --- verbatim from ClippingHelper.isBoxInFrustum ---
    private static boolean isBoxInFrustum(double p_78553_1_, double p_78553_3_, double p_78553_5_, double p_78553_7_, double p_78553_9_, double p_78553_11_) {
        for (int i = 0; i < 6; ++i) {
            float[] afloat = frustum[i];

            if (dot(afloat, p_78553_1_, p_78553_3_, p_78553_5_) <= 0.0D && dot(afloat, p_78553_7_, p_78553_3_, p_78553_5_) <= 0.0D && dot(afloat, p_78553_1_, p_78553_9_, p_78553_5_) <= 0.0D && dot(afloat, p_78553_7_, p_78553_9_, p_78553_5_) <= 0.0D && dot(afloat, p_78553_1_, p_78553_3_, p_78553_11_) <= 0.0D && dot(afloat, p_78553_7_, p_78553_3_, p_78553_11_) <= 0.0D && dot(afloat, p_78553_1_, p_78553_9_, p_78553_11_) <= 0.0D && dot(afloat, p_78553_7_, p_78553_9_, p_78553_11_) <= 0.0D) {
                return false;
            }
        }

        return true;
    }
    // --- /verbatim ---

    public static void main(String[] args) throws IOException {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            String[] tok = line.split("\\s+");
            frustum = new float[6][4];
            for (int i = 0; i < 6; ++i)
                for (int j = 0; j < 4; ++j)
                    frustum[i][j] = Float.intBitsToFloat((int) Long.parseLong(tok[i * 4 + j], 16));
            double[] b = new double[6];
            for (int i = 0; i < 6; ++i)
                b[i] = Double.longBitsToDouble(Long.parseUnsignedLong(tok[24 + i], 16));
            boolean r = isBoxInFrustum(b[0], b[1], b[2], b[3], b[4], b[5]);
            sb.append(r ? '1' : '0').append('\n');
        }
        System.out.print(sb);
    }
}
