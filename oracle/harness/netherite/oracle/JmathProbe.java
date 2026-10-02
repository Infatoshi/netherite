package netherite.oracle;

import java.io.File;
import java.io.PrintWriter;

import net.minecraft.util.MathHelper;

/**
 * MathHelper's floors and the placement facings built on them, at the inputs
 * an unbounded agent can reach (huge, NaN and infinite), the reference for
 * csrc/engine/jfloor.h (csrc/tests/test_jmath.c):
 *   make -C oracle jmath-probe DIR=../out/java/jmath/NAME
 * writes DIR/values.txt, one line per case: kind, the input's raw bits (hex),
 * the answer (decimal int, or a float's raw bits in hex for sin and cos).
 * Kinds: fd floor_double(d); ff floor_float(f); and over a float yaw y, the
 * vanilla call shapes q4 (& 3 of floor_double(y * 4 / 360 + 0.5), furnaces,
 * stairs, chests, fence gates), q4s (+ 2.5, skulls and pumpkins), q4d
 * ((y + 180) * 4 / 360 - 0.5, doors), q16 (y * 16 / 360 + 0.5, skulls), q16s
 * ((y + 180) * 16 / 360 + 0.5, signs), sin and cos (MathHelper.sin and cos
 * of y * pi / 180, the look vector's shape).
 */
public class JmathProbe
{
    public static void main(String[] args) throws Exception
    {
        File dir = new File(args[0]);
        dir.mkdirs();
        double[] ds = {0.0D, -0.0D, 0.5D, -0.5D, 1.0D, -1.0D, 2.5D, -2.5D, 1e10D, -1e10D, 3e11D, -3e11D,
            2147483646.5D, 2147483647.0D, 2147483647.5D, 2147483648.0D, -2147483647.5D, -2147483648.0D,
            -2147483648.5D, -2147483649.0D, 9.2e18D, -9.2e18D, Double.MAX_VALUE, -Double.MAX_VALUE,
            Double.MIN_VALUE, -Double.MIN_VALUE, Double.NaN, Double.POSITIVE_INFINITY, Double.NEGATIVE_INFINITY};
        float[] fs = {0.0F, -0.0F, 0.5F, -0.5F, 1.0F, -1.0F, 179.3F, -179.3F, 1.7e7F, -1.7e7F, 1e10F, -1e10F,
            3e11F, -3e11F, 2147483520.0F, 2147483648.0F, -2147483648.0F, -2147483904.0F, Float.MAX_VALUE,
            -Float.MAX_VALUE, Float.MIN_VALUE, Float.NaN, Float.POSITIVE_INFINITY, Float.NEGATIVE_INFINITY};
        PrintWriter w = new PrintWriter(new File(dir, "values.txt"), "UTF-8");
        for (double d : ds) w.println("fd " + Long.toHexString(Double.doubleToRawLongBits(d)) + " " + MathHelper.floor_double(d));
        for (float f : fs) w.println("ff " + Integer.toHexString(Float.floatToRawIntBits(f)) + " " + MathHelper.floor_float(f));
        for (float y : fs)
        {
            String b = Integer.toHexString(Float.floatToRawIntBits(y));
            w.println("q4 " + b + " " + (MathHelper.floor_double((double)(y * 4.0F / 360.0F) + 0.5D) & 3));
            w.println("q4s " + b + " " + (MathHelper.floor_double((double)(y * 4.0F / 360.0F) + 2.5D) & 3));
            w.println("q4d " + b + " " + (MathHelper.floor_double((double)((y + 180.0F) * 4.0F / 360.0F) - 0.5D) & 3));
            w.println("q16 " + b + " " + (MathHelper.floor_double((double)(y * 16.0F / 360.0F) + 0.5D) & 15));
            w.println("q16s " + b + " " + (MathHelper.floor_double((double)((y + 180.0F) * 16.0F / 360.0F) + 0.5D) & 15));
            w.println("sin " + b + " " + Integer.toHexString(Float.floatToRawIntBits(MathHelper.sin(y * (float)Math.PI / 180.0F))));
            w.println("cos " + b + " " + Integer.toHexString(Float.floatToRawIntBits(MathHelper.cos(y * (float)Math.PI / 180.0F))));
        }
        w.close();
        PrintWriter m = new PrintWriter(new File(dir, "manifest.json"), "UTF-8");
        m.println("{\"kind\":\"jmath\",\"cases\":" + (ds.length + fs.length * 8) + ",\"file\":\"values.txt\"}");
        m.close();
        System.out.println("ORACLE JMATH " + (ds.length + fs.length * 8) + " cases to " + dir);
    }
}
