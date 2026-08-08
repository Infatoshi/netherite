import java.util.Random;

public final class Golden {
    public static void main(String[] args) {
        for (long seed = 0; seed < 64; ++seed) {
            Random random = new Random(0x123456789ABCL + seed * 7919L);
            double firstHealth = 15.0 + seed % 17;
            double secondHealth = 15.0 + seed % 13;
            double firstJump = 0.4 + (seed % 7) * 0.05;
            double secondJump = 0.4 + (seed % 5) * 0.07;
            double firstSpeed = 0.1 + (seed % 9) * 0.02;
            double secondSpeed = 0.1 + (seed % 6) * 0.03;
            int firstStrength = 1 + (int)(seed % 5);
            int secondStrength = 1 + (int)((seed * 3) % 5);
            int firstVariant = (int)(seed % 4);
            int secondVariant = (int)((seed * 3) % 4);

            double health = (firstHealth + secondHealth + 15.0
                    + random.nextInt(8) + random.nextInt(9)) / 3.0;
            double jump = (firstJump + secondJump
                    + 0.4000000059604645
                    + random.nextDouble() * 0.2
                    + random.nextDouble() * 0.2
                    + random.nextDouble() * 0.2) / 3.0;
            double speed = (firstSpeed + secondSpeed
                    + (0.44999998807907104
                    + random.nextDouble() * 0.3
                    + random.nextDouble() * 0.3
                    + random.nextDouble() * 0.3) * 0.25) / 3.0;
            int strength = random.nextInt(
                    Math.max(firstStrength, secondStrength)) + 1;
            if (random.nextFloat() < 0.03F) ++strength;
            strength = Math.max(1, Math.min(5, strength));
            int variant = random.nextBoolean()
                    ? firstVariant : secondVariant;
            System.out.printf("%d,%016x,%016x,%016x,%d,%d%n",
                    seed, Double.doubleToRawLongBits(health),
                    Double.doubleToRawLongBits(jump),
                    Double.doubleToRawLongBits(speed), strength, variant);
        }
    }
}
