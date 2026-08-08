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

    private static float pitch(Random random, boolean splash) {
        float result = 1.0F
            + (random.nextFloat() - random.nextFloat()) * 0.4F;
        if (splash) {
            for (int i = 0; (float)i < 1.0F + 0.6F * 20.0F; ++i) {
                random.nextFloat();
                random.nextFloat();
                random.nextFloat();
            }
            for (int i = 0; (float)i < 1.0F + 0.6F * 20.0F; ++i) {
                random.nextFloat();
                random.nextFloat();
            }
        }
        return result;
    }

    private static Random fromRawSeed(long seed48) {
        return new Random(seed48 ^ MULT);
    }

    private static void oneSwim() {
        Random random = fromRawSeed(0x123456789abcL);
        float volume = volume(false, 0.125D, -0.0784000015258789D, 0.75D);
        float pitch = pitch(random, false);
        System.out.printf("A %08x %08x %08x%n",
            Float.floatToRawIntBits(volume),
            Float.floatToRawIntBits(pitch),
            Float.floatToRawIntBits(random.nextFloat()));
    }

    private static void splashThenSwim() {
        Random random = fromRawSeed(0x0fedcba98765L);
        float splashVolume = volume(true, 0.25D, -0.5D, 7.0D);
        float splashPitch = pitch(random, true);
        float swimVolume = volume(false, 0.0D, 0.0D, 7.0D);
        float swimPitch = pitch(random, false);
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
        float pitch = pitch(random, false);
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
