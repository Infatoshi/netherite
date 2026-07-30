/* GOLDEN: verbatim decompiled MC Particle.renderParticle quad math.
 * Source: src/net/minecraft/client/particle/Particle.java renderParticle() ~:183
 * Supporting verbatim: util/math/Vec3d (scale/add/dotProduct/crossProduct + the -0.0->+0.0
 * normalizing constructor) and util/math/MathHelper sin()/cos() (SIN_TABLE).
 *
 * Scope: the particleTexture==null branch (UVs derived from particleTextureIndexX/Y), which is
 * the pure path -- it reads no world/texture state. getBrightnessForRender(partialTicks) is
 * replaced by a supplied 'light' int. The VertexBuffer builder calls (pos/tex/color/lightmap) are
 * not rebuilt; instead we compute and emit the exact values handed to them, applying the same casts:
 *   pos  -> (float)((double)f5 + avec.xCoord)  [buffer.pos casts double->float, xOffset=0]
 *   tex  -> the f/f1/f2/f3 floats
 *   color-> color(float) does (int)(c*255.0F) per channel, UBYTE little-endian pack r|g<<8|b<<16|a<<24
 *   light-> j=i>>16&65535, k=i&65535
 *
 * Driver: one particle per line, fields in this order (see gen_inputs.py / README):
 *   texIdxX texIdxY scale prevX prevY prevZ posX posY posZ partialTicks interpX interpY interpZ
 *   light angle prevAngle camDirX camDirY camDirZ rotX rotZ rotYZ rotXY rotXZ red green blue alpha
 * ints are decimal; doubles are hex long-bits; floats are hex int-bits (so both sides read
 * identical values). Output: 4 lines (one per vertex):
 *   posXbits posYbits posZbits ubits vbits colorbits j k   (floats/color hex, j/k decimal). */
import java.io.BufferedReader;
import java.io.InputStreamReader;

public class Golden {
    // ---- verbatim MathHelper sin/cos ----
    private static final float[] SIN_TABLE = new float[65536];
    static {
        for (int i = 0; i < 65536; ++i)
            SIN_TABLE[i] = (float)Math.sin((double)i * Math.PI * 2.0D / 65536.0D);
    }
    public static float sin(float value) { return SIN_TABLE[(int)(value * 10430.378F) & 65535]; }
    public static float cos(float value) { return SIN_TABLE[(int)(value * 10430.378F + 16384.0F) & 65535]; }

    // ---- verbatim Vec3d (only what renderParticle uses) ----
    static final class Vec3d {
        final double xCoord, yCoord, zCoord;
        Vec3d(double x, double y, double z) {
            if (x == -0.0D) x = 0.0D;
            if (y == -0.0D) y = 0.0D;
            if (z == -0.0D) z = 0.0D;
            this.xCoord = x; this.yCoord = y; this.zCoord = z;
        }
        double dotProduct(Vec3d vec) { return this.xCoord * vec.xCoord + this.yCoord * vec.yCoord + this.zCoord * vec.zCoord; }
        Vec3d crossProduct(Vec3d vec) { return new Vec3d(this.yCoord * vec.zCoord - this.zCoord * vec.yCoord, this.zCoord * vec.xCoord - this.xCoord * vec.zCoord, this.xCoord * vec.yCoord - this.yCoord * vec.xCoord); }
        Vec3d add(Vec3d vec) { return new Vec3d(this.xCoord + vec.xCoord, this.yCoord + vec.yCoord, this.zCoord + vec.zCoord); }
        Vec3d scale(double s) { return new Vec3d(this.xCoord * s, this.yCoord * s, this.zCoord * s); }
    }

    static String fh(float f) { return Integer.toHexString(Float.floatToRawIntBits(f)); }

    public static void main(String[] args) throws Exception {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = br.readLine()) != null) {
            if (line.trim().isEmpty()) continue;
            String[] t = line.trim().split("\\s+");
            int ix = 0;
            int particleTextureIndexX = Integer.parseInt(t[ix++]);
            int particleTextureIndexY = Integer.parseInt(t[ix++]);
            float particleScale = Float.intBitsToFloat((int) Long.parseLong(t[ix++], 16));
            double prevPosX = Double.longBitsToDouble(Long.parseUnsignedLong(t[ix++], 16));
            double prevPosY = Double.longBitsToDouble(Long.parseUnsignedLong(t[ix++], 16));
            double prevPosZ = Double.longBitsToDouble(Long.parseUnsignedLong(t[ix++], 16));
            double posX = Double.longBitsToDouble(Long.parseUnsignedLong(t[ix++], 16));
            double posY = Double.longBitsToDouble(Long.parseUnsignedLong(t[ix++], 16));
            double posZ = Double.longBitsToDouble(Long.parseUnsignedLong(t[ix++], 16));
            float partialTicks = Float.intBitsToFloat((int) Long.parseLong(t[ix++], 16));
            double interpPosX = Double.longBitsToDouble(Long.parseUnsignedLong(t[ix++], 16));
            double interpPosY = Double.longBitsToDouble(Long.parseUnsignedLong(t[ix++], 16));
            double interpPosZ = Double.longBitsToDouble(Long.parseUnsignedLong(t[ix++], 16));
            int light = Integer.parseInt(t[ix++]);
            float particleAngle = Float.intBitsToFloat((int) Long.parseLong(t[ix++], 16));
            float prevParticleAngle = Float.intBitsToFloat((int) Long.parseLong(t[ix++], 16));
            double camDirX = Double.longBitsToDouble(Long.parseUnsignedLong(t[ix++], 16));
            double camDirY = Double.longBitsToDouble(Long.parseUnsignedLong(t[ix++], 16));
            double camDirZ = Double.longBitsToDouble(Long.parseUnsignedLong(t[ix++], 16));
            float rotationX = Float.intBitsToFloat((int) Long.parseLong(t[ix++], 16));
            float rotationZ = Float.intBitsToFloat((int) Long.parseLong(t[ix++], 16));
            float rotationYZ = Float.intBitsToFloat((int) Long.parseLong(t[ix++], 16));
            float rotationXY = Float.intBitsToFloat((int) Long.parseLong(t[ix++], 16));
            float rotationXZ = Float.intBitsToFloat((int) Long.parseLong(t[ix++], 16));
            float particleRed = Float.intBitsToFloat((int) Long.parseLong(t[ix++], 16));
            float particleGreen = Float.intBitsToFloat((int) Long.parseLong(t[ix++], 16));
            float particleBlue = Float.intBitsToFloat((int) Long.parseLong(t[ix++], 16));
            float particleAlpha = Float.intBitsToFloat((int) Long.parseLong(t[ix++], 16));

            Vec3d cameraViewDir = new Vec3d(camDirX, camDirY, camDirZ);

            // ---- verbatim renderParticle (particleTexture==null path) ----
            float f = (float)particleTextureIndexX / 16.0F;
            float f1 = f + 0.0624375F;
            float f2 = (float)particleTextureIndexY / 16.0F;
            float f3 = f2 + 0.0624375F;
            float f4 = 0.1F * particleScale;

            float f5 = (float)(prevPosX + (posX - prevPosX) * (double)partialTicks - interpPosX);
            float f6 = (float)(prevPosY + (posY - prevPosY) * (double)partialTicks - interpPosY);
            float f7 = (float)(prevPosZ + (posZ - prevPosZ) * (double)partialTicks - interpPosZ);
            int i = light;
            int j = i >> 16 & 65535;
            int k = i & 65535;
            Vec3d[] avec3d = new Vec3d[] {new Vec3d((double)(-rotationX * f4 - rotationXY * f4), (double)(-rotationZ * f4), (double)(-rotationYZ * f4 - rotationXZ * f4)), new Vec3d((double)(-rotationX * f4 + rotationXY * f4), (double)(rotationZ * f4), (double)(-rotationYZ * f4 + rotationXZ * f4)), new Vec3d((double)(rotationX * f4 + rotationXY * f4), (double)(rotationZ * f4), (double)(rotationYZ * f4 + rotationXZ * f4)), new Vec3d((double)(rotationX * f4 - rotationXY * f4), (double)(-rotationZ * f4), (double)(rotationYZ * f4 - rotationXZ * f4))};

            if (particleAngle != 0.0F) {
                float f8 = particleAngle + (particleAngle - prevParticleAngle) * partialTicks;
                float f9 = cos(f8 * 0.5F);
                float f10 = sin(f8 * 0.5F) * (float)cameraViewDir.xCoord;
                float f11 = sin(f8 * 0.5F) * (float)cameraViewDir.yCoord;
                float f12 = sin(f8 * 0.5F) * (float)cameraViewDir.zCoord;
                Vec3d vec3d = new Vec3d((double)f10, (double)f11, (double)f12);

                for (int l = 0; l < 4; ++l) {
                    avec3d[l] = vec3d.scale(2.0D * avec3d[l].dotProduct(vec3d)).add(avec3d[l].scale((double)(f9 * f9) - vec3d.dotProduct(vec3d))).add(vec3d.crossProduct(avec3d[l]).scale((double)(2.0F * f9)));
                }
            }
            // ---- /verbatim ----

            int cr = (int)(particleRed * 255.0F);
            int cg = (int)(particleGreen * 255.0F);
            int cb = (int)(particleBlue * 255.0F);
            int ca = (int)(particleAlpha * 255.0F);
            int color = (cr & 255) | (cg & 255) << 8 | (cb & 255) << 16 | (ca & 255) << 24;
            String ch = Integer.toHexString(color);

            float[][] uv = { {f1, f3}, {f1, f2}, {f, f2}, {f, f3} };
            for (int l = 0; l < 4; ++l) {
                float px = (float)((double)f5 + avec3d[l].xCoord);
                float py = (float)((double)f6 + avec3d[l].yCoord);
                float pz = (float)((double)f7 + avec3d[l].zCoord);
                sb.append(fh(px)).append(' ').append(fh(py)).append(' ').append(fh(pz)).append(' ')
                  .append(fh(uv[l][0])).append(' ').append(fh(uv[l][1])).append(' ')
                  .append(ch).append(' ').append(j).append(' ').append(k).append('\n');
            }
        }
        System.out.print(sb);
    }
}
