/* CANDIDATE: C port of MC 1.11.2 ModelBox ctor + TexturedQuad ctor + draw() normal math.
 * Must BITWISE-match golden/Golden.java. Faithfulness traps:
 *  - Vec3d ctor maps -0.0 -> +0.0 on every component (nz); applied in every Vec3d-producing op.
 *  - Vec3d.normalize uses MathHelper.sqrt = (float)Math.sqrt(double): d0 = (double)(float)sqrt(sumsq);
 *    then the d0 < 1e-4 -> ZERO branch.
 *  - int texcoord arithmetic (texU+dz+dx ...) done in int, THEN (float)/width.
 *  - mirror swaps f/x corners, THEN flipFace reverses each quad (flips UV order AND normal sign).
 *  - invertNormal has no setter in this path -> always false.
 * Build with -ffp-contract=off (runner does).
 * Input  (per line, 12 tokens): texU texV x(fhex) y(fhex) z(fhex) dx dy dz delta(fhex) mirror texW(fhex) texH(fhex)
 * Output per record (38 lines): 8 corner "x y z"; per quad 0..5: 4 "u v" + 1 "nx ny nz" (hex float-bits). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

static float f_from_hex(unsigned long b) { uint32_t u = (uint32_t)b; float f; memcpy(&f, &u, sizeof f); return f; }
static unsigned fbits(float f) { uint32_t b; memcpy(&b, &f, sizeof b); return b; }

typedef struct { double x, y, z; } Vec3d;
static double nz(double v) { return v == -0.0 ? 0.0 : v; }  /* v == -0.0 is true only for -0.0 and +0.0; assign +0.0 */
static Vec3d v3(double x, double y, double z) { Vec3d r = { nz(x), nz(y), nz(z) }; return r; }
static Vec3d v3_from_floats(float x, float y, float z) { return v3((double)x, (double)y, (double)z); }
static Vec3d v3_subtractReverse(Vec3d self, Vec3d vec) { return v3(vec.x - self.x, vec.y - self.y, vec.z - self.z); }
static Vec3d v3_cross(Vec3d self, Vec3d vec) {
    return v3(self.y * vec.z - self.z * vec.y, self.z * vec.x - self.x * vec.z, self.x * vec.y - self.y * vec.x);
}
static Vec3d v3_normalize(Vec3d self) {
    double d0 = (double)(float)sqrt(self.x * self.x + self.y * self.y + self.z * self.z);  /* MathHelper.sqrt */
    if (d0 < 1.0E-4) { Vec3d z = { 0.0, 0.0, 0.0 }; return z; }
    return v3(self.x / d0, self.y / d0, self.z / d0);
}

/* PositionTextureVertex reduced to position + uv */
typedef struct { Vec3d v3d; float u, v; } PTV;

static void fline(float a, float b, float c) { printf("%x %x %x\n", fbits(a), fbits(b), fbits(c)); }
static void f2line(float a, float b) { printf("%x %x\n", fbits(a), fbits(b)); }

/* TexturedQuad ctor: assign UVs to vp[0..3] (int texcoords -> float / size). f,f1 are 0/size = 0. */
static void tquad_set_uv(PTV *vp[4], int u1, int v1, int u2, int v2, float tw, float th) {
    float f = 0.0F / tw;
    float f1 = 0.0F / th;
    vp[0]->u = (float)u2 / tw - f;  vp[0]->v = (float)v1 / th + f1;
    vp[1]->u = (float)u1 / tw + f;  vp[1]->v = (float)v1 / th + f1;
    vp[2]->u = (float)u1 / tw + f;  vp[2]->v = (float)v2 / th - f1;
    vp[3]->u = (float)u2 / tw - f;  vp[3]->v = (float)v2 / th - f1;
}

int main(void) {
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        int texU, texV, dx, dy, dz, mirror;
        unsigned long xb, yb, zb, db, wb, hb;
        int n = sscanf(line, "%d %d %lx %lx %lx %d %d %d %lx %d %lx %lx",
                       &texU, &texV, &xb, &yb, &zb, &dx, &dy, &dz, &db, &mirror, &wb, &hb);
        if (n != 12) continue;
        float x = f_from_hex(xb), y = f_from_hex(yb), z = f_from_hex(zb);
        float delta = f_from_hex(db);
        float tw = f_from_hex(wb), th = f_from_hex(hb);

        float f = x + (float)dx;
        float f1 = y + (float)dy;
        float f2 = z + (float)dz;
        x = x - delta; y = y - delta; z = z - delta;
        f = f + delta; f1 = f1 + delta; f2 = f2 + delta;
        if (mirror) { float f3 = f; f = x; x = f3; }

        /* 8 corner vertices p7,p,p1,p2,p3,p4,p5,p6 in vertexPositions[0..7] */
        PTV vp[8];
        vp[0].v3d = v3_from_floats(x, y, z);    /* p7 */
        vp[1].v3d = v3_from_floats(f, y, z);    /* p  */
        vp[2].v3d = v3_from_floats(f, f1, z);   /* p1 */
        vp[3].v3d = v3_from_floats(x, f1, z);   /* p2 */
        vp[4].v3d = v3_from_floats(x, y, f2);   /* p3 */
        vp[5].v3d = v3_from_floats(f, y, f2);   /* p4 */
        vp[6].v3d = v3_from_floats(f, f1, f2);  /* p5 */
        vp[7].v3d = v3_from_floats(x, f1, f2);  /* p6 */
        for (int i = 0; i < 8; ++i) vp[i].u = vp[i].v = 0.0F;

        /* quad vertex membership by index into vp[]: see ModelBox ctor */
        int quadIdx[6][4] = {
            {5, 1, 2, 6},  /* p4,p,p1,p5 */
            {0, 4, 7, 3},  /* p7,p3,p6,p2 */
            {5, 4, 0, 1},  /* p4,p3,p7,p  */
            {2, 3, 7, 6},  /* p1,p2,p6,p5 */
            {1, 0, 3, 2},  /* p,p7,p2,p1  */
            {4, 5, 6, 7},  /* p3,p4,p5,p6 */
        };
        int tc[6][4] = {  /* u1,v1,u2,v2 (int) per quad */
            {texU + dz + dx, texV + dz, texU + dz + dx + dz, texV + dz + dy},
            {texU, texV + dz, texU + dz, texV + dz + dy},
            {texU + dz, texV, texU + dz + dx, texV + dz},
            {texU + dz + dx, texV + dz, texU + dz + dx + dx, texV},
            {texU + dz, texV + dz, texU + dz + dx, texV + dz + dy},
            {texU + dz + dx + dz, texV + dz, texU + dz + dx + dz + dx, texV + dz + dy},
        };

        /* Each quad owns its own 4 PTV copies (setTexturePosition makes new vertices, sharing v3d). */
        PTV quads[6][4];
        for (int qi = 0; qi < 6; ++qi) {
            PTV *qp[4];
            for (int i = 0; i < 4; ++i) { quads[qi][i] = vp[quadIdx[qi][i]]; qp[i] = &quads[qi][i]; }
            tquad_set_uv(qp, tc[qi][0], tc[qi][1], tc[qi][2], tc[qi][3], tw, th);
            if (mirror) {  /* flipFace: reverse the 4 vertices */
                PTV tmp[4];
                for (int i = 0; i < 4; ++i) tmp[i] = quads[qi][3 - i];
                for (int i = 0; i < 4; ++i) quads[qi][i] = tmp[i];
            }
        }

        /* output 8 corners */
        for (int i = 0; i < 8; ++i)
            fline((float)vp[i].v3d.x, (float)vp[i].v3d.y, (float)vp[i].v3d.z);

        /* output per-quad UVs (final order) + normal */
        for (int qi = 0; qi < 6; ++qi) {
            for (int i = 0; i < 4; ++i) f2line(quads[qi][i].u, quads[qi][i].v);
            Vec3d vec3d = v3_subtractReverse(quads[qi][1].v3d, quads[qi][0].v3d);
            Vec3d vec3d1 = v3_subtractReverse(quads[qi][1].v3d, quads[qi][2].v3d);
            Vec3d vec3d2 = v3_normalize(v3_cross(vec3d1, vec3d));
            fline((float)vec3d2.x, (float)vec3d2.y, (float)vec3d2.z);
        }
    }
    return 0;
}
