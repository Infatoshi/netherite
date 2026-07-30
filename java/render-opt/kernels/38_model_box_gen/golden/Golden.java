// GOLDEN: verbatim-logic port of MC 1.11.2 ModelBox ctor + TexturedQuad ctor + TexturedQuad.draw
// normal math. Sources:
//   src/net/minecraft/client/model/ModelBox.java:27/32 (the 2-arg + full ctor)
//   src/net/minecraft/client/model/TexturedQuad.java:22 (UV-setting ctor) + :48 draw() (normal)
//   src/net/minecraft/util/math/Vec3d.java (ctor -0.0->+0.0, subtractReverse, crossProduct, normalize)
// Bodies copied UNCHANGED; the only adaptations remove standalone-uncompilable deps without changing
// the computed geometry:
//   - ModelRenderer renderer -> textureWidth/textureHeight/mirror passed as inputs
//   - PositionTextureVertex reduced to (Vec3d pos, float u, float v)
//   - Vec3d inlined verbatim (incl. MathHelper.sqrt = (float)Math.sqrt(double) used by normalize)
//   - GL emit in draw() dropped; we keep only the normal computation (the per-vertex pos/tex/normal
//     submit is GL-bound). invertNormal has no setter in this path -> always false.
// Input  (per line, 13 tokens): texU texV x(fhex) y(fhex) z(fhex) dx dy dz delta(fhex) mirror(0/1)
//                               texW(fhex) texH(fhex)
// Output per record (38 lines):
//   8 lines: corner positions "x y z" (hex float-bits), vertexPositions[0..7] order
//   then per quad 0..5: 4 lines "u v" (hex float-bits, final/flip order) + 1 line "nx ny nz"
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

public class Golden {
    // --- verbatim Vec3d (net.minecraft.util.math.Vec3d) ---
    static final class Vec3d {
        static final Vec3d ZERO = new Vec3d(0.0D, 0.0D, 0.0D);
        final double xCoord, yCoord, zCoord;
        Vec3d(double x, double y, double z) {
            if (x == -0.0D) x = 0.0D;
            if (y == -0.0D) y = 0.0D;
            if (z == -0.0D) z = 0.0D;
            this.xCoord = x; this.yCoord = y; this.zCoord = z;
        }
        Vec3d subtractReverse(Vec3d vec) {
            return new Vec3d(vec.xCoord - this.xCoord, vec.yCoord - this.yCoord, vec.zCoord - this.zCoord);
        }
        Vec3d normalize() {
            double d0 = (double) mhSqrt(this.xCoord * this.xCoord + this.yCoord * this.yCoord + this.zCoord * this.zCoord);
            return d0 < 1.0E-4D ? ZERO : new Vec3d(this.xCoord / d0, this.yCoord / d0, this.zCoord / d0);
        }
        Vec3d crossProduct(Vec3d vec) {
            return new Vec3d(this.yCoord * vec.zCoord - this.zCoord * vec.yCoord,
                             this.zCoord * vec.xCoord - this.xCoord * vec.zCoord,
                             this.xCoord * vec.yCoord - this.yCoord * vec.xCoord);
        }
    }
    // MathHelper.sqrt(double) = (float)Math.sqrt(value)
    static float mhSqrt(double value) { return (float) Math.sqrt(value); }

    // PositionTextureVertex reduced to position + uv
    static final class PTV {
        Vec3d v3d; float u, v;
        PTV(float x, float y, float z) { this.v3d = new Vec3d((double) x, (double) y, (double) z); this.u = 0.0F; this.v = 0.0F; }
        PTV(PTV o, float u, float v) { this.v3d = o.v3d; this.u = u; this.v = v; }
        PTV setTexturePosition(float u, float v) { return new PTV(this, u, v); }
    }

    static final class TQuad {
        PTV[] vp;
        TQuad(PTV[] vertices, int u1, int v1, int u2, int v2, float tw, float th) {
            this.vp = vertices;
            float f = 0.0F / tw;
            float f1 = 0.0F / th;
            vertices[0] = vertices[0].setTexturePosition((float) u2 / tw - f, (float) v1 / th + f1);
            vertices[1] = vertices[1].setTexturePosition((float) u1 / tw + f, (float) v1 / th + f1);
            vertices[2] = vertices[2].setTexturePosition((float) u1 / tw + f, (float) v2 / th - f1);
            vertices[3] = vertices[3].setTexturePosition((float) u2 / tw - f, (float) v2 / th - f1);
        }
        void flipFace() {
            PTV[] a = new PTV[this.vp.length];
            for (int i = 0; i < this.vp.length; ++i) a[i] = this.vp[this.vp.length - i - 1];
            this.vp = a;
        }
    }

    static StringBuilder sb = new StringBuilder();

    static void buildBox(int texU, int texV, float x, float y, float z, int dx, int dy, int dz,
                         float delta, boolean mirror, float textureWidth, float textureHeight) {
        PTV[] vertexPositions = new PTV[8];
        TQuad[] quadList = new TQuad[6];
        float f = x + (float) dx;
        float f1 = y + (float) dy;
        float f2 = z + (float) dz;
        x = x - delta; y = y - delta; z = z - delta;
        f = f + delta; f1 = f1 + delta; f2 = f2 + delta;
        if (mirror) { float f3 = f; f = x; x = f3; }

        PTV p7 = new PTV(x, y, z);
        PTV p = new PTV(f, y, z);
        PTV p1 = new PTV(f, f1, z);
        PTV p2 = new PTV(x, f1, z);
        PTV p3 = new PTV(x, y, f2);
        PTV p4 = new PTV(f, y, f2);
        PTV p5 = new PTV(f, f1, f2);
        PTV p6 = new PTV(x, f1, f2);
        vertexPositions[0] = p7; vertexPositions[1] = p; vertexPositions[2] = p1; vertexPositions[3] = p2;
        vertexPositions[4] = p3; vertexPositions[5] = p4; vertexPositions[6] = p5; vertexPositions[7] = p6;
        quadList[0] = new TQuad(new PTV[]{p4, p, p1, p5}, texU + dz + dx, texV + dz, texU + dz + dx + dz, texV + dz + dy, textureWidth, textureHeight);
        quadList[1] = new TQuad(new PTV[]{p7, p3, p6, p2}, texU, texV + dz, texU + dz, texV + dz + dy, textureWidth, textureHeight);
        quadList[2] = new TQuad(new PTV[]{p4, p3, p7, p}, texU + dz, texV, texU + dz + dx, texV + dz, textureWidth, textureHeight);
        quadList[3] = new TQuad(new PTV[]{p1, p2, p6, p5}, texU + dz + dx, texV + dz, texU + dz + dx + dx, texV, textureWidth, textureHeight);
        quadList[4] = new TQuad(new PTV[]{p, p7, p2, p1}, texU + dz, texV + dz, texU + dz + dx, texV + dz + dy, textureWidth, textureHeight);
        quadList[5] = new TQuad(new PTV[]{p3, p4, p5, p6}, texU + dz + dx + dz, texV + dz, texU + dz + dx + dz + dx, texV + dz + dy, textureWidth, textureHeight);
        if (mirror) { for (TQuad q : quadList) q.flipFace(); }

        // 8 corner positions (the float coords stored into each PTV)
        for (int i = 0; i < 8; ++i) {
            Vec3d v = vertexPositions[i].v3d;
            // print the corner coords; they were created from floats, output as float bits
            fline((float) v.xCoord, (float) v.yCoord, (float) v.zCoord);
        }
        // per quad: final UVs (post-flip order) + the draw() normal
        for (int qi = 0; qi < 6; ++qi) {
            TQuad q = quadList[qi];
            for (int i = 0; i < 4; ++i) f2line(q.vp[i].u, q.vp[i].v);
            // draw() normal: vec3d = vp[1].subtractReverse(vp[0]); vec3d1 = vp[1].subtractReverse(vp[2]);
            //               normal = vec3d1.crossProduct(vec3d).normalize()
            Vec3d vec3d = q.vp[1].v3d.subtractReverse(q.vp[0].v3d);
            Vec3d vec3d1 = q.vp[1].v3d.subtractReverse(q.vp[2].v3d);
            Vec3d vec3d2 = vec3d1.crossProduct(vec3d).normalize();
            fline((float) vec3d2.xCoord, (float) vec3d2.yCoord, (float) vec3d2.zCoord);
        }
    }

    static void fline(float a, float b, float c) {
        sb.append(Long.toHexString(Float.floatToRawIntBits(a) & 0xFFFFFFFFL)).append(' ');
        sb.append(Long.toHexString(Float.floatToRawIntBits(b) & 0xFFFFFFFFL)).append(' ');
        sb.append(Long.toHexString(Float.floatToRawIntBits(c) & 0xFFFFFFFFL)).append('\n');
    }
    static void f2line(float a, float b) {
        sb.append(Long.toHexString(Float.floatToRawIntBits(a) & 0xFFFFFFFFL)).append(' ');
        sb.append(Long.toHexString(Float.floatToRawIntBits(b) & 0xFFFFFFFFL)).append('\n');
    }

    public static void main(String[] args) throws IOException {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            String[] t = line.split("\\s+");
            if (t.length != 12) continue;
            int texU = Integer.parseInt(t[0]);
            int texV = Integer.parseInt(t[1]);
            float x = Float.intBitsToFloat((int) Long.parseLong(t[2], 16));
            float y = Float.intBitsToFloat((int) Long.parseLong(t[3], 16));
            float z = Float.intBitsToFloat((int) Long.parseLong(t[4], 16));
            int dx = Integer.parseInt(t[5]);
            int dy = Integer.parseInt(t[6]);
            int dz = Integer.parseInt(t[7]);
            float delta = Float.intBitsToFloat((int) Long.parseLong(t[8], 16));
            boolean mirror = Integer.parseInt(t[9]) != 0;
            float texW = Float.intBitsToFloat((int) Long.parseLong(t[10], 16));
            float texH = Float.intBitsToFloat((int) Long.parseLong(t[11], 16));
            buildBox(texU, texV, x, y, z, dx, dy, dz, delta, mirror, texW, texH);
        }
        System.out.print(sb);
    }
}
