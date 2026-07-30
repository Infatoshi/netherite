/* CANDIDATE: C port of MC 1.11.2 FaceBakery.fillVertexData() + storeVertexData().
 * Must BITWISE-match golden/Golden.java. Op order preserved; build with -ffp-contract=off.
 * modelRotation = identity (rotateVertex no-op, storeIndex == vertexIndex); see golden for isolation.
 * Input  (per line, 24 tokens, all parsed base-16; small ints 0..5 so base16==base10):
 *   vertexIndex facing shade  bounds[6]  uvQuarter  uvs[4]  minU maxU minV maxV  axis angle ox oy oz rescale
 * Output (per line, 7 hex ints): posX posY posZ shadeColor U V normal(=0). */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static float b2f(uint32_t b) { float f; memcpy(&f, &b, sizeof f); return f; }
static uint32_t f2b(float f) { uint32_t b; memcpy(&b, &f, sizeof b); return b; }

static float SCALE_22_5, SCALE_GEN;

/* EnumFaceDirection vertex table [facing][vertex] -> {xIndex,yIndex,zIndex} (EnumFacing indices) */
static const int VINFO[6][4][3] = {
    {{4,0,3},{4,0,2},{5,0,2},{5,0,3}}, /* DOWN  */
    {{4,1,2},{4,1,3},{5,1,3},{5,1,2}}, /* UP    */
    {{5,1,2},{5,0,2},{4,0,2},{4,1,2}}, /* NORTH */
    {{4,1,3},{4,0,3},{5,0,3},{5,1,3}}, /* SOUTH */
    {{4,1,2},{4,0,2},{4,0,3},{4,1,3}}, /* WEST  */
    {{5,1,3},{5,0,3},{5,0,2},{5,1,2}}, /* EAST  */
};

static float getFaceBrightness(int facing) {
    switch (facing) {
        case 0: return 0.5f;
        case 1: return 1.0f;
        case 2: case 3: return 0.8f;
        case 4: case 5: return 0.6f;
        default: return 1.0f;
    }
}
static int clamp_i(int n, int lo, int hi) { return n < lo ? lo : (n > hi ? hi : n); }
static int getFaceShadeColor(int facing) {
    float f = getFaceBrightness(facing);
    int i = clamp_i((int)(f * 255.0f), 0, 255);
    return -16777216 | i << 16 | i << 8 | i;
}

static int getVertexRotated(int idx, int q) { return (idx + q) % 4; }
static float getVertexU(const float *uvs, int idx, int q) {
    int i = getVertexRotated(idx, q);
    return (i != 0 && i != 1) ? uvs[2] : uvs[0];
}
static float getVertexV(const float *uvs, int idx, int q) {
    int i = getVertexRotated(idx, q);
    return (i != 0 && i != 3) ? uvs[3] : uvs[1];
}
static float getInterpolatedU(float minU, float maxU, double u) {
    float f = maxU - minU;
    return minU + f * (float)u / 16.0f;
}
static float getInterpolatedV(float minV, float maxV, double v) {
    float f = maxV - minV;
    return minV + f * (float)v / 16.0f;
}

static void lwjgl_rotate(float angle, float ax, float ay, float az, float *m) {
    float c = (float)cos(angle), s = (float)sin(angle), oneminusc = 1.0f - c;
    float xy=ax*ay, yz=ay*az, xz=ax*az, xs=ax*s, ys=ay*s, zs=az*s;
    float f00=ax*ax*oneminusc+c, f01=xy*oneminusc+zs, f02=xz*oneminusc-ys;
    float f10=xy*oneminusc-zs, f11=ay*ay*oneminusc+c, f12=yz*oneminusc+xs;
    float f20=xz*oneminusc+ys, f21=yz*oneminusc-xs, f22=az*az*oneminusc+c;
    float sm00=m[0],sm01=m[1],sm02=m[2],sm03=m[3], sm10=m[4],sm11=m[5],sm12=m[6],sm13=m[7];
    float sm20=m[8],sm21=m[9],sm22=m[10],sm23=m[11];
    float t00=sm00*f00+sm10*f01+sm20*f02, t01=sm01*f00+sm11*f01+sm21*f02;
    float t02=sm02*f00+sm12*f01+sm22*f02, t03=sm03*f00+sm13*f01+sm23*f02;
    float t10=sm00*f10+sm10*f11+sm20*f12, t11=sm01*f10+sm11*f11+sm21*f12;
    float t12=sm02*f10+sm12*f11+sm22*f12, t13=sm03*f10+sm13*f11+sm23*f12;
    m[8]=sm00*f20+sm10*f21+sm20*f22; m[9]=sm01*f20+sm11*f21+sm21*f22;
    m[10]=sm02*f20+sm12*f21+sm22*f22; m[11]=sm03*f20+sm13*f21+sm23*f22;
    m[0]=t00;m[1]=t01;m[2]=t02;m[3]=t03; m[4]=t10;m[5]=t11;m[6]=t12;m[7]=t13;
}
static void rotate_scale(float *p, const float *o, const float *m, float scx, float scy, float scz) {
    float x=p[0]-o[0], y=p[1]-o[1], z=p[2]-o[2], w=1.0f;
    float rx=m[0]*x+m[4]*y+m[8]*z+m[12]*w, ry=m[1]*x+m[5]*y+m[9]*z+m[13]*w, rz=m[2]*x+m[6]*y+m[10]*z+m[14]*w;
    rx*=scx; ry*=scy; rz*=scz;
    p[0]=rx+o[0]; p[1]=ry+o[1]; p[2]=rz+o[2];
}
static void rotate_part(float *pos, int axis, float angle, const float *origin, int rescale) {
    if (axis > 2) return;
    float m[16] = {0}; m[0]=1; m[5]=1; m[10]=1; m[15]=1;
    float vx, vy, vz;
    switch (axis) {
        case 0: lwjgl_rotate(angle*0.017453292f,1,0,0,m); vx=0;vy=1;vz=1; break;
        case 1: lwjgl_rotate(angle*0.017453292f,0,1,0,m); vx=1;vy=0;vz=1; break;
        default: lwjgl_rotate(angle*0.017453292f,0,0,1,m); vx=1;vy=1;vz=0; break;
    }
    if (rescale) {
        float sc = (fabsf(angle) == 22.5f) ? SCALE_22_5 : SCALE_GEN;
        vx*=sc; vy*=sc; vz*=sc; vx+=1.0f; vy+=1.0f; vz+=1.0f;
    } else { vx=1.0f; vy=1.0f; vz=1.0f; }
    rotate_scale(pos, origin, m, vx, vy, vz);
}

int main(void) {
    SCALE_22_5 = 1.0f / (float)cos(0.39269909262657166) - 1.0f;
    SCALE_GEN  = 1.0f / (float)cos((3.141592653589793 / 4.0)) - 1.0f;
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        char *p = line;
        unsigned long tok[24];
        int n = 0;
        for (; n < 24; ++n) {
            while (*p == ' ' || *p == '\t') ++p;
            if (*p == '\0' || *p == '\n') break;
            tok[n] = strtoul(p, &p, 16);
        }
        if (n != 24) continue;
        int k = 0;
        int vertexIndex = (int)tok[k++];
        int facing = (int)tok[k++];
        int shade = (int)tok[k++];
        float bounds[6];
        for (int i = 0; i < 6; ++i) bounds[i] = b2f((uint32_t)tok[k++]);
        int uvQuarter = (int)tok[k++];
        float uvs[4];
        for (int i = 0; i < 4; ++i) uvs[i] = b2f((uint32_t)tok[k++]);
        float minU = b2f((uint32_t)tok[k++]), maxU = b2f((uint32_t)tok[k++]);
        float minV = b2f((uint32_t)tok[k++]), maxV = b2f((uint32_t)tok[k++]);
        int axis = (int)tok[k++];
        float angle = b2f((uint32_t)tok[k++]);
        float origin[3] = { b2f((uint32_t)tok[k]), b2f((uint32_t)tok[k+1]), b2f((uint32_t)tok[k+2]) };
        k += 3;
        int rescale = (int)tok[k++];

        int shadeColor = shade ? getFaceShadeColor(facing) : -1;
        const int *vi = VINFO[facing][vertexIndex];
        float pos[3] = { bounds[vi[0]], bounds[vi[1]], bounds[vi[2]] };
        rotate_part(pos, axis, angle, origin, rescale);

        double uIn = (double)getVertexU(uvs, vertexIndex, uvQuarter) * .999
                     + getVertexU(uvs, (vertexIndex + 2) % 4, uvQuarter) * .001;
        double vIn = (double)getVertexV(uvs, vertexIndex, uvQuarter) * .999
                     + getVertexV(uvs, (vertexIndex + 2) % 4, uvQuarter) * .001;
        float u = getInterpolatedU(minU, maxU, uIn);
        float v = getInterpolatedV(minV, maxV, vIn);

        printf("%x %x %x %x %x %x %x\n",
               f2b(pos[0]), f2b(pos[1]), f2b(pos[2]),
               (uint32_t)shadeColor, f2b(u), f2b(v), 0u);
    }
    return 0;
}
