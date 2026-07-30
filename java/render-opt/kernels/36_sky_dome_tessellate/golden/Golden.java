// GOLDEN: verbatim-logic port of MC 1.11.2 RenderGlobal.renderSky() grid tessellation.
// Source: src/net/minecraft/client/renderer/RenderGlobal.java:347 renderSky(VertexBuffer, float posY, boolean reverseX),
//   the body of generateSky()/generateSky2() (lines ~306/272 call it with posY=16.0F/false and -16.0F/true).
// The loop is copied UNCHANGED. The only adaptation is removing the GL/VertexBuffer dependency:
//   worldRendererIn.begin(...) / .pos(x,y,z).endVertex() -> print the (x,y,z) doubles passed to pos().
// That is exactly the geometry the kernel emits; VertexBuffer's buffer packing is out of scope (GL-bound).
// Input  (per line): posY (hex IEEE-754 float-bits) + reverseX (0/1)
// Output: one vertex per line = 3 hex doubles (raw long-bits of the x,y,z passed to pos()).
//         Each input record emits 13*13*4 = 676 vertices (k,l in {-384..384 step 64}).
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

public class Golden {
    public static void main(String[] args) throws IOException {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            String[] tok = line.split("\\s+");
            if (tok.length != 2) continue;
            float posY = Float.intBitsToFloat((int) Long.parseLong(tok[0], 16));
            boolean reverseX = Integer.parseInt(tok[1]) != 0;

            // --- verbatim loop from renderSky() (GL emit replaced by printing the pos() args) ---
            int i = 64;
            int j = 6;
            for (int k = -384; k <= 384; k += 64) {
                for (int l = -384; l <= 384; l += 64) {
                    float f = (float) k;
                    float f1 = (float) (k + 64);

                    if (reverseX) {
                        f1 = (float) k;
                        f = (float) (k + 64);
                    }

                    emit(sb, (double) f, (double) posY, (double) l);
                    emit(sb, (double) f1, (double) posY, (double) l);
                    emit(sb, (double) f1, (double) posY, (double) (l + 64));
                    emit(sb, (double) f, (double) posY, (double) (l + 64));
                }
            }
            // --- /verbatim ---
        }
        System.out.print(sb);
    }

    private static void emit(StringBuilder sb, double x, double y, double z) {
        sb.append(Long.toHexString(Double.doubleToRawLongBits(x))).append(' ');
        sb.append(Long.toHexString(Double.doubleToRawLongBits(y))).append(' ');
        sb.append(Long.toHexString(Double.doubleToRawLongBits(z))).append('\n');
    }
}
