import java.util.Random;
import java.util.concurrent.atomic.AtomicLong;

/* Verbatim Java 8 Random/EntityBlaze/EntityFireball numeric oracle. */
public class Golden {
    private static final class Cloud {
        boolean active = true;
        int age;
        int duration = 600;
        int waitTime = 10;
        int reapplicationDelay = 20;
        int nextApplication;
        float radius = 3.0F;
        float radiusOnUse = -0.5F;
        float radiusPerTick = -radius / (float)duration;
    }

    private static boolean cloudTick(Cloud cloud) {
        ++cloud.age;
        if (cloud.age >= cloud.waitTime + cloud.duration) {
            cloud.active = false;
            return false;
        }
        if (cloud.age < cloud.waitTime) return false;
        cloud.radius += cloud.radiusPerTick;
        if (cloud.radius < 0.5F) {
            cloud.active = false;
            return false;
        }
        return cloud.age % 5 == 0;
    }

    private static void cloudApply(Cloud cloud) {
        cloud.nextApplication = cloud.age + cloud.reapplicationDelay;
        cloud.radius += cloud.radiusOnUse;
        if (cloud.radius < 0.5F) cloud.active = false;
    }

    private static int instantHealthDelta(
            int potionId, int amplifier, double factor, boolean undead) {
        boolean healing = potionId == 6 && !undead
            || potionId == 7 && undead;
        int amount = (int)(factor * (double)((healing ? 4 : 6) << amplifier)
            + 0.5D);
        return healing ? amount : -amount;
    }

    private static boolean effectReady(
            int potionId, int duration, int amplifier) {
        int interval;
        if (potionId == 10) interval = 50 >> amplifier;
        else if (potionId == 19) interval = 25 >> amplifier;
        else if (potionId == 20) interval = 40 >> amplifier;
        else return potionId == 17;
        return interval > 0 ? duration % interval == 0 : true;
    }

    private static double movementMultiplier(int potionId, int amplifier) {
        if (potionId == 1)
            return 1.0D + 0.20000000298023224D * (double)(amplifier + 1);
        if (potionId == 2)
            return 1.0D - 0.15000000596046448D * (double)(amplifier + 1);
        return 1.0D;
    }

    private static double attackBonus(int potionId, int amplifier) {
        if (potionId == 5) return 3.0D * (double)(amplifier + 1);
        if (potionId == 18) return -4.0D * (double)(amplifier + 1);
        return 0.0D;
    }

    private static float jumpBonus(int amplifier) {
        return (float)(amplifier + 1) * 0.1F;
    }

    private static float resistanceDamage(float damage, int amplifier) {
        int scale = 25 - (amplifier + 1) * 5;
        float reduced = damage * (float)scale / 25.0F;
        return reduced > 0.0F ? reduced : 0.0F;
    }

    private static double levitationMotion(
            double motionY, int amplifier) {
        double target = 0.05D * (double)(amplifier + 1);
        return (motionY + (target - motionY) * 0.2D)
            * 0.9800000190734863D;
    }

    private static float healthBoost(float baseHealth, int amplifier) {
        return baseHealth + 4.0F * (float)(amplifier + 1);
    }

    private static float absorbDamage(float damage, float[] absorption) {
        float available = absorption[0] > 0.0F ? absorption[0] : 0.0F;
        float healthDamage = damage > available ? damage - available : 0.0F;
        absorption[0] = available - (damage - healthDamage);
        if (absorption[0] < 0.0F) absorption[0] = 0.0F;
        return healthDamage;
    }

    private static int airStep(
            int air, boolean eyeInWater, boolean waterBreathing,
            boolean[] drownPulse) {
        drownPulse[0] = false;
        if (!eyeInWater) return 300;
        if (waterBreathing) return air;
        --air;
        if (air == -20) {
            drownPulse[0] = true;
            return 0;
        }
        return air;
    }

    private static Random entityRandom(long seed) {
        Random random = new Random(seed);
        random.nextLong();
        random.nextLong();
        return random;
    }

    private static float blazeHeightOffset(Random random) {
        return 0.5F + (float)random.nextGaussian() * 3.0F;
    }

    private static double fallDamping(boolean onGround, double motionY) {
        return !onGround && motionY < 0.0D ? motionY * 0.6D : motionY;
    }

    private static double heightImpulse(
            double motionY, double targetEyeY, double blazeEyeY,
            float heightOffset) {
        if (targetEyeY > blazeEyeY + (double)heightOffset) {
            motionY += (0.30000001192092896D - motionY)
                * 0.30000001192092896D;
        }
        return motionY;
    }

    private static double[] smallFireball(
            Random random, double x, double y, double z) {
        x = x + random.nextGaussian() * 0.4D;
        y = y + random.nextGaussian() * 0.4D;
        z = z + random.nextGaussian() * 0.4D;
        double length = (double)(float)Math.sqrt(x * x + y * y + z * z);
        return new double[] {
            x / length * 0.1D,
            y / length * 0.1D,
            z / length * 0.1D
        };
    }

    private static double[] blazeAim(
            Random random, double x, double y, double z) {
        double distanceSq = x * x + y * y + z * z;
        float spread = (float)Math.sqrt(
            (double)(float)Math.sqrt(distanceSq)) * 0.5F;
        return new double[] {
            x + random.nextGaussian() * (double)spread,
            y,
            z + random.nextGaussian() * (double)spread
        };
    }

    private static double[] throwableHeading(
            Random random, double x, double y, double z,
            float velocity, float inaccuracy) {
        float length = (float)Math.sqrt(x * x + y * y + z * z);
        x /= (double)length;
        y /= (double)length;
        z /= (double)length;
        x += random.nextGaussian() * 0.007499999832361937D
            * (double)inaccuracy;
        y += random.nextGaussian() * 0.007499999832361937D
            * (double)inaccuracy;
        z += random.nextGaussian() * 0.007499999832361937D
            * (double)inaccuracy;
        return new double[] {
            x * (double)velocity,
            y * (double)velocity,
            z * (double)velocity
        };
    }

    private static void emitDouble(double value) {
        System.out.printf("%016x%n", Double.doubleToRawLongBits(value));
    }

    private static long seed48(Random random) throws Exception {
        java.lang.reflect.Field field = Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get() & ((1L << 48) - 1L);
    }

    private static boolean haveNextGaussian(Random random) throws Exception {
        java.lang.reflect.Field field =
            Random.class.getDeclaredField("haveNextNextGaussian");
        field.setAccessible(true);
        return field.getBoolean(random);
    }

    private static double nextGaussian(Random random) throws Exception {
        java.lang.reflect.Field field =
            Random.class.getDeclaredField("nextNextGaussian");
        field.setAccessible(true);
        return field.getDouble(random);
    }

    public static void main(String[] args) throws Exception {
        long seed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;
        Random blaze = entityRandom(seed);
        System.out.printf("%08x%n",
            Float.floatToRawIntBits(blazeHeightOffset(blaze)));
        emitDouble(blaze.nextGaussian());
        emitDouble(blaze.nextGaussian());
        emitDouble(blaze.nextGaussian());

        Random fireball = entityRandom(seed);
        double[] aim = blazeAim(blaze, 2.25D, -0.75D, 4.5D);
        emitDouble(aim[0]);
        emitDouble(aim[1]);
        emitDouble(aim[2]);
        double[] acceleration = smallFireball(
            fireball, aim[0], aim[1], aim[2]);
        emitDouble(acceleration[0]);
        emitDouble(acceleration[1]);
        emitDouble(acceleration[2]);
        System.out.printf("%012x%n", seed48(fireball));
        System.out.println(1);
        emitDouble(fireball.nextGaussian());

        emitDouble(fallDamping(false, -0.125D));
        emitDouble(fallDamping(true, -0.125D));
        emitDouble(heightImpulse(
            -0.075D, 9.6200000047683716D, 6.5300000905990601D, 0.5F));
        emitDouble(heightImpulse(
            -0.075D, 6.0D, 6.5300000905990601D, 0.5F));

        Random throwable = entityRandom(seed);
        double[] heading = throwableHeading(
            throwable, -0.3125D, 0.625D, 0.71875D, 0.5F, 1.0F);
        emitDouble(heading[0]);
        emitDouble(heading[1]);
        emitDouble(heading[2]);
        System.out.printf("%012x%n", seed48(throwable));
        System.out.println(haveNextGaussian(throwable) ? 1 : 0);
        emitDouble(nextGaussian(throwable));

        Cloud cloud = new Cloud();
        System.out.printf("%08x%n", Float.floatToRawIntBits(cloud.radius));
        for (int age = 1; age <= 9; ++age) cloudTick(cloud);
        System.out.println(cloud.age);
        System.out.printf("%08x%n", Float.floatToRawIntBits(cloud.radius));
        System.out.println(cloudTick(cloud) ? 1 : 0);
        System.out.printf("%08x%n", Float.floatToRawIntBits(cloud.radius));
        cloudApply(cloud);
        System.out.println(cloud.nextApplication);
        System.out.printf("%08x%n", Float.floatToRawIntBits(cloud.radius));
        while (cloud.age < 30) {
            if (cloudTick(cloud)
                    && cloud.age >= cloud.nextApplication)
                cloudApply(cloud);
        }
        System.out.println(cloud.age);
        System.out.println(cloud.nextApplication);
        System.out.printf("%08x%n", Float.floatToRawIntBits(cloud.radius));
        System.out.println(instantHealthDelta(6, 0, 1.0D, false));
        System.out.println(instantHealthDelta(7, 1, 1.0D, false));
        System.out.println(instantHealthDelta(6, 1, 0.5D, true));
        System.out.println(instantHealthDelta(7, 1, 0.25D, true));
        System.out.println(instantHealthDelta(6, 0, 0.04D, false));
        System.out.println(instantHealthDelta(7, 0, 0.125D, false));
        System.out.println((int)(3600.0D * 0.5D + 0.5D));
        System.out.println((int)(3600.0D * 0.005D + 0.5D));
        System.out.println(effectReady(10, 50, 0) ? 1 : 0);
        System.out.println(effectReady(10, 49, 0) ? 1 : 0);
        System.out.println(effectReady(19, 12, 1) ? 1 : 0);
        System.out.println(effectReady(19, 11, 1) ? 1 : 0);
        System.out.println(effectReady(20, 5, 3) ? 1 : 0);
        System.out.println(effectReady(17, 7, 0) ? 1 : 0);
        emitDouble(movementMultiplier(1, 0));
        emitDouble(movementMultiplier(1, 1));
        emitDouble(movementMultiplier(2, 0));
        emitDouble(movementMultiplier(2, 1));
        emitDouble(movementMultiplier(1, 0) * movementMultiplier(2, 0));
        emitDouble(attackBonus(5, 0));
        emitDouble(attackBonus(5, 1));
        emitDouble(attackBonus(18, 0));
        emitDouble(attackBonus(5, 0) + attackBonus(18, 0));
        System.out.printf("%08x%n", Float.floatToRawIntBits(jumpBonus(0)));
        System.out.printf("%08x%n", Float.floatToRawIntBits(jumpBonus(1)));
        System.out.printf("%08x%n",
            Float.floatToRawIntBits(resistanceDamage(1.0F, 0)));
        System.out.printf("%08x%n",
            Float.floatToRawIntBits(resistanceDamage(6.0F, 1)));
        System.out.printf("%08x%n",
            Float.floatToRawIntBits(resistanceDamage(20.0F, 4)));
        emitDouble(levitationMotion(-0.08D, 0));
        emitDouble(levitationMotion(0.0D, 2));
        System.out.printf("%08x%n",
            Float.floatToRawIntBits(healthBoost(10.0F, 0)));
        System.out.printf("%08x%n",
            Float.floatToRawIntBits(healthBoost(10.0F, 2)));
        float[] absorption = {4.0F};
        System.out.printf("%08x%n",
            Float.floatToRawIntBits(absorbDamage(6.0F, absorption)));
        System.out.printf("%08x%n", Float.floatToRawIntBits(absorption[0]));
        absorption[0] = 8.0F;
        System.out.printf("%08x%n",
            Float.floatToRawIntBits(absorbDamage(5.0F, absorption)));
        System.out.printf("%08x%n", Float.floatToRawIntBits(absorption[0]));
        boolean[] drownPulse = {false};
        System.out.println(airStep(47, false, false, drownPulse));
        System.out.println(airStep(300, true, false, drownPulse));
        System.out.println(airStep(-19, true, true, drownPulse));
        System.out.println(airStep(-19, true, false, drownPulse));
        System.out.println(drownPulse[0] ? 1 : 0);
        System.out.println(airStep(0, true, false, drownPulse));
        Random bubbles = entityRandom(seed);
        for (int draw = 0; draw < 48; ++draw) bubbles.nextFloat();
        System.out.printf("%012x%n", seed48(bubbles));
    }
}
