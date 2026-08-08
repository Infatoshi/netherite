/* CANDIDATE: C port of MC 1.11.2 FaceBakery rotatePart() + rotateVertex() geometric transform.
 * Must match golden/Golden.java. Op order preserved; build with -ffp-contract=off.
 * See golden for source lines. floats fed/emitted as raw 32-bit hex.
 * Input  (per line): vx vy vz axis angle ox oy oz rescale m[16]
 * Output (per line): 6 hex ints = rotatePart(vec) bits then transform(thatVec, m) bits. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static float b2f(uint32_t b) { float f; memcpy(&f, &b, sizeof f); return f; }
static uint32_t f2b(float f) { uint32_t b; memcpy(&b, &f, sizeof b); return b; }

/* FaceBakery class constants (cos at class-init). Computed identically to the JVM;
 * if a bitwise mismatch ever appears on rescale lines, hardcode the JVM float bits here. */
static const float SCALE_ROTATION_22_5_C  = 0;  /* set in main via init (avoids static-init UB) */
static float SCALE_22_5, SCALE_GEN;

/* lwjgl Matrix4f.rotate(angle, axis, src=dest=m) -- m col-major m[col*4+row], starts identity */
static void lwjgl_rotate(float angle, float ax, float ay, float az, float *m) {
    float c = (float)cos(angle);
    float s = (float)sin(angle);
    float oneminusc = 1.0f - c;
    float xy = ax * ay;
    float yz = ay * az;
    float xz = ax * az;
    float xs = ax * s;
    float ys = ay * s;
    float zs = az * s;
    float f00 = ax * ax * oneminusc + c;
    float f01 = xy * oneminusc + zs;
    float f02 = xz * oneminusc - ys;
    float f10 = xy * oneminusc - zs;
    float f11 = ay * ay * oneminusc + c;
    float f12 = yz * oneminusc + xs;
    float f20 = xz * oneminusc + ys;
    float f21 = yz * oneminusc - xs;
    float f22 = az * az * oneminusc + c;
    float sm00=m[0], sm01=m[1], sm02=m[2], sm03=m[3];
    float sm10=m[4], sm11=m[5], sm12=m[6], sm13=m[7];
    float sm20=m[8], sm21=m[9], sm22=m[10], sm23=m[11];
    float t00 = sm00 * f00 + sm10 * f01 + sm20 * f02;
    float t01 = sm01 * f00 + sm11 * f01 + sm21 * f02;
    float t02 = sm02 * f00 + sm12 * f01 + sm22 * f02;
    float t03 = sm03 * f00 + sm13 * f01 + sm23 * f02;
    float t10 = sm00 * f10 + sm10 * f11 + sm20 * f12;
    float t11 = sm01 * f10 + sm11 * f11 + sm21 * f12;
    float t12 = sm02 * f10 + sm12 * f11 + sm22 * f12;
    float t13 = sm03 * f10 + sm13 * f11 + sm23 * f12;
    m[8]  = sm00 * f20 + sm10 * f21 + sm20 * f22;
    m[9]  = sm01 * f20 + sm11 * f21 + sm21 * f22;
    m[10] = sm02 * f20 + sm12 * f21 + sm22 * f22;
    m[11] = sm03 * f20 + sm13 * f21 + sm23 * f22;
    m[0]=t00; m[1]=t01; m[2]=t02; m[3]=t03;
    m[4]=t10; m[5]=t11; m[6]=t12; m[7]=t13;
}

static void rotate_scale(float *pos, const float *origin, const float *m,
                         float scx, float scy, float scz) {
    float x = pos[0]-origin[0], y = pos[1]-origin[1], z = pos[2]-origin[2], w = 1.0f;
    /* lwjgl transform: col-major */
    float rx = m[0]*x + m[4]*y + m[8]*z  + m[12]*w;
    float ry = m[1]*x + m[5]*y + m[9]*z  + m[13]*w;
    float rz = m[2]*x + m[6]*y + m[10]*z + m[14]*w;
    rx *= scx; ry *= scy; rz *= scz;
    pos[0] = rx + origin[0];
    pos[1] = ry + origin[1];
    pos[2] = rz + origin[2];
}

static void rotate_part(float *pos, int axis, float angle, const float *origin, int rescale) {
    if (axis > 2) return;  /* axis 3 = none */
    float m[16] = {0};
    m[0]=1; m[5]=1; m[10]=1; m[15]=1;
    float vx, vy, vz;
    switch (axis) {
        case 0: lwjgl_rotate(angle * 0.017453292f, 1.0f, 0.0f, 0.0f, m); vx=0; vy=1; vz=1; break;
        case 1: lwjgl_rotate(angle * 0.017453292f, 0.0f, 1.0f, 0.0f, m); vx=1; vy=0; vz=1; break;
        default: lwjgl_rotate(angle * 0.017453292f, 0.0f, 0.0f, 1.0f, m); vx=1; vy=1; vz=0; break;
    }
    if (rescale) {
        float sc = (fabsf(angle) == 22.5f) ? SCALE_22_5 : SCALE_GEN;
        vx *= sc; vy *= sc; vz *= sc;
        vx += 1.0f; vy += 1.0f; vz += 1.0f;
    } else {
        vx = 1.0f; vy = 1.0f; vz = 1.0f;
    }
    rotate_scale(pos, origin, m, vx, vy, vz);
}

/* ForgeHooksClient.transform: javax row-major matrix m[row*4+col] */
static void transform_vertex(float *vec, const float *m) {
    float x = vec[0], y = vec[1], z = vec[2], w = 1.0f;
    float f  = m[0]*x  + m[1]*y  + m[2]*z  + m[3]*w;
    float f2 = m[4]*x  + m[5]*y  + m[6]*z  + m[7]*w;
    float f3 = m[8]*x  + m[9]*y  + m[10]*z + m[11]*w;
    float tw = m[12]*x + m[13]*y + m[14]*z + m[15]*w;
    float tx = f, ty = f2, tz = f3;
    if (fabsf(tw - 1.0f) > 1e-5) { float inv = 1.0f / tw; tx*=inv; ty*=inv; tz*=inv; }
    vec[0] = tx; vec[1] = ty; vec[2] = tz;
}

int main(void) {
    (void)SCALE_ROTATION_22_5_C;
    SCALE_22_5 = 1.0f / (float)cos(0.39269909262657166) - 1.0f;
    SCALE_GEN  = 1.0f / (float)cos((3.141592653589793 / 4.0)) - 1.0f;
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        char *p = line;
        unsigned long tok[25];
        int n = 0;
        for (; n < 25; ++n) {
            while (*p == ' ' || *p == '\t') ++p;
            if (*p == '\0' || *p == '\n') break;
            tok[n] = strtoul(p, &p, 16);
        }
        if (n != 25) continue;
        float pos[3] = { b2f((uint32_t)tok[0]), b2f((uint32_t)tok[1]), b2f((uint32_t)tok[2]) };
        int axis = (int)(int32_t)tok[3];
        float angle = b2f((uint32_t)tok[4]);
        float origin[3] = { b2f((uint32_t)tok[5]), b2f((uint32_t)tok[6]), b2f((uint32_t)tok[7]) };
        int rescale = (int)tok[8];
        float m[16];
        for (int i = 0; i < 16; ++i) m[i] = b2f((uint32_t)tok[9 + i]);

        rotate_part(pos, axis, angle, origin, rescale);
        float vec[3] = { pos[0], pos[1], pos[2] };
        transform_vertex(vec, m);

        printf("%x %x %x %x %x %x\n",
               f2b(pos[0]), f2b(pos[1]), f2b(pos[2]),
               f2b(vec[0]), f2b(vec[1]), f2b(vec[2]));
    }
    return 0;
}
