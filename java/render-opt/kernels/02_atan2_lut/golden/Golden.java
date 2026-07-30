// GOLDEN: verbatim decompiled Minecraft 1.11.2 source. DO NOT "clean up" or optimize.
// Source: src/net/minecraft/util/math/MathHelper.java
//   atan2() (~475), fastInvSqrt() (~539, called by atan2), and the FRAC_BIAS / ASINE_TAB / COS_TAB
//   static initializer (~621).
// atan2 signature is atan2(double y, double x): d0 = x*x + y*y, result reflects on sign of x/y.
// Reads "y x" (two doubles) per line; prints the decimal value of atan2(y, x).
// (Decimal + tol compare, not raw-bits/bitwise: the asin/cos LUT seed values differ by <=1 ULP
//  between Java StrictMath and the platform libm, so the candidate cannot recompute the table
//  bit-for-bit. See README.)
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

public class Golden {
    // --- verbatim from MathHelper ---
    private static final double FRAC_BIAS;
    private static final double[] ASINE_TAB;
    private static final double[] COS_TAB;

    public static double atan2(double p_181159_0_, double p_181159_2_)
    {
        double d0 = p_181159_2_ * p_181159_2_ + p_181159_0_ * p_181159_0_;

        if (Double.isNaN(d0))
        {
            return Double.NaN;
        }
        else
        {
            boolean flag = p_181159_0_ < 0.0D;

            if (flag)
            {
                p_181159_0_ = -p_181159_0_;
            }

            boolean flag1 = p_181159_2_ < 0.0D;

            if (flag1)
            {
                p_181159_2_ = -p_181159_2_;
            }

            boolean flag2 = p_181159_0_ > p_181159_2_;

            if (flag2)
            {
                double d1 = p_181159_2_;
                p_181159_2_ = p_181159_0_;
                p_181159_0_ = d1;
            }

            double d9 = fastInvSqrt(d0);
            p_181159_2_ = p_181159_2_ * d9;
            p_181159_0_ = p_181159_0_ * d9;
            double d2 = FRAC_BIAS + p_181159_0_;
            int i = (int)Double.doubleToRawLongBits(d2);
            double d3 = ASINE_TAB[i];
            double d4 = COS_TAB[i];
            double d5 = d2 - FRAC_BIAS;
            double d6 = p_181159_0_ * d4 - p_181159_2_ * d5;
            double d7 = (6.0D + d6 * d6) * d6 * 0.16666666666666666D;
            double d8 = d3 + d7;

            if (flag2)
            {
                d8 = (Math.PI / 2D) - d8;
            }

            if (flag1)
            {
                d8 = Math.PI - d8;
            }

            if (flag)
            {
                d8 = -d8;
            }

            return d8;
        }
    }

    public static double fastInvSqrt(double p_181161_0_)
    {
        double d0 = 0.5D * p_181161_0_;
        long i = Double.doubleToRawLongBits(p_181161_0_);
        i = 6910469410427058090L - (i >> 1);
        p_181161_0_ = Double.longBitsToDouble(i);
        p_181161_0_ = p_181161_0_ * (1.5D - d0 * p_181161_0_ * p_181161_0_);
        return p_181161_0_;
    }

    static
    {
        FRAC_BIAS = Double.longBitsToDouble(4805340802404319232L);
        ASINE_TAB = new double[257];
        COS_TAB = new double[257];

        for (int j = 0; j < 257; ++j)
        {
            double d0 = (double)j / 256.0D;
            double d1 = Math.asin(d0);
            COS_TAB[j] = Math.cos(d1);
            ASINE_TAB[j] = d1;
        }
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
            double y = Double.parseDouble(t[0]);
            double x = Double.parseDouble(t[1]);
            sb.append(Double.toString(atan2(y, x))).append('\n');
        }
        System.out.print(sb);
    }
}
