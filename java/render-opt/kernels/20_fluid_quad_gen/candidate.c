/* CANDIDATE: C port of BlockFluidRenderer.renderFluid (MC 1.11.2)
 *   src/net/minecraft/client/renderer/BlockFluidRenderer.java:43
 *
 * Emits the DefaultVertexFormats.BLOCK vertex stream for one fluid block
 * (7 ints/vertex: posX, posY, posZ, colorPacked, u, v, lightmap), one int per
 * line, BITWISE-matching golden/golden.txt.
 *
 * INPUT FORMAT (enriched live-hook capture, produced by NetheriteMod `capture_fluidquads`):
 *   line 1 (sprite UV header), 20 ints (each is Float.floatToRawIntBits):
 *       wstill(minU maxU minV maxV) wflow(...) lstill(...) lflow(...) woverlay(...)
 *   then one line per fluid block, 27 ints:
 *       x y z isLava colorInt flag1 flag2 a0 a1 a2 a3 renderSidesUp slopeBits
 *       f7b f8b f9b f10b lmUp lmDown lmS0 lmS1 lmS2 lmS3 ov0 ov1 ov2 ov3
 *   where:
 *       flag1/flag2          = shouldSideBeRendered UP / DOWN
 *       a0..a3               = shouldSideBeRendered NORTH/SOUTH/WEST/EAST
 *       renderSidesUp        = blockliquid.shouldRenderSides(pos.up())
 *       slopeBits            = floatToRawIntBits(getSlopeAngle)  (<-999 -> still)
 *       f7b..f10b            = floatToRawIntBits(getFluidHeight) for this/south/eastsouth/east
 *       lmUp/lmDown          = getPackedLightmapCoords(pos)/(pos.down())
 *       lmS0..lmS3           = getPackedLightmapCoords(neighbor) per side i1=0..3
 *       ov0..ov3             = 1 if that side uses atlasSpriteWaterOverlay (water+glass), else 0
 *
 * UV-fix: getInterpolatedU/V is reproduced from the captured per-sprite UV bounds
 * (minU/maxU/minV/maxV), removing the original runtime-atlas blocker.
 *
 * Build with -ffp-contract=off (the runner does) so float ops round like the JVM.
 *
 * STATUS: faithful port written against the enriched capture format. The current
 * on-disk golden/inputs.txt is the OLD 7-field capture and lacks the geometry/
 * light/color leaf inputs, so this cannot be run to PASS until a richer capture is
 * taken on the game box. See README.md.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float SIN_TABLE[65536];
static void build_table(void) {
    for (int i = 0; i < 65536; ++i)
        SIN_TABLE[i] = (float)sin((double)i * M_PI * 2.0 / 65536.0);
}
/* Java narrowing float->int (JLS 5.1.3). */
static int java_f2i(float f) {
    if (f != f) return 0;
    if (f >= 2147483648.0f) return INT_MAX;
    if (f <= -2147483648.0f) return INT_MIN;
    return (int)f;
}
static float sin_lut(float v) { return SIN_TABLE[java_f2i(v * 10430.378f) & 65535]; }
static float cos_lut(float v) { return SIN_TABLE[java_f2i(v * 10430.378f + 16384.0f) & 65535]; }

static float i2f(int b) { float f; memcpy(&f, &b, 4); return f; }
static int   f2i(float f){ int b;  memcpy(&b, &f, 4); return b; }
static int   d2fi(double d){ return f2i((float)d); }   /* VertexBuffer.pos: (float)(x+offset) */

/* TextureAtlasSprite.getInterpolatedU/V: minU + (maxU-minU)*(float)u/16.0F */
static float interpU(const float *sp, double u) { float f = sp[1] - sp[0]; return sp[0] + f * (float)u / 16.0F; }
static float interpV(const float *sp, double v) { float f = sp[3] - sp[2]; return sp[2] + f * (float)v / 16.0F; }

/* VertexBuffer.color(float,float,float,float)->color(int...): packed little-endian RGBA. */
static int colorPack(float r, float g, float b, float a) {
    int ri = java_f2i(r * 255.0F) & 255;
    int gi = java_f2i(g * 255.0F) & 255;
    int bi = java_f2i(b * 255.0F) & 255;
    int ai = java_f2i(a * 255.0F) & 255;
    return ri | (gi << 8) | (bi << 16) | (ai << 24);
}
/* VertexBuffer.lightmap(p1,p2): SHORT pair, int = (p1<<16) | (p2&0xffff). */
static int lmPack(int p1, int p2) { return (p1 << 16) | (p2 & 0xffff); }

/* one emitted vertex -> 7 ints printed, one per line. */
static void vertex(double x, double y, double z, int color, float u, float v, int lm) {
    printf("%d\n", d2fi(x));
    printf("%d\n", d2fi(y));
    printf("%d\n", d2fi(z));
    printf("%d\n", color);
    printf("%d\n", f2i(u));
    printf("%d\n", f2i(v));
    printf("%d\n", lm);
}

int main(void) {
    build_table();

    /* --- sprite UV header (20 ints) --- */
    int hdr[20];
    for (int i = 0; i < 20; i++) if (scanf("%d", &hdr[i]) != 1) return 0;
    float spr[5][4]; /* 0=wstill 1=wflow 2=lstill 3=lflow 4=woverlay */
    for (int s = 0; s < 5; s++) for (int k = 0; k < 4; k++) spr[s][k] = i2f(hdr[s * 4 + k]);

    int x, y, z, isLava, colorInt, flag1, flag2, a0, a1, a2, a3, rsUp, slopeBits;
    int f7b, f8b, f9b, f10b, lmUp, lmDown, lmS[4], ov[4];

    while (scanf("%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
                 &x, &y, &z, &isLava, &colorInt, &flag1, &flag2,
                 &a0, &a1, &a2, &a3, &rsUp, &slopeBits,
                 &f7b, &f8b, &f9b, &f10b, &lmUp, &lmDown,
                 &lmS[0], &lmS[1], &lmS[2], &lmS[3],
                 &ov[0], &ov[1], &ov[2], &ov[3]) == 27) {

        /* sprite set: water {0,1} / lava {2,3}; [0]=still [1]=flow */
        const float *setStill = isLava ? spr[2] : spr[0];
        const float *setFlow  = isLava ? spr[3] : spr[1];
        int aboolean[4] = { a0, a1, a2, a3 };

        int i = colorInt;
        float f  = (float)((i >> 16) & 255) / 255.0F;
        float f1 = (float)((i >> 8)  & 255) / 255.0F;
        float f2 = (float)( i        & 255) / 255.0F;

        /* nothing renders -> emit nothing (matches return false, vc=0). */
        if (!flag1 && !flag2 && !aboolean[0] && !aboolean[1] && !aboolean[2] && !aboolean[3])
            continue;

        float f7  = i2f(f7b);
        float f8  = i2f(f8b);
        float f9  = i2f(f9b);
        float f10 = i2f(f10b);
        double d0 = (double)x, d1 = (double)y, d2 = (double)z;

        if (flag1) {
            float f12 = i2f(slopeBits);
            const float *spUp = f12 > -999.0F ? setFlow : setStill;
            f7  -= 0.001F; f8 -= 0.001F; f9 -= 0.001F; f10 -= 0.001F;
            float f13, f14, f15, f16, f17, f18, f19, f20;
            if (f12 < -999.0F) {
                f13 = interpU(spUp, 0.0);  f17 = interpV(spUp, 0.0);
                f14 = f13;                 f18 = interpV(spUp, 16.0);
                f15 = interpU(spUp, 16.0); f19 = f18;
                f16 = f15;                 f20 = f17;
            } else {
                float f21 = sin_lut(f12) * 0.25F;
                float f22 = cos_lut(f12) * 0.25F;
                f13 = interpU(spUp, (double)(8.0F + (-f22 - f21) * 16.0F));
                f17 = interpV(spUp, (double)(8.0F + (-f22 + f21) * 16.0F));
                f14 = interpU(spUp, (double)(8.0F + (-f22 + f21) * 16.0F));
                f18 = interpV(spUp, (double)(8.0F + ( f22 + f21) * 16.0F));
                f15 = interpU(spUp, (double)(8.0F + ( f22 + f21) * 16.0F));
                f19 = interpV(spUp, (double)(8.0F + ( f22 - f21) * 16.0F));
                f16 = interpU(spUp, (double)(8.0F + ( f22 - f21) * 16.0F));
                f20 = interpV(spUp, (double)(8.0F + (-f22 - f21) * 16.0F));
            }
            int l2 = (lmUp >> 16) & 65535;
            int i3 = lmUp & 65535;
            int lm = lmPack(l2, i3);
            float f24 = 1.0F * f, f25 = 1.0F * f1, f26 = 1.0F * f2;
            int col = colorPack(f24, f25, f26, 1.0F);
            vertex(d0 + 0.0, d1 + (double)f7,  d2 + 0.0, col, f13, f17, lm);
            vertex(d0 + 0.0, d1 + (double)f8,  d2 + 1.0, col, f14, f18, lm);
            vertex(d0 + 1.0, d1 + (double)f9,  d2 + 1.0, col, f15, f19, lm);
            vertex(d0 + 1.0, d1 + (double)f10, d2 + 0.0, col, f16, f20, lm);
            if (rsUp) {
                vertex(d0 + 0.0, d1 + (double)f7,  d2 + 0.0, col, f13, f17, lm);
                vertex(d0 + 1.0, d1 + (double)f10, d2 + 0.0, col, f16, f20, lm);
                vertex(d0 + 1.0, d1 + (double)f9,  d2 + 1.0, col, f15, f19, lm);
                vertex(d0 + 0.0, d1 + (double)f8,  d2 + 1.0, col, f14, f18, lm);
            }
        }

        if (flag2) {
            float f35 = setStill[0], f36 = setStill[1], f37 = setStill[2], f38 = setStill[3];
            int i2v = (lmDown >> 16) & 65535;
            int j2  = lmDown & 65535;
            int lm = lmPack(i2v, j2);
            int col = colorPack(0.5F, 0.5F, 0.5F, 1.0F);
            vertex(d0,       d1, d2 + 1.0, col, f35, f38, lm);
            vertex(d0,       d1, d2,       col, f35, f37, lm);
            vertex(d0 + 1.0, d1, d2,       col, f36, f37, lm);
            vertex(d0 + 1.0, d1, d2 + 1.0, col, f36, f38, lm);
        }

        for (int i1 = 0; i1 < 4; ++i1) {
            const float *sp1 = ov[i1] ? spr[4] : setFlow;
            if (aboolean[i1]) {
                float f39, f40; double d3, d4, d5, d6;
                if (i1 == 0)      { f39 = f7;  f40 = f10; d3 = d0;       d5 = d0 + 1.0; d4 = d2 + 0.0010000000474974513; d6 = d2 + 0.0010000000474974513; }
                else if (i1 == 1) { f39 = f9;  f40 = f8;  d3 = d0 + 1.0; d5 = d0;       d4 = d2 + 1.0 - 0.0010000000474974513; d6 = d2 + 1.0 - 0.0010000000474974513; }
                else if (i1 == 2) { f39 = f8;  f40 = f7;  d3 = d0 + 0.0010000000474974513; d5 = d0 + 0.0010000000474974513; d4 = d2 + 1.0; d6 = d2; }
                else              { f39 = f10; f40 = f9;  d3 = d0 + 1.0 - 0.0010000000474974513; d5 = d0 + 1.0 - 0.0010000000474974513; d4 = d2; d6 = d2 + 1.0; }

                float f41 = interpU(sp1, 0.0);
                float f27 = interpU(sp1, 8.0);
                float f28 = interpV(sp1, (double)((1.0F - f39) * 16.0F * 0.5F));
                float f29 = interpV(sp1, (double)((1.0F - f40) * 16.0F * 0.5F));
                float f30 = interpV(sp1, 8.0);
                int k = (lmS[i1] >> 16) & 65535;
                int l = lmS[i1] & 65535;
                int lm = lmPack(k, l);
                float f31 = i1 < 2 ? 0.8F : 0.6F;
                float f32 = 1.0F * f31 * f, f33 = 1.0F * f31 * f1, f34 = 1.0F * f31 * f2;
                int col = colorPack(f32, f33, f34, 1.0F);
                vertex(d3, d1 + (double)f39, d4, col, f41, f28, lm);
                vertex(d5, d1 + (double)f40, d6, col, f27, f29, lm);
                vertex(d5, d1 + 0.0,         d6, col, f27, f30, lm);
                vertex(d3, d1 + 0.0,         d4, col, f41, f30, lm);
                if (!ov[i1]) {
                    vertex(d3, d1 + 0.0,         d4, col, f41, f30, lm);
                    vertex(d5, d1 + 0.0,         d6, col, f27, f30, lm);
                    vertex(d5, d1 + (double)f40, d6, col, f27, f29, lm);
                    vertex(d3, d1 + (double)f39, d4, col, f41, f28, lm);
                }
            }
        }
    }
    return 0;
}
