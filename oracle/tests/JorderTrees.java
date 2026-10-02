import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.HashSet;

/**
 * The golden for csrc/tests/order_test's tree windows (jorder.c
 * jord_ccp_window): each input line "r c0 pcx pcz" fills a JDK 8 HashSet
 * whose table has c0 bins (0: a new set) with the (2r+1)^2 chunks around
 * (pcx, pcz), x outer and z inner, as World.activeChunkSet and
 * SpawnerAnimals.eligibleChunksForSpawning do, and prints the line with the
 * key count, the final table and the FNV-1a 64 of the iteration order.
 * A line "b seed adds rad cx cy cz" fills a new HashSet of ChunkPosition
 * (World.doExplosionA's affected set, jorder.c jord_set3) with adds keys
 * (cx + dx, cy + dy, cz + dz), each d next() % (2 rad + 1) - rad in the
 * order dx, dy, dz, next() stepping s = s * 6364136223846793005 +
 * 1442695040888963407 from the seed and returning s >>> 33; it prints the
 * same way (x, y, z per key into the hash).
 * Run it on the oracle's JDK 8 with every identity hash one, the tie-break
 * jorder.c takes:
 *
 *   javac -d DIR JorderTrees.java
 *   java -XX:hashCode=2 -cp DIR JorderTrees < cases > csrc/tests/jorder_trees.txt
 */
public final class JorderTrees
{
    /** ChunkCoordIntPair's hashCode and equals, not Comparable. */
    static final class P
    {
        final int x, z;
        P(int x, int z) { this.x = x; this.z = z; }
        public int hashCode()
        {
            int a = 1664525 * x + 1013904223;
            int b = 1664525 * (z ^ -559038737) + 1013904223;
            return a ^ b;
        }
        public boolean equals(Object o) { return o instanceof P && ((P)o).x == x && ((P)o).z == z; }
    }

    /** ChunkPosition's hashCode and equals, not Comparable. */
    static final class Q
    {
        final int x, y, z;
        Q(int x, int y, int z) { this.x = x; this.y = y; this.z = z; }
        public int hashCode() { return x * 8976890 + y * 981131 + z; }
        public boolean equals(Object o) { return o instanceof Q && ((Q)o).x == x && ((Q)o).y == y && ((Q)o).z == z; }
    }

    static long st;
    static int next() { st = st * 6364136223846793005L + 1442695040888963407L; return (int)(st >>> 33); }

    static long fnv(long h, int v)
    {
        for (int k = 0; k < 4; ++k) { h ^= (v >>> (8 * k)) & 0xff; h *= 0x100000001b3L; }
        return h;
    }

    static int table(HashSet<?> s) throws Exception
    {
        java.lang.reflect.Field mf = HashSet.class.getDeclaredField("map");
        mf.setAccessible(true);
        Object m = mf.get(s);
        java.lang.reflect.Field tf = java.util.HashMap.class.getDeclaredField("table");
        tf.setAccessible(true);
        Object[] t = (Object[])tf.get(m);
        return t == null ? 0 : t.length;
    }

    public static void main(String[] a) throws Exception
    {
        BufferedReader in = new BufferedReader(new InputStreamReader(System.in));
        String line;
        while ((line = in.readLine()) != null)
        {
            line = line.trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] f = line.split(" +");
            if (f[0].equals("b"))
            {
                st = Long.parseLong(f[1]);
                int adds = Integer.parseInt(f[2]), rad = Integer.parseInt(f[3]);
                int cx = Integer.parseInt(f[4]), cy = Integer.parseInt(f[5]), cz = Integer.parseInt(f[6]);
                HashSet<Object> b = new HashSet<Object>();
                for (int i = 0; i < adds; ++i)
                {
                    int x = cx + next() % (2 * rad + 1) - rad;
                    int y = cy + next() % (2 * rad + 1) - rad;
                    int z = cz + next() % (2 * rad + 1) - rad;
                    b.add(new Q(x, y, z));
                }
                long h = 0xcbf29ce484222325L;
                for (Object o : b) { Q q = (Q)o; h = fnv(fnv(fnv(h, q.x), q.y), q.z); }
                System.out.println(line + " " + b.size() + " " + table(b) + " " + String.format("%016x", h));
                continue;
            }
            int r = Integer.parseInt(f[0]), c0 = Integer.parseInt(f[1]);
            int pcx = Integer.parseInt(f[2]), pcz = Integer.parseInt(f[3]);
            HashSet<Object> s = new HashSet<Object>();
            // a table of c0 bins, then clear(), which keeps it
            for (int i = 0; c0 > 0 && i < c0 * 3 / 4; ++i) s.add(Integer.valueOf(i));
            s.clear();
            if (c0 > 0 && table(s) != c0) throw new IllegalStateException("table " + table(s) + " for " + c0);
            for (int dx = -r; dx <= r; ++dx)
                for (int dz = -r; dz <= r; ++dz)
                    s.add(new P(dx + pcx, dz + pcz));
            long h = 0xcbf29ce484222325L;
            for (Object o : s)
            {
                P p = (P)o;
                for (int v : new int[] {p.x, p.z})
                    for (int k = 0; k < 4; ++k) { h ^= (v >>> (8 * k)) & 0xff; h *= 0x100000001b3L; }
            }
            System.out.println(r + " " + c0 + " " + pcx + " " + pcz + " " + s.size() + " " + table(s) + " " + String.format("%016x", h));
        }
    }
}
