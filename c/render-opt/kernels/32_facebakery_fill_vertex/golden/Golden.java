// GOLDEN: verbatim-logic port of MC 1.11.2 FaceBakery.fillVertexData() (line ~141) +
//   storeVertexData() (~152), with the helpers they call: getFaceShadeColor/getFaceBrightness (~98),
//   EnumFaceDirection vertex table (client/renderer/EnumFaceDirection.java), BlockFaceUV.getVertexU/V
//   (block/model/BlockFaceUV.java), TextureAtlasSprite.getInterpolatedU/V (texture/...:123), and
//   rotatePart() (~163, identical to kernel 33).
//
// Bodies copied UNCHANGED. Purity isolation (does not change the math):
//   - modelRotation = X0_Y0 (identity): rotate(facing)=facing and rotateVertex returns vertexIndex
//     and applies no transform (the non-identity model-rotation matrix path is kernel 33's domain).
//   - uvLock: the post-lock BlockFaceUV.uvs[4] are fed directly, so applyUVLock is mooted.
//   - TextureAtlasSprite -> its (minU,maxU,minV,maxV) fed as inputs (getInterpolatedU/V only read
//     those four); BlockPartRotation -> axis/angle/origin/rescale params.
//   - The position-bounds float[6] (FaceBakery.getPositionsDiv16 output, indexed by EnumFacing) is
//     fed directly.
//
// Input  (per line, 24 tokens; floats=raw 32-bit hex, ints decimal): vertexIndex facing shade
//   bounds[6]  uvQuarter  uvs[4]  minU maxU minV maxV  axis angle ox oy oz rescale
//   (uvQuarter = blockFaceUV.rotation/90 in 0..3; axis 0/1/2=X/Y/Z, 3=none.)
// Output (per line, 7 hex ints): the vertex's int[7] = posX posY posZ shadeColor U V normal(=0).
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

public class Golden {
    static final float SCALE_ROTATION_22_5 = 1.0F / (float)Math.cos(0.39269909262657166D) - 1.0F;
    static final float SCALE_ROTATION_GENERAL = 1.0F / (float)Math.cos((Math.PI / 4D)) - 1.0F;

    // EnumFacing indices: DOWN=0 UP=1 NORTH=2 SOUTH=3 WEST=4 EAST=5
    // EnumFaceDirection vertex table [facing][vertex] -> {xIndex,yIndex,zIndex} (EnumFacing indices)
    static final int[][][] VINFO = {
        // DOWN
        {{4,0,3},{4,0,2},{5,0,2},{5,0,3}},
        // UP
        {{4,1,2},{4,1,3},{5,1,3},{5,1,2}},
        // NORTH
        {{5,1,2},{5,0,2},{4,0,2},{4,1,2}},
        // SOUTH
        {{4,1,3},{4,0,3},{5,0,3},{5,1,3}},
        // WEST
        {{4,1,2},{4,0,2},{4,0,3},{4,1,3}},
        // EAST
        {{5,1,3},{5,0,3},{5,0,2},{5,1,2}},
    };

    // --- verbatim getFaceBrightness ---
    static float getFaceBrightness(int facing) {
        switch (facing) {
            case 0: return 0.5F;        // DOWN
            case 1: return 1.0F;        // UP
            case 2: case 3: return 0.8F; // NORTH/SOUTH
            case 4: case 5: return 0.6F; // WEST/EAST
            default: return 1.0F;
        }
    }
    static int clamp(int num, int min, int max) { return num < min ? min : (num > max ? max : num); }
    // --- verbatim getFaceShadeColor ---
    static int getFaceShadeColor(int facing) {
        float f = getFaceBrightness(facing);
        int i = clamp((int)(f * 255.0F), 0, 255);
        return -16777216 | i << 16 | i << 8 | i;
    }

    // --- verbatim BlockFaceUV.getVertexU/V (rotation -> uvQuarter = rotation/90) ---
    static int getVertexRotated(int idx, int uvQuarter) { return (idx + uvQuarter) % 4; }
    static float getVertexU(float[] uvs, int idx, int uvQuarter) {
        int i = getVertexRotated(idx, uvQuarter);
        return (i != 0 && i != 1) ? uvs[2] : uvs[0];
    }
    static float getVertexV(float[] uvs, int idx, int uvQuarter) {
        int i = getVertexRotated(idx, uvQuarter);
        return (i != 0 && i != 3) ? uvs[3] : uvs[1];
    }
    // --- verbatim TextureAtlasSprite.getInterpolatedU/V ---
    static float getInterpolatedU(float minU, float maxU, double u) {
        float f = maxU - minU;
        return minU + f * (float)u / 16.0F;
    }
    static float getInterpolatedV(float minV, float maxV, double v) {
        float f = maxV - minV;
        return minV + f * (float)v / 16.0F;
    }

    // --- rotatePart (identical to kernel 33; LWJGL Matrix4f.rotate + rotateScale) ---
    static void lwjglRotate(float angle, float ax, float ay, float az, float[] m) {
        float c = (float)Math.cos(angle), s = (float)Math.sin(angle), oneminusc = 1.0F - c;
        float xy = ax*ay, yz = ay*az, xz = ax*az, xs = ax*s, ys = ay*s, zs = az*s;
        float f00 = ax*ax*oneminusc + c, f01 = xy*oneminusc + zs, f02 = xz*oneminusc - ys;
        float f10 = xy*oneminusc - zs, f11 = ay*ay*oneminusc + c, f12 = yz*oneminusc + xs;
        float f20 = xz*oneminusc + ys, f21 = yz*oneminusc - xs, f22 = az*az*oneminusc + c;
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
        rx*=scx; ry*=scy; rz*=scz;
        p[0]=rx+o[0]; p[1]=ry+o[1]; p[2]=rz+o[2];
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

    static float f(String s) { return Float.intBitsToFloat((int)Long.parseLong(s, 16)); }

    public static void main(String[] args) throws IOException {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            String[] t = line.split("\\s+");
            if (t.length != 24) continue;
            int k = 0;
            int vertexIndex = Integer.parseInt(t[k++]);
            int facing = Integer.parseInt(t[k++]);
            boolean shade = Integer.parseInt(t[k++]) != 0;
            float[] bounds = new float[6];
            for (int i = 0; i < 6; ++i) bounds[i] = f(t[k++]);
            int uvQuarter = Integer.parseInt(t[k++]);
            float[] uvs = new float[4];
            for (int i = 0; i < 4; ++i) uvs[i] = f(t[k++]);
            float minU = f(t[k++]), maxU = f(t[k++]), minV = f(t[k++]), maxV = f(t[k++]);
            int axis = Integer.parseInt(t[k++]);
            float angle = f(t[k++]);
            float[] origin = { f(t[k++]), f(t[k++]), f(t[k++]) };
            boolean rescale = Integer.parseInt(t[k++]) != 0;

            // fillVertexData (identity modelRotation)
            int shadeColor = shade ? getFaceShadeColor(facing) : -1;
            int[] vi = VINFO[facing][vertexIndex];
            float[] pos = { bounds[vi[0]], bounds[vi[1]], bounds[vi[2]] };
            rotatePart(pos, axis, angle, origin, rescale);
            // rotateVertex under X0_Y0: no transform, returns vertexIndex (storeIndex == vertexIndex)

            // storeVertexData
            double uIn = (double)getVertexU(uvs, vertexIndex, uvQuarter) * .999
                         + getVertexU(uvs, (vertexIndex + 2) % 4, uvQuarter) * .001;
            double vIn = (double)getVertexV(uvs, vertexIndex, uvQuarter) * .999
                         + getVertexV(uvs, (vertexIndex + 2) % 4, uvQuarter) * .001;
            float u = getInterpolatedU(minU, maxU, uIn);
            float v = getInterpolatedV(minV, maxV, vIn);

            int[] out = {
                Float.floatToRawIntBits(pos[0]), Float.floatToRawIntBits(pos[1]), Float.floatToRawIntBits(pos[2]),
                shadeColor, Float.floatToRawIntBits(u), Float.floatToRawIntBits(v), 0
            };
            for (int i = 0; i < 7; ++i) {
                if (i > 0) sb.append(' ');
                sb.append(Long.toHexString(out[i] & 0xFFFFFFFFL));
            }
            sb.append('\n');
        }
        System.out.print(sb);
    }
}
