package qrl;

import java.util.Random;
import net.minecraft.util.math.MathHelper;

/** Exact Entity player swim/splash scalar and Random-cursor oracle. */
public final class PlayerMovementSoundGolden {
    private static final long MULT = 0x5deece66dL;

    private PlayerMovementSoundGolden() { }

    private static float volume(
            boolean splash, double x, double y, double z) {
        float result = MathHelper.sqrt(
            x * x * 0.20000000298023224D + y * y
            + z * z * 0.20000000298023224D) * (splash ? 0.2F : 0.35F);
        return result > 1.0F ? 1.0F : result;
    }

    private static float pitch(Random random) {
        float result = 1.0F
            + (random.nextFloat() - random.nextFloat()) * 0.4F;
        return result;
    }

    private static void particle(int index, int kind,
            double x, double y, double z,
            double motionX, double motionY, double motionZ) {
        System.out.printf("P %02d %d %016x %016x %016x %016x %016x %016x%n",
            index, kind,
            Double.doubleToRawLongBits(x), Double.doubleToRawLongBits(y),
            Double.doubleToRawLongBits(z), Double.doubleToRawLongBits(motionX),
            Double.doubleToRawLongBits(motionY), Double.doubleToRawLongBits(motionZ));
    }

    private static void splashParticles(Random random,
            double x, double minY, double z, float width,
            double motionX, double motionY, double motionZ) {
        float y = (float)MathHelper.floor(minY) + 1.0F;
        int index = 0;
        for (int i = 0; (float)i < 1.0F + width * 20.0F; ++i) {
            float dx = (random.nextFloat() * 2.0F - 1.0F) * width;
            float dz = (random.nextFloat() * 2.0F - 1.0F) * width;
            float down = random.nextFloat() * 0.2F;
            particle(index++, 4, x + (double)dx, (double)y, z + (double)dz,
                motionX, motionY - (double)down, motionZ);
        }
        for (int i = 0; (float)i < 1.0F + width * 20.0F; ++i) {
            float dx = (random.nextFloat() * 2.0F - 1.0F) * width;
            float dz = (random.nextFloat() * 2.0F - 1.0F) * width;
            particle(index++, 5, x + (double)dx, (double)y, z + (double)dz,
                motionX, motionY, motionZ);
        }
    }

    private static Random fromRawSeed(long seed48) {
        return new Random(seed48 ^ MULT);
    }

    private static void oneSwim() {
        Random random = fromRawSeed(0x123456789abcL);
        float volume = volume(false, 0.125D, -0.0784000015258789D, 0.75D);
        float pitch = pitch(random);
        System.out.printf("A %08x %08x %08x%n",
            Float.floatToRawIntBits(volume),
            Float.floatToRawIntBits(pitch),
            Float.floatToRawIntBits(random.nextFloat()));
    }

    private static void splashThenSwim() {
        Random random = fromRawSeed(0x0fedcba98765L);
        float splashVolume = volume(true, 0.25D, -0.5D, 7.0D);
        float splashPitch = pitch(random);
        splashParticles(random, 8.5D, 7.0D, 8.5D, 0.6F,
            0.25D, -0.5D, 7.0D);
        float swimVolume = volume(false, 0.0D, 0.0D, 7.0D);
        float swimPitch = pitch(random);
        System.out.printf("B %08x %08x %08x %08x %08x%n",
            Float.floatToRawIntBits(splashVolume),
            Float.floatToRawIntBits(splashPitch),
            Float.floatToRawIntBits(swimVolume),
            Float.floatToRawIntBits(swimPitch),
            Float.floatToRawIntBits(random.nextFloat()));
    }

    private static void cappedSwim() {
        Random random = fromRawSeed(0x000000000001L);
        float volume = volume(false, 100.0D, -100.0D, 100.0D);
        float pitch = pitch(random);
        System.out.printf("C %08x %08x %08x%n",
            Float.floatToRawIntBits(volume),
            Float.floatToRawIntBits(pitch),
            Float.floatToRawIntBits(random.nextFloat()));
    }

    public static void main(String[] args) {
        oneSwim();
        splashThenSwim();
        cappedSwim();
    }
}
