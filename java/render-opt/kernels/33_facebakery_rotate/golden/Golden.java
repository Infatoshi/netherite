// GOLDEN: verbatim-logic port of MC 1.11.2 FaceBakery rotation math.
// Source: src/net/minecraft/client/renderer/block/model/FaceBakery.java
//   rotatePart()   (line ~163)  + rotateScale() (~225) + the LWJGL Matrix4f.rotate/transform it calls
//   rotateVertex() (line ~212)  -> the geometric part is ForgeHooksClient.transform() (~549),
//                                  which uses javax.vecmath.Matrix4f.transform (ROW-major).
//
// The method bodies are copied UNCHANGED. The only adaptations (do not change the math):
//   - org.lwjgl.util.vector.Vector3f/Vector4f/Matrix4f ops are inlined verbatim from their
//     decompiled sources (static add/sub/scale/rotate/transform).
//   - BlockPartRotation -> (axis,angle,origin,rescale) params; EnumFacing.Axis -> int 0=X,1=Y,2=Z,-1=none.
//   - rotateVertex's ITransformation matrix is fed in as a 16-float javax.vecmath (row-major) matrix
//     input (its construction = ModelRotation.getMatrix via Forge TRSR is upstream / out of scope here;
//     identity/X0_Y0 short-circuit is covered by feeding the identity matrix).
//   - SCALE_ROTATION_* are the verbatim class constants (cos at class-init).
//
// Input  (per line, all floats as raw 32-bit hex): vx vy vz  axis  angle  ox oy oz  rescale  m[16]
//   axis: -1 = no part rotation (rotatePart is a no-op), else 0/1/2 = X/Y/Z; rescale 0/1.
//   m[16] = javax.vecmath row-major: m00 m01 m02 m03 m10 m11 m12 m13 m20 m21 m22 m23 m30 m31 m32 m33
// Output (per line): 6 hex ints = raw float bits of rotatePart(vec) then transform(thatVec, m).
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

public class Golden {
    // verbatim FaceBakery class constants
    static final float SCALE_ROTATION_22_5 = 1.0F / (float)Math.cos(0.39269909262657166D) - 1.0F;
    static final float SCALE_ROTATION_GENERAL = 1.0F / (float)Math.cos((Math.PI / 4D)) - 1.0F;

    // --- inlined org.lwjgl.util.vector.Matrix4f.rotate (static, src,dest == same identity matrix) ---
    // returns the 9 relevant 3x3 entries packed into a float[16] (col-major lwjgl layout: m[c*4+r])
    static float[] lwjglIdentity() {
        float[] m = new float[16];
        m[0]=1; m[5]=1; m[10]=1; m[15]=1;
        return m;
    }
    // lwjgl Matrix4f.rotate(angle, axis, src, dest) with src==dest==m (m starts identity)
    static void lwjglRotate(float angle, float ax, float ay, float az, float[] m) {
        float c = (float)Math.cos(angle);
        float s = (float)Math.sin(angle);
        float oneminusc = 1.0F - c;
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
        // src entries (m, lwjgl col-major: m[col*4+row]). m00=m[0] etc.
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

    // lwjgl Matrix4f.transform(left, right, dest): column-major. right=(x,y,z,w)
    static float[] lwjglTransform(float[] m, float x, float y, float z, float w) {
        float rx = m[0]*x + m[4]*y + m[8]*z + m[12]*w;
        float ry = m[1]*x + m[5]*y + m[9]*z + m[13]*w;
        float rz = m[2]*x + m[6]*y + m[10]*z + m[14]*w;
        float rw = m[3]*x + m[7]*y + m[11]*z + m[15]*w;
        return new float[]{rx, ry, rz, rw};
    }

    // verbatim rotateScale(): position is float[3], origin float[3]
    static void rotateScale(float[] position, float[] origin, float[] m, float scx, float scy, float scz) {
        float[] v4 = lwjglTransform(m, position[0]-origin[0], position[1]-origin[1], position[2]-origin[2], 1.0F);
        v4[0] *= scx; v4[1] *= scy; v4[2] *= scz;
        position[0] = v4[0] + origin[0];
        position[1] = v4[1] + origin[1];
        position[2] = v4[2] + origin[2];
    }

    // verbatim rotatePart()
    static void rotatePart(float[] pos, int axis, float angle, float[] origin, boolean rescale) {
        if (axis > 2) return; // partRotation == null (axis 3 = none; 0/1/2 = X/Y/Z)
        float[] m = lwjglIdentity();
        float vx, vy, vz;
        switch (axis) {
            case 0: // X
                lwjglRotate(angle * 0.017453292F, 1.0F, 0.0F, 0.0F, m);
                vx = 0.0F; vy = 1.0F; vz = 1.0F; break;
            case 1: // Y
                lwjglRotate(angle * 0.017453292F, 0.0F, 1.0F, 0.0F, m);
                vx = 1.0F; vy = 0.0F; vz = 1.0F; break;
            default: // Z
                lwjglRotate(angle * 0.017453292F, 0.0F, 0.0F, 1.0F, m);
                vx = 1.0F; vy = 1.0F; vz = 0.0F; break;
        }
        if (rescale) {
            float sc;
            if (Math.abs(angle) == 22.5F) sc = SCALE_ROTATION_22_5;
            else sc = SCALE_ROTATION_GENERAL;
            vx *= sc; vy *= sc; vz *= sc;
            vx += 1.0F; vy += 1.0F; vz += 1.0F;
        } else {
            vx = 1.0F; vy = 1.0F; vz = 1.0F;
        }
        rotateScale(pos, origin, m, vx, vy, vz);
    }

    // verbatim ForgeHooksClient.transform(vec, javax Matrix4f m) -- m row-major (m[r*4+c])
    static void transformVertex(float[] vec, float[] m) {
        float x = vec[0], y = vec[1], z = vec[2], w = 1.0F;
        // javax.vecmath.Matrix4f.transform(Tuple4f) -- row-major
        float f  = m[0]*x  + m[1]*y  + m[2]*z  + m[3]*w;
        float f2 = m[4]*x  + m[5]*y  + m[6]*z  + m[7]*w;
        float f3 = m[8]*x  + m[9]*y  + m[10]*z + m[11]*w;
        float tw = m[12]*x + m[13]*y + m[14]*z + m[15]*w;
        float tx = f, ty = f2, tz = f3;
        if (Math.abs(tw - 1.0F) > 1e-5) { float inv = 1.0F / tw; tx*=inv; ty*=inv; tz*=inv; tw*=inv; }
        vec[0] = tx; vec[1] = ty; vec[2] = tz;
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
            if (t.length != 25) continue;
            float[] pos = { f(t[0]), f(t[1]), f(t[2]) };
            int axis = Integer.parseInt(t[3]);
            float angle = f(t[4]);
            float[] origin = { f(t[5]), f(t[6]), f(t[7]) };
            boolean rescale = Integer.parseInt(t[8]) != 0;
            float[] m = new float[16];
            for (int i = 0; i < 16; ++i) m[i] = f(t[9 + i]);

            rotatePart(pos, axis, angle, origin, rescale);
            float[] vec = { pos[0], pos[1], pos[2] };
            transformVertex(vec, m);

            int[] out = {
                Float.floatToRawIntBits(pos[0]), Float.floatToRawIntBits(pos[1]), Float.floatToRawIntBits(pos[2]),
                Float.floatToRawIntBits(vec[0]), Float.floatToRawIntBits(vec[1]), Float.floatToRawIntBits(vec[2])
            };
            for (int i = 0; i < 6; ++i) {
                if (i > 0) sb.append(' ');
                sb.append(Long.toHexString(out[i] & 0xFFFFFFFFL));
            }
            sb.append('\n');
        }
        System.out.print(sb);
    }
}
