// Verbatim MC 1.11.2 MathHelper sin/cos/floor + SIN_TABLE static init
// (net/minecraft/util/math/MathHelper.java:29 sin, :37 cos, :73 floor(double), :615 SIN_TABLE).
// Real MC code only - the vanilla ground truth for the table-based trig + floor port. Sweeps the
// SAME inputs as cpu/mathhelper.c and prints the SAME format: sin/cos as %08x of floatToRawIntBits,
// floor as a decimal int.
public class Golden {
    // --- verbatim from MathHelper ---
    private static final float[] SIN_TABLE = new float[65536];

    public static float sin(float value)
    {
        return SIN_TABLE[(int)(value * 10430.378F) & 65535];
    }

    public static float cos(float value)
    {
        return SIN_TABLE[(int)(value * 10430.378F + 16384.0F) & 65535];
    }

    public static int floor(double value)
    {
        int i = (int)value;
        return value < (double)i ? i - 1 : i;
    }

    static
    {
        for (int i = 0; i < 65536; ++i)
        {
            SIN_TABLE[i] = (float)Math.sin((double)i * Math.PI * 2.0D / 65536.0D);
        }
    }
    // --- /verbatim ---

    static final int NT = 12567;
    static final int NF = 2001;

    public static void main(String[] args) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < NT; ++i) {
            float v = (float)((i - 6283) * 0.01);
            sb.append(String.format("%08x%n", Float.floatToRawIntBits(sin(v))));
            sb.append(String.format("%08x%n", Float.floatToRawIntBits(cos(v))));
        }
        for (int i = 0; i < NF; ++i) {
            double d = (i - 1000) * 0.123;
            sb.append(floor(d)).append(System.lineSeparator());
        }
        System.out.print(sb);
    }
}
