/* GOLDEN: verbatim decompiled MC VertexBuffer translucent sort.
 * Source: src/net/minecraft/client/renderer/VertexBuffer.java
 *   sortVertexData(float,float,float) ~:69   (distance + Arrays.sort of Integer[] indices)
 *   getDistanceSq(FloatBuffer,...)    ~:148   (quad centroid -> camera, squared)
 *
 * getDistanceSq is copied verbatim (reading from a float[] instead of a FloatBuffer; .get(idx)
 * -> arr[idx], identical). The sort is the verbatim Comparator from sortVertexData, except
 * com.google.common.primitives.Floats.compare is replaced by Float.compare -- Guava's Floats.compare
 * delegates to Float.compare, so this is bit-faithful and avoids the Guava classpath.
 * Arrays.sort(Integer[], cmp) is a stable TimSort, so equal distances keep ascending original index.
 *
 * Vertex format = DefaultVertexFormats.BLOCK: getIntegerSize()=7 (float stride per vertex),
 * and the per-quad base index passed in sortVertexData is j*getNextOffset()=j*28 (28 ints/quad).
 *
 * Driver: line 1 = "n camXbits camYbits camZbits" (n quads; camera as hex float-bits).
 * Then n lines, each 28 hex int32 = the quad's raw vertex ints (only the 3 position floats of
 * each vertex, at +0/+1/+2 within the 7-int vertex, affect the sort).
 * Output: the sorted permutation, one quad index per line (descending distance, stable). */
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.Arrays;
import java.util.Comparator;

public class Golden {
    // --- verbatim getDistanceSq (FloatBuffer -> float[]) ---
    private static float getDistanceSq(float[] p_181665_0_, float p_181665_1_, float p_181665_2_, float p_181665_3_, int p_181665_4_, int p_181665_5_) {
        float f = p_181665_0_[p_181665_5_ + p_181665_4_ * 0 + 0];
        float f1 = p_181665_0_[p_181665_5_ + p_181665_4_ * 0 + 1];
        float f2 = p_181665_0_[p_181665_5_ + p_181665_4_ * 0 + 2];
        float f3 = p_181665_0_[p_181665_5_ + p_181665_4_ * 1 + 0];
        float f4 = p_181665_0_[p_181665_5_ + p_181665_4_ * 1 + 1];
        float f5 = p_181665_0_[p_181665_5_ + p_181665_4_ * 1 + 2];
        float f6 = p_181665_0_[p_181665_5_ + p_181665_4_ * 2 + 0];
        float f7 = p_181665_0_[p_181665_5_ + p_181665_4_ * 2 + 1];
        float f8 = p_181665_0_[p_181665_5_ + p_181665_4_ * 2 + 2];
        float f9 = p_181665_0_[p_181665_5_ + p_181665_4_ * 3 + 0];
        float f10 = p_181665_0_[p_181665_5_ + p_181665_4_ * 3 + 1];
        float f11 = p_181665_0_[p_181665_5_ + p_181665_4_ * 3 + 2];
        float f12 = (f + f3 + f6 + f9) * 0.25F - p_181665_1_;
        float f13 = (f1 + f4 + f7 + f10) * 0.25F - p_181665_2_;
        float f14 = (f2 + f5 + f8 + f11) * 0.25F - p_181665_3_;
        return f12 * f12 + f13 * f13 + f14 * f14;
    }
    // --- /verbatim ---

    public static void main(String[] args) throws Exception {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        String header = br.readLine();
        String[] h = header.trim().split("\\s+");
        final int i = Integer.parseInt(h[0]);
        float camX = Float.intBitsToFloat((int) Long.parseLong(h[1], 16));
        float camY = Float.intBitsToFloat((int) Long.parseLong(h[2], 16));
        float camZ = Float.intBitsToFloat((int) Long.parseLong(h[3], 16));

        float[] floatbuf = new float[i * 28];
        for (int q = 0; q < i; ++q) {
            String[] t = br.readLine().trim().split("\\s+");
            for (int n = 0; n < 28; ++n) {
                floatbuf[q * 28 + n] = Float.intBitsToFloat((int) Long.parseLong(t[n], 16));
            }
        }

        // verbatim sortVertexData body (distance fill + index sort)
        final float[] afloat = new float[i];
        for (int j = 0; j < i; ++j) {
            afloat[j] = getDistanceSq(floatbuf, camX, camY, camZ, 7, j * 28);
        }

        Integer[] ainteger = new Integer[i];
        for (int k = 0; k < ainteger.length; ++k) {
            ainteger[k] = Integer.valueOf(k);
        }

        Arrays.sort(ainteger, new Comparator<Integer>() {
            public int compare(Integer p_compare_1_, Integer p_compare_2_) {
                // Guava Floats.compare(a,b) == Float.compare(a,b)
                return Float.compare(afloat[p_compare_2_.intValue()], afloat[p_compare_1_.intValue()]);
            }
        });

        StringBuilder sb = new StringBuilder();
        for (int k = 0; k < i; ++k) sb.append(ainteger[k].intValue()).append('\n');
        System.out.print(sb);
    }
}
