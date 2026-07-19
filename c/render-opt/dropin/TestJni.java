// Phase A: prove JNI works on this toolchain. Loads libqsin.so, then for sampled
// floats checks nsin(x) is BITWISE-equal to the verified MathHelper SIN_TABLE lookup.
public class TestJni {
    static native float nsin(float value);

    // verbatim from MathHelper / golden Golden.java
    private static final float[] SIN_TABLE = new float[65536];
    static {
        for (int i = 0; i < 65536; ++i)
            SIN_TABLE[i] = (float)Math.sin((double)i * Math.PI * 2.0D / 65536.0D);
    }
    static float javaSin(float value) { return SIN_TABLE[(int)(value * 10430.378F) & 65535]; }

    public static void main(String[] args) {
        System.load(args[0]); // absolute path to libqsin.so
        int n = 0, fail = 0;
        // dense sweep across a wide range incl negatives and large magnitudes
        for (double x = -50.0; x <= 50.0; x += 0.0001) {
            float v = (float)x;
            int a = Float.floatToRawIntBits(nsin(v));
            int b = Float.floatToRawIntBits(javaSin(v));
            n++;
            if (a != b) { fail++; if (fail <= 5) System.out.println("MISMATCH x="+v+" native="+Integer.toHexString(a)+" java="+Integer.toHexString(b)); }
        }
        // edge cases
        float[] edge = {0f, 3.14159265f, 6.2831853f, -3.14159265f, 1e9f, -1e9f, Float.NaN, Float.POSITIVE_INFINITY, Float.NEGATIVE_INFINITY};
        for (float v : edge) {
            int a = Float.floatToRawIntBits(nsin(v));
            int b = Float.floatToRawIntBits(javaSin(v));
            n++;
            if (a != b) { fail++; System.out.println("EDGE MISMATCH x="+v+" native="+Integer.toHexString(a)+" java="+Integer.toHexString(b)); }
        }
        System.out.println("checked="+n+" mismatches="+fail+" -> "+(fail==0?"PASS":"FAIL"));
    }
}
