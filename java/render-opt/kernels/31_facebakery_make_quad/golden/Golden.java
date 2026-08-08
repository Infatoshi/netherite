// GOLDEN: verbatim-logic port of MC 1.11.2 FaceBakery.makeBakedQuad() (line ~55) producing the
// BakedQuad int[28]. Composes (all bodies copied UNCHANGED):
//   getPositionsDiv16 (~124), makeQuadVertexData->fillVertexData (~141)/storeVertexData (~152),
//   rotatePart (~163), getFacingFromVertexData (~242), applyFacing (~283),
//   ForgeHooksClient.fillNormal (~620, javax.vecmath ops inlined).
//
// Faithful to makeBakedQuad: makeQuadVertexData is called with shade=FALSE (hardcoded at the call
// site), so every vertex's lane 3 (shadeColor) is -1; the `shade` arg only tags the BakedQuad object.
//
// Purity isolation (does not change the int[28]):
//   - modelRotation = X0_Y0 (identity): rotate(facing)=facing, rotateVertex is a no-op. The
//     non-identity model-rotation matrix path is kernel 33's domain.
//   - uvLock=false: post-lock BlockFaceUV.uvs[4] fed directly (applyUVLock mooted).
//   - TextureAtlasSprite -> (minU,maxU,minV,maxV). BlockPartRotation -> axis/angle/origin/rescale.
//   - partRotation present/absent: when absent (partPresent=0) the `partRotation==null` branch runs
//     applyFacing (and rotatePart is a no-op); when present, rotatePart runs and applyFacing is skipped
//     -- exactly as makeBakedQuad branches.
//
// Input  (per line, 23 tokens; floats=raw 32-bit hex, ints decimal 0..5):
//   fx fy fz  tx ty tz  facing  uvQuarter  uvs[4]  minU maxU minV maxV  partPresent axis angle ox oy oz rescale
//   (posFrom=fx,fy,fz and posTo=tx,ty,tz are model space [0,16]; getPositionsDiv16 divides by 16.)
// Output (per line): 28 hex ints (the BakedQuad vertexData) + 1 decimal = derived facing ordinal.
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

public class Golden {
    static final float SCALE_ROTATION_22_5 = 1.0F / (float)Math.cos(0.39269909262657166D) - 1.0F;
    static final float SCALE_ROTATION_GENERAL = 1.0F / (float)Math.cos((Math.PI / 4D)) - 1.0F;

    // EnumFacing indices: DOWN=0 UP=1 NORTH=2 SOUTH=3 WEST=4 EAST=5
    static final int[][][] VINFO = {
        {{4,0,3},{4,0,2},{5,0,2},{5,0,3}},
        {{4,1,2},{4,1,3},{5,1,3},{5,1,2}},
        {{5,1,2},{5,0,2},{4,0,2},{4,1,2}},
        {{4,1,3},{4,0,3},{5,0,3},{5,1,3}},
        {{4,1,2},{4,0,2},{4,0,3},{4,1,3}},
        {{5,1,3},{5,0,3},{5,0,2},{5,1,2}},
    };
    // EnumFacing.getDirectionVec, values() order D,U,N,S,W,E
    static final int[][] DIRVEC = {{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};

    static int getVertexRotated(int idx, int q) { return (idx + q) % 4; }
    static float getVertexU(float[] uvs, int idx, int q) {
        int i = getVertexRotated(idx, q); return (i != 0 && i != 1) ? uvs[2] : uvs[0];
    }
    static float getVertexV(float[] uvs, int idx, int q) {
        int i = getVertexRotated(idx, q); return (i != 0 && i != 3) ? uvs[3] : uvs[1];
    }
    static float getInterpolatedU(float minU, float maxU, double u) { float f = maxU - minU; return minU + f * (float)u / 16.0F; }
    static float getInterpolatedV(float minV, float maxV, double v) { float f = maxV - minV; return minV + f * (float)v / 16.0F; }

    static void lwjglRotate(float angle, float ax, float ay, float az, float[] m) {
        float c = (float)Math.cos(angle), s = (float)Math.sin(angle), oneminusc = 1.0F - c;
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
    static void rotateScale(float[] p, float[] o, float[] m, float scx, float scy, float scz) {
        float x=p[0]-o[0], y=p[1]-o[1], z=p[2]-o[2], w=1.0F;
        float rx=m[0]*x+m[4]*y+m[8]*z+m[12]*w, ry=m[1]*x+m[5]*y+m[9]*z+m[13]*w, rz=m[2]*x+m[6]*y+m[10]*z+m[14]*w;
        rx*=scx; ry*=scy; rz*=scz; p[0]=rx+o[0]; p[1]=ry+o[1]; p[2]=rz+o[2];
    }
    static void rotatePart(float[] pos, int axis, float angle, float[] origin, boolean rescale) {
        if (axis > 2) return;
        float[] m = new float[16]; m[0]=1; m[5]=1; m[10]=1; m[15]=1;
        float vx, vy, vz;
        switch (axis) {
            case 0: lwjglRotate(angle*0.017453292F,1,0,0,m); vx=0;vy=1;vz=1; break;
            case 1: lwjglRotate(angle*0.017453292F,0,1,0,m); vx=1;vy=0;vz=1; break;
            default: lwjglRotate(angle*0.017453292F,0,0,1,m); vx=1;vy=1;vz=0; break;
        }
        if (rescale) {
            float sc = (Math.abs(angle) == 22.5F) ? SCALE_ROTATION_22_5 : SCALE_ROTATION_GENERAL;
            vx*=sc; vy*=sc; vz*=sc; vx+=1.0F; vy+=1.0F; vz+=1.0F;
        } else { vx=1.0F; vy=1.0F; vz=1.0F; }
        rotateScale(pos, origin, m, vx, vy, vz);
    }

    // verbatim getFacingFromVertexData
    static int getFacingFromVertexData(int[] d) {
        float v0x=Float.intBitsToFloat(d[0]), v0y=Float.intBitsToFloat(d[1]), v0z=Float.intBitsToFloat(d[2]);
        float v1x=Float.intBitsToFloat(d[7]), v1y=Float.intBitsToFloat(d[8]), v1z=Float.intBitsToFloat(d[9]);
        float v2x=Float.intBitsToFloat(d[14]), v2y=Float.intBitsToFloat(d[15]), v2z=Float.intBitsToFloat(d[16]);
        // vector3f3 = v0 - v1 ; vector3f4 = v2 - v1
        float ax=v0x-v1x, ay=v0y-v1y, az=v0z-v1z;
        float bx=v2x-v1x, by=v2y-v1y, bz=v2z-v1z;
        // vector3f5 = cross(vector3f4(=b), vector3f3(=a)) : lwjgl static cross(left=b,right=a)
        float cx = by*az - bz*ay;
        float cy = ax*bz - az*bx;   // right.x*left.z - right.z*left.x = a.x*b.z - a.z*b.x
        float cz = bx*ay - by*ax;
        float f = (float)Math.sqrt((double)(cx*cx + cy*cy + cz*cz));
        cx /= f; cy /= f; cz /= f;
        int best = -1; float f1 = 0.0F;
        for (int e = 0; e < 6; ++e) {
            float f2 = cx*DIRVEC[e][0] + cy*DIRVEC[e][1] + cz*DIRVEC[e][2];
            if (f2 >= 0.0F && f2 > f1) { f1 = f2; best = e; }
        }
        return best < 0 ? 1 : best; // null -> EnumFacing.UP (index 1)
    }

    static boolean epsilonEquals(float a, float b) { return Math.abs(b - a) < 1.0E-5F; }

    // verbatim applyFacing (face = derived facing index)
    static void applyFacing(int[] d, int face) {
        int[] cp = d.clone();
        float[] af = new float[6];
        af[4]=999.0F; af[0]=999.0F; af[2]=999.0F; af[5]=-999.0F; af[1]=-999.0F; af[3]=-999.0F;
        for (int i = 0; i < 4; ++i) {
            int j = 7*i;
            float f = Float.intBitsToFloat(cp[j]), f1 = Float.intBitsToFloat(cp[j+1]), f2 = Float.intBitsToFloat(cp[j+2]);
            if (f < af[4]) af[4]=f;
            if (f1 < af[0]) af[0]=f1;
            if (f2 < af[2]) af[2]=f2;
            if (f > af[5]) af[5]=f;
            if (f1 > af[1]) af[1]=f1;
            if (f2 > af[3]) af[3]=f2;
        }
        for (int i1 = 0; i1 < 4; ++i1) {
            int j1 = 7*i1;
            int[] vi = VINFO[face][i1];
            float f8 = af[vi[0]], f3 = af[vi[1]], f4 = af[vi[2]];
            d[j1] = Float.floatToRawIntBits(f8);
            d[j1+1] = Float.floatToRawIntBits(f3);
            d[j1+2] = Float.floatToRawIntBits(f4);
            for (int k = 0; k < 4; ++k) {
                int l = 7*k;
                float f5 = Float.intBitsToFloat(cp[l]), f6 = Float.intBitsToFloat(cp[l+1]), f7 = Float.intBitsToFloat(cp[l+2]);
                if (epsilonEquals(f8,f5) && epsilonEquals(f3,f6) && epsilonEquals(f4,f7)) {
                    d[j1+4] = cp[l+4];
                    d[j1+4+1] = cp[l+4+1];
                }
            }
        }
    }

    // verbatim ForgeHooksClient.fillNormal -- NOTE: ints widened to float BY VALUE (not bit-reinterpret)
    static void fillNormal(int[] d, int face) {
        float v1x = (float)d[21], v1y = (float)d[22], v1z = (float)d[23];
        float tx  = (float)d[7],  ty  = (float)d[8],  tz  = (float)d[9];
        float v2x = (float)d[14], v2y = (float)d[15], v2z = (float)d[16];
        v1x -= tx; v1y -= ty; v1z -= tz;            // v1.sub(t)
        tx = (float)d[0]; ty = (float)d[1]; tz = (float)d[2];   // t.set(...)
        v2x -= tx; v2y -= ty; v2z -= tz;            // v2.sub(t)
        // v1.cross(v2, v1): javax cross(a=v2, b=v1old) -> new v1
        float nx = v2y*v1z - v2z*v1y;
        float ny = v1x*v2z - v1z*v2x;               // b.x*a.z - b.z*a.x
        float nz = v2x*v1y - v2y*v1x;               // a.x*b.y - a.y*b.x
        v1x = nx; v1y = ny; v1z = nz;
        float fn = (float)(1.0 / Math.sqrt((double)(v1x*v1x + v1y*v1y + v1z*v1z))); // normalize
        v1x *= fn; v1y *= fn; v1z *= fn;
        int x = ((byte)(v1x * 127)) & 0xFF;
        int y = ((byte)(v1y * 127)) & 0xFF;
        int z = ((byte)(v1z * 127)) & 0xFF;
        for (int i = 0; i < 4; ++i) d[i*7 + 6] = x | (y << 8) | (z << 16);
    }

    static float f(String s) { return Float.intBitsToFloat((int)Long.parseLong(s, 16)); }

    public static void main(String[] args) throws IOException {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            String[] t = line.split("\\s+");
            if (t.length != 23) continue;
            int k = 0;
            float fx=f(t[k++]), fy=f(t[k++]), fz=f(t[k++]);
            float tx=f(t[k++]), ty=f(t[k++]), tz=f(t[k++]);
            int facing = Integer.parseInt(t[k++]);
            int uvQuarter = Integer.parseInt(t[k++]);
            float[] uvs = { f(t[k++]), f(t[k++]), f(t[k++]), f(t[k++]) };
            float minU=f(t[k++]), maxU=f(t[k++]), minV=f(t[k++]), maxV=f(t[k++]);
            int partPresent = Integer.parseInt(t[k++]);
            int axis = Integer.parseInt(t[k++]);
            float angle = f(t[k++]);
            float[] origin = { f(t[k++]), f(t[k++]), f(t[k++]) };
            boolean rescale = Integer.parseInt(t[k++]) != 0;
            int partAxis = (partPresent != 0) ? axis : 3; // 3 = none -> rotatePart no-op

            // getPositionsDiv16: bounds indexed by EnumFacing
            float[] bounds = new float[6];
            bounds[4] = fx / 16.0F; // WEST
            bounds[0] = fy / 16.0F; // DOWN
            bounds[2] = fz / 16.0F; // NORTH
            bounds[5] = tx / 16.0F; // EAST
            bounds[1] = ty / 16.0F; // UP
            bounds[3] = tz / 16.0F; // SOUTH

            int[] d = new int[28];
            // makeQuadVertexData: 4x fillVertexData with shade=false
            for (int vidx = 0; vidx < 4; ++vidx) {
                int shadeColor = -1; // shade=false
                int[] vi = VINFO[facing][vidx];
                float[] pos = { bounds[vi[0]], bounds[vi[1]], bounds[vi[2]] };
                rotatePart(pos, partAxis, angle, origin, rescale);
                // rotateVertex identity -> storeIndex == vidx
                double uIn = (double)getVertexU(uvs, vidx, uvQuarter) * .999 + getVertexU(uvs, (vidx+2)%4, uvQuarter) * .001;
                double vIn = (double)getVertexV(uvs, vidx, uvQuarter) * .999 + getVertexV(uvs, (vidx+2)%4, uvQuarter) * .001;
                float u = getInterpolatedU(minU, maxU, uIn);
                float v = getInterpolatedV(minV, maxV, vIn);
                int o = vidx * 7;
                d[o]   = Float.floatToRawIntBits(pos[0]);
                d[o+1] = Float.floatToRawIntBits(pos[1]);
                d[o+2] = Float.floatToRawIntBits(pos[2]);
                d[o+3] = shadeColor;
                d[o+4] = Float.floatToRawIntBits(u);
                d[o+5] = Float.floatToRawIntBits(v);
                // d[o+6] left 0
            }
            int enumfacing = getFacingFromVertexData(d);
            if (partPresent == 0) applyFacing(d, enumfacing); // partRotation == null branch
            fillNormal(d, enumfacing);

            for (int i = 0; i < 28; ++i) {
                if (i > 0) sb.append(' ');
                sb.append(Long.toHexString(d[i] & 0xFFFFFFFFL));
            }
            sb.append(' ').append(enumfacing);
            sb.append('\n');
        }
        System.out.print(sb);
    }
}
