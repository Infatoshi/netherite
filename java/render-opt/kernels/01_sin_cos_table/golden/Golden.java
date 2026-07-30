// GOLDEN: verbatim decompiled Minecraft 1.11.2 source. DO NOT "clean up" or optimize.
// Source: src/net/minecraft/util/math/MathHelper.java
//   sin() (~29), cos() (~37), and the SIN_TABLE static initializer (~615).
// Reads one float angle per line from stdin; prints two hex tokens per line:
//   floatToRawIntBits(sin(value))  floatToRawIntBits(cos(value))
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

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

    static
    {
        for (int i = 0; i < 65536; ++i)
        {
            SIN_TABLE[i] = (float)Math.sin((double)i * Math.PI * 2.0D / 65536.0D);
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
            float v = Float.parseFloat(line);
            sb.append(Integer.toHexString(Float.floatToRawIntBits(sin(v)))).append(' ')
              .append(Integer.toHexString(Float.floatToRawIntBits(cos(v)))).append('\n');
        }
        System.out.print(sb);
    }
}
