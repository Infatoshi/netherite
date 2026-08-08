// GOLDEN: verbatim decompiled Minecraft 1.11.2 math from
//   src/net/minecraft/client/renderer/culling/ClippingHelperImpl.java  init() (lines ~36-101)
//   + normalize() (lines ~27-34).
// The real init() reads the GL matrices via GlStateManager (impure). Here we isolate the PURE
// math core: the projection + modelview matrices are taken as INPUTS instead of read from GL.
// Everything from the clippingMatrix multiply through the 6 frustum-plane extractions and the
// normalize(...) calls is copied VERBATIM (operation order preserved for bit-exactness).
// MathHelper.sqrt(float) is (float)Math.sqrt((double)value) (src MathHelper.java:42-45), inlined here.
//
// Input: one record per line = 32 space-separated 8-hex-digit ints (raw float bits):
//   16 projection-matrix floats, then 16 modelview-matrix floats.
// Output: 24 space-separated hex ints = raw bits of frustum[0..5][0..3] (normalized plane coeffs).
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

public class Golden {
    // --- verbatim from ClippingHelperImpl.normalize ---
    private static void normalize(float[] p_180547_1_) {
        float f = sqrt(p_180547_1_[0] * p_180547_1_[0] + p_180547_1_[1] * p_180547_1_[1] + p_180547_1_[2] * p_180547_1_[2]);
        p_180547_1_[0] /= f;
        p_180547_1_[1] /= f;
        p_180547_1_[2] /= f;
        p_180547_1_[3] /= f;
    }
    // MathHelper.sqrt(float) verbatim
    private static float sqrt(float value) {
        return (float)Math.sqrt((double)value);
    }

    // --- verbatim core of ClippingHelperImpl.init(), with projectionMatrix/modelviewMatrix as inputs ---
    private static float[] extract(float[] projectionMatrix, float[] modelviewMatrix) {
        float[][] frustum = new float[6][4];
        float[] clippingMatrix = new float[16];
        float[] afloat = projectionMatrix;
        float[] afloat1 = modelviewMatrix;
        clippingMatrix[0] = afloat1[0] * afloat[0] + afloat1[1] * afloat[4] + afloat1[2] * afloat[8] + afloat1[3] * afloat[12];
        clippingMatrix[1] = afloat1[0] * afloat[1] + afloat1[1] * afloat[5] + afloat1[2] * afloat[9] + afloat1[3] * afloat[13];
        clippingMatrix[2] = afloat1[0] * afloat[2] + afloat1[1] * afloat[6] + afloat1[2] * afloat[10] + afloat1[3] * afloat[14];
        clippingMatrix[3] = afloat1[0] * afloat[3] + afloat1[1] * afloat[7] + afloat1[2] * afloat[11] + afloat1[3] * afloat[15];
        clippingMatrix[4] = afloat1[4] * afloat[0] + afloat1[5] * afloat[4] + afloat1[6] * afloat[8] + afloat1[7] * afloat[12];
        clippingMatrix[5] = afloat1[4] * afloat[1] + afloat1[5] * afloat[5] + afloat1[6] * afloat[9] + afloat1[7] * afloat[13];
        clippingMatrix[6] = afloat1[4] * afloat[2] + afloat1[5] * afloat[6] + afloat1[6] * afloat[10] + afloat1[7] * afloat[14];
        clippingMatrix[7] = afloat1[4] * afloat[3] + afloat1[5] * afloat[7] + afloat1[6] * afloat[11] + afloat1[7] * afloat[15];
        clippingMatrix[8] = afloat1[8] * afloat[0] + afloat1[9] * afloat[4] + afloat1[10] * afloat[8] + afloat1[11] * afloat[12];
        clippingMatrix[9] = afloat1[8] * afloat[1] + afloat1[9] * afloat[5] + afloat1[10] * afloat[9] + afloat1[11] * afloat[13];
        clippingMatrix[10] = afloat1[8] * afloat[2] + afloat1[9] * afloat[6] + afloat1[10] * afloat[10] + afloat1[11] * afloat[14];
        clippingMatrix[11] = afloat1[8] * afloat[3] + afloat1[9] * afloat[7] + afloat1[10] * afloat[11] + afloat1[11] * afloat[15];
        clippingMatrix[12] = afloat1[12] * afloat[0] + afloat1[13] * afloat[4] + afloat1[14] * afloat[8] + afloat1[15] * afloat[12];
        clippingMatrix[13] = afloat1[12] * afloat[1] + afloat1[13] * afloat[5] + afloat1[14] * afloat[9] + afloat1[15] * afloat[13];
        clippingMatrix[14] = afloat1[12] * afloat[2] + afloat1[13] * afloat[6] + afloat1[14] * afloat[10] + afloat1[15] * afloat[14];
        clippingMatrix[15] = afloat1[12] * afloat[3] + afloat1[13] * afloat[7] + afloat1[14] * afloat[11] + afloat1[15] * afloat[15];
        float[] afloat2 = frustum[0];
        afloat2[0] = clippingMatrix[3] - clippingMatrix[0];
        afloat2[1] = clippingMatrix[7] - clippingMatrix[4];
        afloat2[2] = clippingMatrix[11] - clippingMatrix[8];
        afloat2[3] = clippingMatrix[15] - clippingMatrix[12];
        normalize(afloat2);
        float[] afloat3 = frustum[1];
        afloat3[0] = clippingMatrix[3] + clippingMatrix[0];
        afloat3[1] = clippingMatrix[7] + clippingMatrix[4];
        afloat3[2] = clippingMatrix[11] + clippingMatrix[8];
        afloat3[3] = clippingMatrix[15] + clippingMatrix[12];
        normalize(afloat3);
        float[] afloat4 = frustum[2];
        afloat4[0] = clippingMatrix[3] + clippingMatrix[1];
        afloat4[1] = clippingMatrix[7] + clippingMatrix[5];
        afloat4[2] = clippingMatrix[11] + clippingMatrix[9];
        afloat4[3] = clippingMatrix[15] + clippingMatrix[13];
        normalize(afloat4);
        float[] afloat5 = frustum[3];
        afloat5[0] = clippingMatrix[3] - clippingMatrix[1];
        afloat5[1] = clippingMatrix[7] - clippingMatrix[5];
        afloat5[2] = clippingMatrix[11] - clippingMatrix[9];
        afloat5[3] = clippingMatrix[15] - clippingMatrix[13];
        normalize(afloat5);
        float[] afloat6 = frustum[4];
        afloat6[0] = clippingMatrix[3] - clippingMatrix[2];
        afloat6[1] = clippingMatrix[7] - clippingMatrix[6];
        afloat6[2] = clippingMatrix[11] - clippingMatrix[10];
        afloat6[3] = clippingMatrix[15] - clippingMatrix[14];
        normalize(afloat6);
        float[] afloat7 = frustum[5];
        afloat7[0] = clippingMatrix[3] + clippingMatrix[2];
        afloat7[1] = clippingMatrix[7] + clippingMatrix[6];
        afloat7[2] = clippingMatrix[11] + clippingMatrix[10];
        afloat7[3] = clippingMatrix[15] + clippingMatrix[14];
        normalize(afloat7);
        float[] out = new float[24];
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 4; ++j)
                out[i * 4 + j] = frustum[i][j];
        return out;
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
            float[] proj = new float[16];
            float[] mv = new float[16];
            for (int i = 0; i < 16; ++i) proj[i] = Float.intBitsToFloat((int) Long.parseLong(tok[i], 16));
            for (int i = 0; i < 16; ++i) mv[i] = Float.intBitsToFloat((int) Long.parseLong(tok[16 + i], 16));
            float[] out = extract(proj, mv);
            for (int i = 0; i < 24; ++i) {
                if (i > 0) sb.append(' ');
                sb.append(Integer.toHexString(Float.floatToRawIntBits(out[i])));
            }
            sb.append('\n');
        }
        System.out.print(sb);
    }
}
