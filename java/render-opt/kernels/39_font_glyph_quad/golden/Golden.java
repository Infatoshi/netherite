// GOLDEN: verbatim-logic port of MC 1.11.2 FontRenderer.renderDefaultChar().
// Source: src/net/minecraft/client/gui/FontRenderer.java:230 renderDefaultChar(int ch, boolean italic).
// The glyph-layout math is copied UNCHANGED. The only adaptations remove the GL/texture deps that
// do not affect the computed quad:
//   - bindTexture(...) dropped (texture binding, no geometry effect)
//   - this.charWidth[ch] -> an `int l` input param (the per-char width; pure input)
//   - this.posX / this.posY -> float input params
//   - GlStateManager.glBegin/glEnd dropped; glTexCoord2f / glVertex3f -> print the (u,v) and (x,y,z).
// Input  (per line): ch + l(charWidth) + posX(hex float-bits) + posY(hex float-bits) + italic(0/1)
// Output: one vertex per line = 5 hex floats (raw bits of u v x y z). 4 verts per char.
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

public class Golden {
    static StringBuilder sb = new StringBuilder();

    public static void main(String[] args) throws IOException {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            String[] tok = line.split("\\s+");
            if (tok.length != 5) continue;
            int ch = Integer.parseInt(tok[0]);
            int charWidth = Integer.parseInt(tok[1]);
            float posX = Float.intBitsToFloat((int) Long.parseLong(tok[2], 16));
            float posY = Float.intBitsToFloat((int) Long.parseLong(tok[3], 16));
            boolean italic = Integer.parseInt(tok[4]) != 0;
            renderDefaultChar(ch, charWidth, posX, posY, italic);
        }
        System.out.print(sb);
    }

    // --- verbatim from renderDefaultChar() (charWidth[ch]->l param, posX/posY params, GL emit -> print) ---
    private static float renderDefaultChar(int ch, int charWidthCh, float posX, float posY, boolean italic) {
        int i = ch % 16 * 8;
        int j = ch / 16 * 8;
        int k = italic ? 1 : 0;
        int l = charWidthCh;
        float f = (float) l - 0.01F;
        emit((float) i / 128.0F, (float) j / 128.0F, posX + (float) k, posY, 0.0F);
        emit((float) i / 128.0F, ((float) j + 7.99F) / 128.0F, posX - (float) k, posY + 7.99F, 0.0F);
        emit(((float) i + f - 1.0F) / 128.0F, (float) j / 128.0F, posX + f - 1.0F + (float) k, posY, 0.0F);
        emit(((float) i + f - 1.0F) / 128.0F, ((float) j + 7.99F) / 128.0F, posX + f - 1.0F - (float) k, posY + 7.99F, 0.0F);
        return (float) l;
    }
    // --- /verbatim ---

    private static void emit(float u, float v, float x, float y, float z) {
        sb.append(Long.toHexString(Float.floatToRawIntBits(u) & 0xFFFFFFFFL)).append(' ');
        sb.append(Long.toHexString(Float.floatToRawIntBits(v) & 0xFFFFFFFFL)).append(' ');
        sb.append(Long.toHexString(Float.floatToRawIntBits(x) & 0xFFFFFFFFL)).append(' ');
        sb.append(Long.toHexString(Float.floatToRawIntBits(y) & 0xFFFFFFFFL)).append(' ');
        sb.append(Long.toHexString(Float.floatToRawIntBits(z) & 0xFFFFFFFFL)).append('\n');
    }
}
