/* CANDIDATE: pure-C port of MC 1.11.2 BlockModelRenderer.renderQuadsSmooth()
 *   (src/net/minecraft/client/renderer/BlockModelRenderer.java:116), per single quad, on the
 *   DefaultVertexFormats.BLOCK layout (7 ints/vertex; 28 ints/quad).
 *
 * Like renderQuadsFlat (23) but with smooth lighting / ambient occlusion: brightness and color
 * are per-vertex, computed by AmbientOcclusionFace.updateVertexBrightness (world + AO dependent).
 * Those per-vertex results are world state, so they are SNAPSHOT as inputs; this kernel ports the
 * buffer-write arithmetic the method performs given them.
 *
 * Golden CAPTURED FROM REAL MINECRAFT (NetheriteMod 'capture_quadssmooth'): begins a fresh VertexBuffer,
 * invokes the real private renderQuadsSmooth with a one-quad list + a real AmbientOcclusionFace,
 * dumps the 28 output ints, and records the aoFace fields AFTER the call (vertexColorMultiplier is
 * already post-diffuse, since the in-place diffuse multiply happens before putColorMultiplier).
 *
 * Input record (one line, golden/inputs.txt): 41 ints
 *     vd[0..27] vb0 vb1 vb2 vb3 vcm0 vcm1 vcm2 vcm3 hasTint k d0bits d1bits d2bits
 *   vd   : bakedquad.getVertexData() (addVertexData)
 *   vb*  : aoFace.vertexBrightness[0..3] -> putBrightness4
 *   vcm* : aoFace.vertexColorMultiplier[0..3] as float raw bits (post-diffuse)
 *   hasTint/k : if hasTint, k=blockColors.colorMultiplier; (f,f1,f2)=(R,G,B)/255 else 1
 *   d0/d1/d2  : position offset doubles -> putPosition
 *
 * Per vertex v: putColorMultiplier(vcm[v]*f, vcm[v]*f1, vcm[v]*f2). Prints 28 ints; BITWISE-match.
 * (Assumes EntityRenderer.anaglyphEnable == false, the default.) */
#include <stdio.h>
#include <string.h>

static float i2f(int b)  { float f; memcpy(&f, &b, 4); return f; }
static int   f2i(float f){ int b;   memcpy(&b, &f, 4); return b; }
static double bits2d(long long b){ double d; memcpy(&d, &b, 8); return d; }

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
    int vd[28], vb[4], vcmb[4], hasTint, k;
    long long d0b, d1b, d2b;
    for (;;) {
        int ok = 1;
        for (int z = 0; z < 28; z++) if (scanf("%d", &vd[z]) != 1) { ok = 0; break; }
        if (!ok) break;
        for (int z = 0; z < 4; z++) if (scanf("%d", &vb[z]) != 1) { ok = 0; break; }
        if (!ok) break;
        for (int z = 0; z < 4; z++) if (scanf("%d", &vcmb[z]) != 1) { ok = 0; break; }
        if (!ok) break;
        if (scanf("%d %d %lld %lld %lld", &hasTint, &k, &d0b, &d1b, &d2b) != 5) break;

        int out[28];
        memcpy(out, vd, sizeof out);                       /* addVertexData */

        /* putBrightness4: per-vertex lightmap (offset 6) */
        out[6] = vb[0]; out[13] = vb[1]; out[20] = vb[2]; out[27] = vb[3];

        float f, f1, f2;
        if (hasTint) {
            f  = (float)((k >> 16) & 255) / 255.0f;
            f1 = (float)((k >> 8)  & 255) / 255.0f;
            f2 = (float)( k        & 255) / 255.0f;
        } else {
            f = f1 = f2 = 1.0f;
        }
        for (int v = 0; v < 4; v++) {
            float m = i2f(vcmb[v]);
            colorMul(out, v * 7 + 3, m * f, m * f1, m * f2);
        }

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
