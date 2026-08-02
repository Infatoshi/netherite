/* CANDIDATE: pure-C port of MC 1.11.2 BlockModelRenderer.renderQuadsFlat()
 *   (src/net/minecraft/client/renderer/BlockModelRenderer.java:238), per single quad, on the
 *   DefaultVertexFormats.BLOCK layout (7 ints/vertex: pos.xyz, color, u, v, lightmap; 28 ints/quad).
 *
 * Golden is CAPTURED FROM REAL MINECRAFT (capture_mode "live-hook"): NetheriteMod 'capture_quadsflat'
 * begins a fresh VertexBuffer, invokes the real private renderQuadsFlat with a one-quad list
 * (ownBrightness=false, brightnessIn precomputed), and dumps the 28 output ints. World-dependent
 * scalars the method reads (brightness, tint color) are snapshot as inputs.
 *
 * Input record (one line, golden/inputs.txt): 36 ints
 *     vd[0..27] brightnessIn hasTint k applyDiff diffuseBits d0bits d1bits d2bits
 *   vd        : bakedquad.getVertexData() copied verbatim by buffer.addVertexData
 *   brightnessIn : packed lightmap int -> buffer.putBrightness4 (all 4 verts)
 *   hasTint/k : if hasTint, k = blockColors.colorMultiplier; (f,f1,f2)=(R,G,B)/255
 *   applyDiff/diffuseBits : if shouldApplyDiffuseLighting, multiply color by LightUtil.diffuseLight
 *   d0/d1/d2  : (posX+offset, ...) doubles added per vertex by buffer.putPosition
 *
 * Replicates buffer.addVertexData + putBrightness4 + putColorMultiplier(little-endian)
 * + putPosition exactly. Prints the 28 resulting ints, one per line; BITWISE-match golden.txt.
 * (Assumes EntityRenderer.anaglyphEnable == false, the default.) */
#include <stdio.h>
#include <string.h>

static float i2f(int b)  { float f; memcpy(&f, &b, 4); return f; }
static int   f2i(float f){ int b;   memcpy(&b, &f, 4); return b; }
static double bits2d(long long b){ double d; memcpy(&d, &b, 8); return d; }

/* buffer.putColorMultiplier little-endian branch on the color int at out[idx]. */
static void colorMul(int *out, int idx, float red, float green, float blue) {
    unsigned j = (unsigned) out[idx];
    int k  = (int)((float)(j & 255u)        * red);
    int l  = (int)((float)((j >> 8) & 255u) * green);
    int i1 = (int)((float)((j >> 16) & 255u)* blue);
    j = j & 0xFF000000u;
    j = j | ((unsigned)i1 << 16) | ((unsigned)l << 8) | (unsigned)k;
    out[idx] = (int) j;
}

int main(void) {
    int vd[28], brightnessIn, hasTint, k, applyDiff, diffuseBits;
    long long d0b, d1b, d2b;
    for (;;) {
        int n = 0, ok = 1;
        for (int z = 0; z < 28; z++) if (scanf("%d", &vd[z]) != 1) { ok = 0; break; }
        if (!ok) break;
        if (scanf("%d %d %d %d %d %lld %lld %lld",
                  &brightnessIn, &hasTint, &k, &applyDiff, &diffuseBits,
                  &d0b, &d1b, &d2b) != 8) break;
        (void) n;
        int out[28];
        memcpy(out, vd, sizeof out);                 /* addVertexData */

        /* putBrightness4: lightmap int (offset 6) of each of 4 vertices */
        out[6] = out[13] = out[20] = out[27] = brightnessIn;

        /* color */
        if (hasTint || applyDiff) {
            float f, f1, f2;
            if (hasTint) {
                f  = (float)((k >> 16) & 255) / 255.0f;
                f1 = (float)((k >> 8)  & 255) / 255.0f;
                f2 = (float)( k        & 255) / 255.0f;
                if (applyDiff) { float d = i2f(diffuseBits); f *= d; f1 *= d; f2 *= d; }
            } else {
                f = f1 = f2 = i2f(diffuseBits);       /* diffuse only */
            }
            /* color int offset 3 of each vertex */
            colorMul(out, 3, f, f1, f2);
            colorMul(out, 10, f, f1, f2);
            colorMul(out, 17, f, f1, f2);
            colorMul(out, 24, f, f1, f2);
        }

        /* putPosition: add (float)d to each vertex's pos floats (xOffset/yOffset/zOffset = 0) */
        float dx = (float) bits2d(d0b), dy = (float) bits2d(d1b), dz = (float) bits2d(d2b);
        for (int v = 0; v < 4; v++) {
            int b = v * 7;
            out[b + 0] = f2i(dx + i2f(out[b + 0]));
            out[b + 1] = f2i(dy + i2f(out[b + 1]));
            out[b + 2] = f2i(dz + i2f(out[b + 2]));
        }

        for (int z = 0; z < 28; z++) printf("%d\n", out[z]);
    }
    return 0;
}
