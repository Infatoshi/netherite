/* CANDIDATE: C port of MC 1.11.2 FontRenderer.renderDefaultChar() glyph quad layout.
 * Must BITWISE-match golden/Golden.java. Op order + cast points preserved; build -ffp-contract=off.
 * Input  (per line): ch + l(charWidth) + posX(hex float-bits) + posY(hex float-bits) + italic(0/1)
 * Output: one vertex per line = 5 hex floats (raw bits of u v x y z). 4 verts per char. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static float f_from_hex(unsigned long b) { uint32_t u = (uint32_t)b; float f; memcpy(&f, &u, sizeof f); return f; }
static unsigned fbits(float f) { uint32_t b; memcpy(&b, &f, sizeof b); return b; }

static void emit(float u, float v, float x, float y, float z) {
    printf("%x %x %x %x %x\n", fbits(u), fbits(v), fbits(x), fbits(y), fbits(z));
}

static void render_default_char(int ch, int charWidthCh, float posX, float posY, int italic) {
    int i = ch % 16 * 8;
    int j = ch / 16 * 8;
    int k = italic ? 1 : 0;
    int l = charWidthCh;
    float f = (float) l - 0.01F;
    emit((float) i / 128.0F, (float) j / 128.0F, posX + (float) k, posY, 0.0F);
    emit((float) i / 128.0F, ((float) j + 7.99F) / 128.0F, posX - (float) k, posY + 7.99F, 0.0F);
    emit(((float) i + f - 1.0F) / 128.0F, (float) j / 128.0F, posX + f - 1.0F + (float) k, posY, 0.0F);
    emit(((float) i + f - 1.0F) / 128.0F, ((float) j + 7.99F) / 128.0F, posX + f - 1.0F - (float) k, posY + 7.99F, 0.0F);
}

int main(void) {
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        int ch, charWidth, italic;
        unsigned long xb, yb;
        if (sscanf(line, "%d %d %lx %lx %d", &ch, &charWidth, &xb, &yb, &italic) != 5) continue;
        render_default_char(ch, charWidth, f_from_hex(xb), f_from_hex(yb), italic);
    }
    return 0;
}
