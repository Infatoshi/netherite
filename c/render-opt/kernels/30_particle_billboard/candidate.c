/* CANDIDATE: C port of MC Particle.renderParticle quad math (particleTexture==null path).
 * Must BITWISE-match golden/Golden.java.
 *
 * Faithfulness traps handled:
 *  - Vec3d's constructor maps -0.0 -> +0.0 on every component; nz() replicates this and is applied
 *    after EVERY vector op (initial avec, scale, add, crossProduct) -- skipping it flips sign bits.
 *  - float/double cast points preserved exactly: initial avec components computed in float then ->
 *    double; (double)(f9*f9); (double)(2.0F*f9); final pos = (float)((double)f5 + avec.x).
 *  - MathHelper sin/cos via the same 65536 SIN_TABLE + java_f2i truncation as kernel 01.
 *  - color() does (int)(c*255.0F) per channel (java_f2i), UBYTE little-endian pack r|g<<8|b<<16|a<<24.
 * Build with -ffp-contract=off (runner does). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
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
static int java_f2i(float f) {
    if (f != f) return 0;
    if (f >= 2147483648.0f) return INT_MAX;
    if (f <= -2147483648.0f) return INT_MIN;
    return (int)f;
}
static float mh_sin(float v) { return SIN_TABLE[java_f2i(v * 10430.378f) & 65535]; }
static float mh_cos(float v) { return SIN_TABLE[java_f2i(v * 10430.378f + 16384.0f) & 65535]; }

/* Vec3d ctor -0.0 -> +0.0 normalization (x == 0.0 is true for both zeros; assign +0.0). */
static double nz(double x) { return x == 0.0 ? 0.0 : x; }

typedef struct { double x, y, z; } Vec3d;
static Vec3d v3(double x, double y, double z) { Vec3d r = {nz(x), nz(y), nz(z)}; return r; }
static double v3dot(Vec3d a, Vec3d b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3d v3cross(Vec3d a, Vec3d b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static Vec3d v3add(Vec3d a, Vec3d b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static Vec3d v3scale(Vec3d a, double s) { return v3(a.x * s, a.y * s, a.z * s); }

static float f_from_hex(const char *h) { uint32_t b = (uint32_t)strtoul(h, NULL, 16); float f; memcpy(&f, &b, sizeof f); return f; }
static double d_from_hex(const char *h) { uint64_t b = (uint64_t)strtoull(h, NULL, 16); double d; memcpy(&d, &b, sizeof d); return d; }
static unsigned fbits(float f) { uint32_t b; memcpy(&b, &f, sizeof b); return b; }

int main(void) {
    build_table();
    char line[2048];
    while (fgets(line, sizeof line, stdin)) {
        char tok[28][40];
        int nt = 0;
        char *p = line; int consumed;
        while (nt < 28 && sscanf(p, "%39s%n", tok[nt], &consumed) == 1) { p += consumed; ++nt; }
        if (nt < 28) continue;
        int t = 0;
        int particleTextureIndexX = atoi(tok[t++]);
        int particleTextureIndexY = atoi(tok[t++]);
        float particleScale = f_from_hex(tok[t++]);
        double prevPosX = d_from_hex(tok[t++]);
        double prevPosY = d_from_hex(tok[t++]);
        double prevPosZ = d_from_hex(tok[t++]);
        double posX = d_from_hex(tok[t++]);
        double posY = d_from_hex(tok[t++]);
        double posZ = d_from_hex(tok[t++]);
        float partialTicks = f_from_hex(tok[t++]);
        double interpPosX = d_from_hex(tok[t++]);
        double interpPosY = d_from_hex(tok[t++]);
        double interpPosZ = d_from_hex(tok[t++]);
        int light = atoi(tok[t++]);
        float particleAngle = f_from_hex(tok[t++]);
        float prevParticleAngle = f_from_hex(tok[t++]);
        double camDirX = d_from_hex(tok[t++]);
        double camDirY = d_from_hex(tok[t++]);
        double camDirZ = d_from_hex(tok[t++]);
        float rotationX = f_from_hex(tok[t++]);
        float rotationZ = f_from_hex(tok[t++]);
        float rotationYZ = f_from_hex(tok[t++]);
        float rotationXY = f_from_hex(tok[t++]);
        float rotationXZ = f_from_hex(tok[t++]);
        float particleRed = f_from_hex(tok[t++]);
        float particleGreen = f_from_hex(tok[t++]);
        float particleBlue = f_from_hex(tok[t++]);
        float particleAlpha = f_from_hex(tok[t++]);

        Vec3d cameraViewDir = v3(camDirX, camDirY, camDirZ);

        float f = (float)particleTextureIndexX / 16.0f;
        float f1 = f + 0.0624375f;
        float f2 = (float)particleTextureIndexY / 16.0f;
        float f3 = f2 + 0.0624375f;
        float f4 = 0.1f * particleScale;

        float f5 = (float)(prevPosX + (posX - prevPosX) * (double)partialTicks - interpPosX);
        float f6 = (float)(prevPosY + (posY - prevPosY) * (double)partialTicks - interpPosY);
        float f7 = (float)(prevPosZ + (posZ - prevPosZ) * (double)partialTicks - interpPosZ);
        int i = light;
        int j = i >> 16 & 65535;
        int k = i & 65535;

        Vec3d avec3d[4];
        avec3d[0] = v3((double)(-rotationX * f4 - rotationXY * f4), (double)(-rotationZ * f4), (double)(-rotationYZ * f4 - rotationXZ * f4));
        avec3d[1] = v3((double)(-rotationX * f4 + rotationXY * f4), (double)(rotationZ * f4), (double)(-rotationYZ * f4 + rotationXZ * f4));
        avec3d[2] = v3((double)(rotationX * f4 + rotationXY * f4), (double)(rotationZ * f4), (double)(rotationYZ * f4 + rotationXZ * f4));
        avec3d[3] = v3((double)(rotationX * f4 - rotationXY * f4), (double)(-rotationZ * f4), (double)(rotationYZ * f4 - rotationXZ * f4));

        if (particleAngle != 0.0f) {
            float f8 = particleAngle + (particleAngle - prevParticleAngle) * partialTicks;
            float f9 = mh_cos(f8 * 0.5f);
            float f10 = mh_sin(f8 * 0.5f) * (float)cameraViewDir.x;
            float f11 = mh_sin(f8 * 0.5f) * (float)cameraViewDir.y;
            float f12 = mh_sin(f8 * 0.5f) * (float)cameraViewDir.z;
            Vec3d vec3d = v3((double)f10, (double)f11, (double)f12);

            for (int l = 0; l < 4; ++l) {
                Vec3d a = v3scale(vec3d, 2.0 * v3dot(avec3d[l], vec3d));
                Vec3d b = v3scale(avec3d[l], (double)(f9 * f9) - v3dot(vec3d, vec3d));
                Vec3d c = v3scale(v3cross(vec3d, avec3d[l]), (double)(2.0f * f9));
                avec3d[l] = v3add(v3add(a, b), c);
            }
        }

        int cr = java_f2i(particleRed * 255.0f);
        int cg = java_f2i(particleGreen * 255.0f);
        int cb = java_f2i(particleBlue * 255.0f);
        int ca = java_f2i(particleAlpha * 255.0f);
        unsigned color = (unsigned)((cr & 255) | (cg & 255) << 8 | (cb & 255) << 16 | (ca & 255) << 24);

        float uv[4][2] = { {f1, f3}, {f1, f2}, {f, f2}, {f, f3} };
        for (int l = 0; l < 4; ++l) {
            float px = (float)((double)f5 + avec3d[l].x);
            float py = (float)((double)f6 + avec3d[l].y);
            float pz = (float)((double)f7 + avec3d[l].z);
            printf("%x %x %x %x %x %x %d %d\n",
                   fbits(px), fbits(py), fbits(pz),
                   fbits(uv[l][0]), fbits(uv[l][1]), color, j, k);
        }
    }
    return 0;
}
