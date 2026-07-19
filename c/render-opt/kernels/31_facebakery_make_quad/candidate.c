/* CANDIDATE: C port of MC 1.11.2 FaceBakery.makeBakedQuad() -> BakedQuad int[28].
 * Must BITWISE-match golden/Golden.java. Op order preserved; build with -ffp-contract=off.
 * makeQuadVertexData is called with shade=false, so lane 3 is always -1.
 * GOTCHA: fillNormal widens the stored INT bits to float BY VALUE (signed), not by bit-reinterpret.
 * Input  (per line, 23 tokens, all base-16; small ints 0..5 so base16==base10):
 *   fx fy fz tx ty tz facing uvQuarter uvs[4] minU maxU minV maxV partPresent axis angle ox oy oz rescale
 * Output (per line): 28 hex ints (vertexData) + 1 decimal = derived facing ordinal. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static float b2f(uint32_t b) { float f; memcpy(&f, &b, sizeof f); return f; }
static uint32_t f2b(float f) { uint32_t b; memcpy(&b, &f, sizeof b); return b; }

static float SCALE_22_5, SCALE_GEN;

static const int VINFO[6][4][3] = {
    {{4,0,3},{4,0,2},{5,0,2},{5,0,3}},
    {{4,1,2},{4,1,3},{5,1,3},{5,1,2}},
    {{5,1,2},{5,0,2},{4,0,2},{4,1,2}},
    {{4,1,3},{4,0,3},{5,0,3},{5,1,3}},
    {{4,1,2},{4,0,2},{4,0,3},{4,1,3}},
    {{5,1,3},{5,0,3},{5,0,2},{5,1,2}},
};
static const int DIRVEC[6][3] = {{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};

static int getVertexRotated(int idx, int q) { return (idx + q) % 4; }
static float getVertexU(const float *uvs, int idx, int q) { int i=getVertexRotated(idx,q); return (i!=0 && i!=1)?uvs[2]:uvs[0]; }
static float getVertexV(const float *uvs, int idx, int q) { int i=getVertexRotated(idx,q); return (i!=0 && i!=3)?uvs[3]:uvs[1]; }
static float getInterpolatedU(float minU, float maxU, double u) { float f=maxU-minU; return minU + f*(float)u/16.0f; }
static float getInterpolatedV(float minV, float maxV, double v) { float f=maxV-minV; return minV + f*(float)v/16.0f; }

static void lwjgl_rotate(float angle, float ax, float ay, float az, float *m) {
    float c=(float)cos(angle), s=(float)sin(angle), oneminusc=1.0f-c;
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
    rx*=scx; ry*=scy; rz*=scz; p[0]=rx+o[0]; p[1]=ry+o[1]; p[2]=rz+o[2];
}
static void rotate_part(float *pos, int axis, float angle, const float *origin, int rescale) {
    if (axis > 2) return;
    float m[16]={0}; m[0]=1; m[5]=1; m[10]=1; m[15]=1;
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

static int getFacingFromVertexData(const int32_t *d) {
    float v0x=b2f((uint32_t)d[0]), v0y=b2f((uint32_t)d[1]), v0z=b2f((uint32_t)d[2]);
    float v1x=b2f((uint32_t)d[7]), v1y=b2f((uint32_t)d[8]), v1z=b2f((uint32_t)d[9]);
    float v2x=b2f((uint32_t)d[14]), v2y=b2f((uint32_t)d[15]), v2z=b2f((uint32_t)d[16]);
    float ax=v0x-v1x, ay=v0y-v1y, az=v0z-v1z;
    float bx=v2x-v1x, by=v2y-v1y, bz=v2z-v1z;
    float cx = by*az - bz*ay;
    float cy = ax*bz - az*bx;
    float cz = bx*ay - by*ax;
    float f = (float)sqrt((double)(cx*cx + cy*cy + cz*cz));
    cx /= f; cy /= f; cz /= f;
    int best = -1; float f1 = 0.0f;
    for (int e = 0; e < 6; ++e) {
        float f2 = cx*DIRVEC[e][0] + cy*DIRVEC[e][1] + cz*DIRVEC[e][2];
        if (f2 >= 0.0f && f2 > f1) { f1 = f2; best = e; }
    }
    return best < 0 ? 1 : best;
}

static int epsilonEquals(float a, float b) { return fabsf(b - a) < 1.0E-5f; }

static void applyFacing(int32_t *d, int face) {
    int32_t cp[28];
    memcpy(cp, d, sizeof cp);
    float af[6];
    af[4]=999.0f; af[0]=999.0f; af[2]=999.0f; af[5]=-999.0f; af[1]=-999.0f; af[3]=-999.0f;
    for (int i = 0; i < 4; ++i) {
        int j = 7*i;
        float f=b2f((uint32_t)cp[j]), f1=b2f((uint32_t)cp[j+1]), f2=b2f((uint32_t)cp[j+2]);
        if (f < af[4]) af[4]=f;
        if (f1 < af[0]) af[0]=f1;
        if (f2 < af[2]) af[2]=f2;
        if (f > af[5]) af[5]=f;
        if (f1 > af[1]) af[1]=f1;
        if (f2 > af[3]) af[3]=f2;
    }
    for (int i1 = 0; i1 < 4; ++i1) {
        int j1 = 7*i1;
        const int *vi = VINFO[face][i1];
        float f8=af[vi[0]], f3=af[vi[1]], f4=af[vi[2]];
        d[j1]=(int32_t)f2b(f8); d[j1+1]=(int32_t)f2b(f3); d[j1+2]=(int32_t)f2b(f4);
        for (int k = 0; k < 4; ++k) {
            int l = 7*k;
            float f5=b2f((uint32_t)cp[l]), f6=b2f((uint32_t)cp[l+1]), f7=b2f((uint32_t)cp[l+2]);
            if (epsilonEquals(f8,f5) && epsilonEquals(f3,f6) && epsilonEquals(f4,f7)) {
                d[j1+4]=cp[l+4]; d[j1+4+1]=cp[l+4+1];
            }
        }
    }
}

static void fillNormal(int32_t *d, int face) {
    (void)face;
    float v1x=(float)d[21], v1y=(float)d[22], v1z=(float)d[23];
    float tx=(float)d[7], ty=(float)d[8], tz=(float)d[9];
    float v2x=(float)d[14], v2y=(float)d[15], v2z=(float)d[16];
    v1x -= tx; v1y -= ty; v1z -= tz;
    tx=(float)d[0]; ty=(float)d[1]; tz=(float)d[2];
    v2x -= tx; v2y -= ty; v2z -= tz;
    float nx = v2y*v1z - v2z*v1y;
    float ny = v1x*v2z - v1z*v2x;
    float nz = v2x*v1y - v2y*v1x;
    v1x=nx; v1y=ny; v1z=nz;
    float fn = (float)(1.0 / sqrt((double)(v1x*v1x + v1y*v1y + v1z*v1z)));
    v1x*=fn; v1y*=fn; v1z*=fn;
    int x = ((int8_t)(int)(v1x * 127)) & 0xFF;
    int y = ((int8_t)(int)(v1y * 127)) & 0xFF;
    int z = ((int8_t)(int)(v1z * 127)) & 0xFF;
    for (int i = 0; i < 4; ++i) d[i*7 + 6] = x | (y << 8) | (z << 16);
}

int main(void) {
    SCALE_22_5 = 1.0f / (float)cos(0.39269909262657166) - 1.0f;
    SCALE_GEN  = 1.0f / (float)cos((3.141592653589793 / 4.0)) - 1.0f;
    char line[2048];
    while (fgets(line, sizeof line, stdin)) {
        char *p = line;
        unsigned long tok[23];
        int n = 0;
        for (; n < 23; ++n) {
            while (*p == ' ' || *p == '\t') ++p;
            if (*p == '\0' || *p == '\n') break;
            tok[n] = strtoul(p, &p, 16);
        }
        if (n != 23) continue;
        int k = 0;
        float fx=b2f((uint32_t)tok[k++]), fy=b2f((uint32_t)tok[k++]), fz=b2f((uint32_t)tok[k++]);
        float tx=b2f((uint32_t)tok[k++]), ty=b2f((uint32_t)tok[k++]), tz=b2f((uint32_t)tok[k++]);
        int facing = (int)tok[k++];
        int uvQuarter = (int)tok[k++];
        float uvs[4] = { b2f((uint32_t)tok[k]), b2f((uint32_t)tok[k+1]), b2f((uint32_t)tok[k+2]), b2f((uint32_t)tok[k+3]) };
        k += 4;
        float minU=b2f((uint32_t)tok[k++]), maxU=b2f((uint32_t)tok[k++]), minV=b2f((uint32_t)tok[k++]), maxV=b2f((uint32_t)tok[k++]);
        int partPresent = (int)tok[k++];
        int axis = (int)tok[k++];
        float angle = b2f((uint32_t)tok[k++]);
        float origin[3] = { b2f((uint32_t)tok[k]), b2f((uint32_t)tok[k+1]), b2f((uint32_t)tok[k+2]) };
        k += 3;
        int rescale = (int)tok[k++];
        int partAxis = partPresent ? axis : 3;

        float bounds[6];
        bounds[4]=fx/16.0f; bounds[0]=fy/16.0f; bounds[2]=fz/16.0f;
        bounds[5]=tx/16.0f; bounds[1]=ty/16.0f; bounds[3]=tz/16.0f;

        int32_t d[28] = {0};
        for (int vidx = 0; vidx < 4; ++vidx) {
            int shadeColor = -1;
            const int *vi = VINFO[facing][vidx];
            float pos[3] = { bounds[vi[0]], bounds[vi[1]], bounds[vi[2]] };
            rotate_part(pos, partAxis, angle, origin, rescale);
            double uIn = (double)getVertexU(uvs, vidx, uvQuarter) * .999 + getVertexU(uvs, (vidx+2)%4, uvQuarter) * .001;
            double vIn = (double)getVertexV(uvs, vidx, uvQuarter) * .999 + getVertexV(uvs, (vidx+2)%4, uvQuarter) * .001;
            float u = getInterpolatedU(minU, maxU, uIn);
            float v = getInterpolatedV(minV, maxV, vIn);
            int o = vidx*7;
            d[o]=(int32_t)f2b(pos[0]); d[o+1]=(int32_t)f2b(pos[1]); d[o+2]=(int32_t)f2b(pos[2]);
            d[o+3]=shadeColor; d[o+4]=(int32_t)f2b(u); d[o+5]=(int32_t)f2b(v);
        }
        int enumfacing = getFacingFromVertexData(d);
        if (!partPresent) applyFacing(d, enumfacing);
        fillNormal(d, enumfacing);

        for (int i = 0; i < 28; ++i) {
            if (i > 0) putchar(' ');
            printf("%x", (uint32_t)d[i]);
        }
        printf(" %d\n", enumfacing);
    }
    return 0;
}
