import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Random;

public final class NaturalSpawnShuffleGolden {
    private static final class Pos {
        final int x, z;
        Pos(int x, int z) { this.x = x; this.z = z; }
        @Override public int hashCode() {
            int a = 1664525 * x + 1013904223;
            int b = 1664525 * (z ^ -559038737) + 1013904223;
            return a ^ b;
        }
        @Override public boolean equals(Object value) {
            return value instanceof Pos && ((Pos)value).x == x
                && ((Pos)value).z == z;
        }
    }

    private static final class TrackedRandom extends Random {
        private static final long serialVersionUID = 1L;
        long cursor;
        TrackedRandom(long seed) {
            super(0L);
            cursor = (seed ^ 0x5DEECE66DL) & ((1L << 48) - 1L);
        }
        @Override protected int next(int bits) {
            cursor = (cursor * 0x5DEECE66DL + 0xBL) & ((1L << 48) - 1L);
            return (int)(cursor >>> (48 - bits));
        }
    }

    public static void main(String[] args) {
        HashSet<Pos> eligible = new HashSet<>();
        int playerX = -3, playerZ = 7;
        for (int dx = -8; dx <= 8; ++dx)
            for (int dz = -8; dz <= 8; ++dz)
                if (dx != -8 && dx != 8 && dz != -8 && dz != 8
                        && Math.abs(dx) <= 2 && Math.abs(dz) <= 2)
                    eligible.add(new Pos(playerX + dx, playerZ + dz));
        List<Pos> values = new ArrayList<>(eligible);
        TrackedRandom random = new TrackedRandom(0x123456789ABCL);
        Collections.shuffle(values, random);
        for (Pos value : values)
            System.out.println(value.x + "," + value.z);
        System.out.println("seed48=" + random.cursor);
    }
}
