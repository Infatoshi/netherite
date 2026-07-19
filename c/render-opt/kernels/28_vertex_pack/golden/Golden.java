/* GOLDEN: verbatim decompiled MC VertexBuffer quad-pack path.
 * Source: src/net/minecraft/client/renderer/VertexBuffer.java
 *   addVertexData(int[])              ~:434
 *   putBrightness4(int,int,int,int)   ~:262
 *   getColorIndex(int)                ~:294
 *   putColorMultiplier(float,float,float,int) ~:300
 *
 * The three method bodies are copied verbatim. The only change is that the
 * this.vertexFormat.getX() calls are replaced by the concrete constants of
 * DefaultVertexFormats.BLOCK (POSITION_3F + COLOR_4UB + TEX_2F + TEX_2S):
 *   getIntegerSize()    = 7   (28 bytes / 4)
 *   getNextOffset()     = 28  (bytes per vertex)
 *   getColorOffset()    = 12  (byte offset of COLOR_4UB)
 *   getUvOffsetById(1)  = 24  (byte offset of TEX_2S lightmap)
 * rawIntBuffer is a real direct ByteBuffer (nativeOrder) IntBuffer view, so
 * ByteOrder.nativeOrder() in putColorMultiplier is genuinely queried.
 *
 * Driver: one record per line:
 *   28 decimal int32 (quad vertex data) | 4 decimal int32 (brightness) | 4 hex float-bits (color mult)
 * The 4 color multipliers are applied as putColorMultiplier(c,c,c, vertexIndex) for
 * vertexIndex 4,3,2,1 (c = colorMult[0..3]) -> matches BlockModelRenderer's no-tint path.
 * Output: the 28 resulting ints (decimal, space-separated). */
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.IntBuffer;

public class Golden {
    // DefaultVertexFormats.BLOCK constants
    static final int INTEGER_SIZE = 7;
    static final int NEXT_OFFSET = 28;
    static final int COLOR_OFFSET = 12;
    static final int UV_OFFSET_1 = 24;

    IntBuffer rawIntBuffer;
    int vertexCount;
    boolean noColor = false;

    private int getBufferSize() {
        return this.vertexCount * INTEGER_SIZE;
    }

    // --- verbatim addVertexData (growBuffer elided; buffer is pre-sized) ---
    public void addVertexData(int[] vertexData) {
        this.rawIntBuffer.position(this.getBufferSize());
        this.rawIntBuffer.put(vertexData);
        this.vertexCount += vertexData.length / INTEGER_SIZE;
    }

    // --- verbatim putBrightness4 ---
    public void putBrightness4(int p_178962_1_, int p_178962_2_, int p_178962_3_, int p_178962_4_) {
        int i = (this.vertexCount - 4) * INTEGER_SIZE + UV_OFFSET_1 / 4;
        int j = NEXT_OFFSET >> 2;
        this.rawIntBuffer.put(i, p_178962_1_);
        this.rawIntBuffer.put(i + j, p_178962_2_);
        this.rawIntBuffer.put(i + j * 2, p_178962_3_);
        this.rawIntBuffer.put(i + j * 3, p_178962_4_);
    }

    // --- verbatim getColorIndex ---
    public int getColorIndex(int vertexIndex) {
        return ((this.vertexCount - vertexIndex) * NEXT_OFFSET + COLOR_OFFSET) / 4;
    }

    // --- verbatim putColorMultiplier ---
    public void putColorMultiplier(float red, float green, float blue, int vertexIndex) {
        int i = this.getColorIndex(vertexIndex);
        int j = -1;

        if (!this.noColor) {
            j = this.rawIntBuffer.get(i);

            if (ByteOrder.nativeOrder() == ByteOrder.LITTLE_ENDIAN) {
                int k = (int)((float)(j & 255) * red);
                int l = (int)((float)(j >> 8 & 255) * green);
                int i1 = (int)((float)(j >> 16 & 255) * blue);
                j = j & -16777216;
                j = j | i1 << 16 | l << 8 | k;
            } else {
                int j1 = (int)((float)(j >> 24 & 255) * red);
                int k1 = (int)((float)(j >> 16 & 255) * green);
                int l1 = (int)((float)(j >> 8 & 255) * blue);
                j = j & 255;
                j = j | j1 << 24 | k1 << 16 | l1 << 8;
            }
        }

        this.rawIntBuffer.put(i, j);
    }
    // --- /verbatim ---

    public static void main(String[] args) throws Exception {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = br.readLine()) != null) {
            if (line.trim().isEmpty()) continue;
            String[] t = line.trim().split("\\s+");
            int[] data = new int[28];
            for (int n = 0; n < 28; ++n) data[n] = Integer.parseInt(t[n]);
            int[] bright = new int[4];
            for (int n = 0; n < 4; ++n) bright[n] = Integer.parseInt(t[28 + n]);
            float[] cmul = new float[4];
            for (int n = 0; n < 4; ++n) cmul[n] = Float.intBitsToFloat((int) Long.parseLong(t[32 + n], 16));

            Golden vb = new Golden();
            vb.rawIntBuffer = ByteBuffer.allocateDirect(28 * 4).order(ByteOrder.nativeOrder()).asIntBuffer();
            vb.vertexCount = 0;

            vb.addVertexData(data);
            vb.putBrightness4(bright[0], bright[1], bright[2], bright[3]);
            vb.putColorMultiplier(cmul[0], cmul[0], cmul[0], 4);
            vb.putColorMultiplier(cmul[1], cmul[1], cmul[1], 3);
            vb.putColorMultiplier(cmul[2], cmul[2], cmul[2], 2);
            vb.putColorMultiplier(cmul[3], cmul[3], cmul[3], 1);

            int[] out = new int[28];
            vb.rawIntBuffer.position(0);
            vb.rawIntBuffer.get(out);
            for (int n = 0; n < 28; ++n) {
                sb.append(out[n]);
                sb.append(n == 27 ? '\n' : ' ');
            }
        }
        System.out.print(sb);
    }
}
