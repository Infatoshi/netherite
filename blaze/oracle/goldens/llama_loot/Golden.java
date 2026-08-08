import java.util.Random;

public final class Golden {
    public static void main(String[] args) {
        for (long row = 0; row < 96; ++row) {
            long seed = 0x3456789ABCDEFL + row * 104729L;
            int looting = (int)(row % 4);
            Random random = new Random(seed);
            random.nextInt(1);
            int leather = random.nextInt(3);
            if (looting > 0)
                leather += Math.round(looting * random.nextFloat());
            System.out.printf("%d,%d,%d,%016x%n",
                    row, looting, leather, random.nextLong());
        }
    }
}
