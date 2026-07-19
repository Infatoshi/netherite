// Verbatim plant-tick math from MC 1.11.2 block classes, driven as a synthetic soil/crop
// column battery matching core/plant_growth.h (same layout, JavaRandom stream, probe dump).
// Sources (java/oracle-src/net/minecraft/block):
//   BlockCrops.updateTick + getGrowthChance
//   BlockFarmland.updateTick (+ Entity.canTrample for trample)
//   BlockSapling.updateTick + grow (stage only; stage1 -> LOG marker, no WorldGenTrees)
//   BlockCactus.updateTick, BlockReed.updateTick
//   BlockStem.updateTick (pumpkin), BlockNetherWart.updateTick
// ForgeHooks.onCropsGrowPre is a pass-through of the nextInt boolean (no event cancel).
import java.util.Random;

public class Golden {
    static final int W = 16, H = 8, VOL = W * W * H;
    static final int NTICKS = 64;
    static final int DEFAULT_SEED = 12345;

    static final int AIR = 0, GRASS = 2, DIRT = 3, SAPLING = 6, WATER = 9, SAND = 12, LOG = 17;
    static final int WHEAT = 59, FARMLAND = 60, CACTUS = 81, REEDS = 83, PUMPKIN = 86;
    static final int SOUL_SAND = 88, PUMPKIN_STEM = 104, NETHER_WART = 115;

    // HORIZONTAL.facings(): N E S W
    static int hdx(int d) { return d == 1 ? 1 : (d == 3 ? -1 : 0); }
    static int hdz(int d) { return d == 0 ? -1 : (d == 2 ? 1 : 0); }

    static int idx(int x, int y, int z) { return (y * W + z) * W + x; }
    static boolean in(int x, int y, int z) {
        return x >= 0 && x < W && y >= 0 && y < H && z >= 0 && z < W;
    }
    static int pack(int id, int meta) { return ((id & 0xFFF) << 4) | (meta & 0xF); }
    static int idOf(int s) { return s >>> 4; }
    static int metaOf(int s) { return s & 0xF; }

    int[] blocksA = new int[VOL];
    int[] blocksB = new int[VOL];
    int[] lightAbove = new int[VOL];
    int cur;
    Random rng;

    int[] now()  { return cur != 0 ? blocksB : blocksA; }
    int[] next() { return cur != 0 ? blocksA : blocksB; }

    int get(int[] b, int x, int y, int z) {
        if (!in(x, y, z)) return pack(AIR, 0);
        return b[idx(x, y, z)];
    }
    void set(int[] b, int x, int y, int z, int s) {
        if (!in(x, y, z)) return;
        b[idx(x, y, z)] = s;
    }
    void copy(int[] dst, int[] src) {
        System.arraycopy(src, 0, dst, 0, VOL);
    }

    boolean isWater(int s) {
        int id = idOf(s);
        return id == WATER || id == 8;
    }
    boolean isFarmland(int s) { return idOf(s) == FARMLAND; }
    boolean farmlandFertile(int s) { return isFarmland(s) && metaOf(s) > 0; }

    // BlockCrops.getGrowthChance
    float growthChance(int[] now, int x, int y, int z, int cropId) {
        float f = 1.0F;
        for (int i = -1; i <= 1; ++i) {
            for (int j = -1; j <= 1; ++j) {
                float f1 = 0.0F;
                int soil = get(now, x + i, y - 1, z + j);
                if (isFarmland(soil)) {
                    f1 = 1.0F;
                    if (farmlandFertile(soil)) f1 = 3.0F;
                }
                if (i != 0 || j != 0) f1 /= 4.0F;
                f += f1;
            }
        }
        boolean flag = idOf(get(now, x - 1, y, z)) == cropId || idOf(get(now, x + 1, y, z)) == cropId;
        boolean flag1 = idOf(get(now, x, y, z - 1)) == cropId || idOf(get(now, x, y, z + 1)) == cropId;
        if (flag && flag1) {
            f /= 2.0F;
        } else {
            boolean flag2 =
                idOf(get(now, x - 1, y, z - 1)) == cropId ||
                idOf(get(now, x + 1, y, z - 1)) == cropId ||
                idOf(get(now, x + 1, y, z + 1)) == cropId ||
                idOf(get(now, x - 1, y, z + 1)) == cropId;
            if (flag2) f /= 2.0F;
        }
        return f;
    }

    boolean cropCanStay(int[] now, int x, int y, int z) {
        int la = lightAbove[idx(x, y, z)];
        if (la < 8) return false;
        return isFarmland(get(now, x, y - 1, z));
    }

    void tickWheat(int[] now, int[] next, int x, int y, int z) {
        int s = get(now, x, y, z);
        if (idOf(s) != WHEAT) return;
        if (!cropCanStay(now, x, y, z)) {
            set(next, x, y, z, pack(AIR, 0));
            return;
        }
        if (lightAbove[idx(x, y, z)] < 9) return;
        int age = metaOf(s);
        if (age >= 7) return;
        float f = growthChance(now, x, y, z, WHEAT);
        int bound = (int)(25.0F / f) + 1;
        if (bound < 1) bound = 1;
        if (rng.nextInt(bound) == 0) {
            int na = age + 1;
            if (na > 7) na = 7;
            set(next, x, y, z, pack(WHEAT, na));
        }
    }

    boolean farmlandHasWater(int[] now, int x, int y, int z) {
        for (int dy = 0; dy <= 1; ++dy)
            for (int dz = -4; dz <= 4; ++dz)
                for (int dx = -4; dx <= 4; ++dx)
                    if (isWater(get(now, x + dx, y + dy, z + dz)))
                        return true;
        return false;
    }

    boolean farmlandHasCrops(int[] now, int x, int y, int z) {
        int id = idOf(get(now, x, y + 1, z));
        return id == WHEAT || id == PUMPKIN_STEM || id == SAPLING;
    }

    void tickFarmland(int[] now, int[] next, int x, int y, int z) {
        int s = get(now, x, y, z);
        if (idOf(s) != FARMLAND) return;
        int moisture = metaOf(s) & 7;
        if (!farmlandHasWater(now, x, y, z)) {
            if (moisture > 0) {
                set(next, x, y, z, pack(FARMLAND, moisture - 1));
            } else if (!farmlandHasCrops(now, x, y, z)) {
                set(next, x, y, z, pack(DIRT, 0));
            }
        } else if (moisture < 7) {
            set(next, x, y, z, pack(FARMLAND, 7));
        }
    }

    void tickSapling(int[] now, int[] next, int x, int y, int z) {
        int s = get(now, x, y, z);
        if (idOf(s) != SAPLING) return;
        if (lightAbove[idx(x, y, z)] < 9) return;
        if (rng.nextInt(7) != 0) return;
        int meta = metaOf(s);
        int stage = (meta >> 3) & 1;
        int type = meta & 7;
        if (stage == 0) {
            set(next, x, y, z, pack(SAPLING, type | (1 << 3)));
        } else {
            set(next, x, y, z, pack(LOG, type));
        }
    }

    void tickCactus(int[] now, int[] next, int x, int y, int z) {
        int s = get(now, x, y, z);
        if (idOf(s) != CACTUS) return;
        if (idOf(get(now, x, y + 1, z)) != AIR) return;
        int i;
        for (i = 1; idOf(get(now, x, y - i, z)) == CACTUS; ++i) { }
        if (i >= 3) return;
        int age = metaOf(s);
        if (age == 15) {
            set(next, x, y + 1, z, pack(CACTUS, 0));
            set(next, x, y, z, pack(CACTUS, 0));
        } else {
            set(next, x, y, z, pack(CACTUS, age + 1));
        }
    }

    void tickReed(int[] now, int[] next, int x, int y, int z) {
        int s = get(now, x, y, z);
        if (idOf(s) != REEDS) return;
        int below = idOf(get(now, x, y - 1, z));
        if (below != REEDS && below != GRASS && below != DIRT && below != SAND) return;
        if (idOf(get(now, x, y + 1, z)) != AIR) return;
        int i;
        for (i = 1; idOf(get(now, x, y - i, z)) == REEDS; ++i) { }
        if (i >= 3) return;
        int age = metaOf(s);
        if (age == 15) {
            set(next, x, y + 1, z, pack(REEDS, 0));
            set(next, x, y, z, pack(REEDS, 0));
        } else {
            set(next, x, y, z, pack(REEDS, age + 1));
        }
    }

    void tickNetherWart(int[] now, int[] next, int x, int y, int z) {
        int s = get(now, x, y, z);
        if (idOf(s) != NETHER_WART) return;
        if (idOf(get(now, x, y - 1, z)) != SOUL_SAND) {
            set(next, x, y, z, pack(AIR, 0));
            return;
        }
        int age = metaOf(s);
        if (age < 3 && rng.nextInt(10) == 0)
            set(next, x, y, z, pack(NETHER_WART, age + 1));
    }

    void tickStem(int[] now, int[] next, int x, int y, int z) {
        int s = get(now, x, y, z);
        if (idOf(s) != PUMPKIN_STEM) return;
        if (!cropCanStay(now, x, y, z)) {
            set(next, x, y, z, pack(AIR, 0));
            return;
        }
        if (lightAbove[idx(x, y, z)] < 9) return;
        float f = growthChance(now, x, y, z, PUMPKIN_STEM);
        int bound = (int)(25.0F / f) + 1;
        if (bound < 1) bound = 1;
        if (rng.nextInt(bound) != 0) return;
        int age = metaOf(s);
        if (age < 7) {
            set(next, x, y, z, pack(PUMPKIN_STEM, age + 1));
        } else {
            for (int d = 0; d < 4; ++d) {
                if (idOf(get(now, x + hdx(d), y, z + hdz(d))) == PUMPKIN)
                    return;
            }
            int dir = rng.nextInt(4);
            int fx = x + hdx(dir);
            int fz = z + hdz(dir);
            int soil = get(now, fx, y - 1, fz);
            int sid = idOf(soil);
            if (idOf(get(now, fx, y, fz)) == AIR &&
                (isFarmland(soil) || sid == DIRT || sid == GRASS)) {
                set(next, fx, y, fz, pack(PUMPKIN, 0));
            }
        }
    }

    void init(long seed) {
        cur = 0;
        rng = new Random(seed);
        for (int i = 0; i < VOL; ++i) {
            blocksA[i] = pack(AIR, 0);
            lightAbove[i] = 15;
        }
        int[] b = blocksA;

        set(b, 4, 2, 1, pack(WATER, 0));

        set(b, 3, 1, 2, pack(DIRT, 0));
        set(b, 3, 2, 2, pack(FARMLAND, 7));
        set(b, 3, 3, 2, pack(WHEAT, 0));
        lightAbove[idx(3, 3, 2)] = 15;

        set(b, 5, 1, 2, pack(DIRT, 0));
        set(b, 5, 2, 2, pack(FARMLAND, 7));
        set(b, 5, 3, 2, pack(WHEAT, 3));
        lightAbove[idx(5, 3, 2)] = 8;

        set(b, 7, 1, 2, pack(DIRT, 0));
        set(b, 7, 2, 2, pack(FARMLAND, 0));

        set(b, 3, 1, 3, pack(DIRT, 0));
        set(b, 3, 2, 3, pack(FARMLAND, 7));
        set(b, 3, 3, 3, pack(PUMPKIN_STEM, 0));
        lightAbove[idx(3, 3, 3)] = 15;
        set(b, 3, 2, 4, pack(DIRT, 0));
        set(b, 2, 2, 3, pack(DIRT, 0));
        set(b, 4, 2, 3, pack(DIRT, 0));

        set(b, 3, 1, 10, pack(DIRT, 0));
        set(b, 3, 2, 10, pack(FARMLAND, 0));
        set(b, 3, 3, 10, pack(WHEAT, 0));
        lightAbove[idx(3, 3, 10)] = 15;

        set(b, 5, 1, 10, pack(DIRT, 0));
        set(b, 5, 2, 10, pack(FARMLAND, 7));
        set(b, 5, 3, 10, pack(WHEAT, 1));
        lightAbove[idx(5, 3, 10)] = 15;

        set(b, 7, 1, 10, pack(DIRT, 0));
        set(b, 7, 2, 10, pack(FARMLAND, 0));

        set(b, 9, 1, 10, pack(DIRT, 0));
        set(b, 9, 2, 10, pack(FARMLAND, 7));

        set(b, 1, 1, 6, pack(SAND, 0));
        set(b, 1, 2, 6, pack(CACTUS, 14));

        set(b, 4, 1, 6, pack(DIRT, 0));
        set(b, 5, 1, 6, pack(WATER, 0));
        set(b, 4, 2, 6, pack(REEDS, 14));

        set(b, 7, 1, 6, pack(DIRT, 0));
        set(b, 7, 2, 6, pack(SAPLING, 0));
        lightAbove[idx(7, 2, 6)] = 15;

        set(b, 10, 1, 6, pack(SOUL_SAND, 0));
        set(b, 10, 2, 6, pack(NETHER_WART, 0));

        copy(blocksB, blocksA);
    }

    void tick() {
        int[] now = now();
        int[] next = next();
        copy(next, now);
        for (int z = 0; z < W; ++z) {
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    int id = idOf(get(now, x, y, z));
                    switch (id) {
                        case WHEAT:        tickWheat(now, next, x, y, z); break;
                        case FARMLAND:     tickFarmland(now, next, x, y, z); break;
                        case SAPLING:      tickSapling(now, next, x, y, z); break;
                        case CACTUS:       tickCactus(now, next, x, y, z); break;
                        case REEDS:        tickReed(now, next, x, y, z); break;
                        case NETHER_WART:  tickNetherWart(now, next, x, y, z); break;
                        case PUMPKIN_STEM: tickStem(now, next, x, y, z); break;
                        default: break;
                    }
                }
            }
        }
        // trample col L: fallDistance=1.0F -> nextFloat() < 0.5F
        {
            int x = 9, y = 2, z = 10;
            int s = get(next, x, y, z);
            if (idOf(s) == FARMLAND) {
                if (rng.nextFloat() < 1.0F - 0.5F)
                    set(next, x, y, z, pack(DIRT, 0));
            }
        }
        cur ^= 1;
    }

    void run(long seed, int nticks) {
        init(seed);
        for (int t = 0; t < nticks; ++t) tick();
    }

    static final int[][] PROBES = {
        {3,3,2},  // A wheat wet
        {3,3,10}, // B wheat dry
        {5,3,2},  // C wheat low light
        {7,2,2},  // D farmland hydrate
        {5,2,10}, // E farmland dry+crop
        {7,2,10}, // F dry no crop
        {5,3,10}, // E wheat
        {1,2,6},  // cactus base
        {1,3,6},  // cactus grown
        {4,2,6},  // reed base
        {4,3,6},  // reed grown
        {7,2,6},  // sapling/log
        {10,2,6}, // nether wart
        {3,3,3},  // stem
        {3,3,4},  // stem S fruit
        {9,2,10}, // trample
    };

    public static void main(String[] args) {
        long seed = args.length > 0 ? Long.parseLong(args[0]) : DEFAULT_SEED;
        int nticks = args.length > 1 ? Integer.parseInt(args[1]) : NTICKS;
        Golden g = new Golden();
        g.run(seed, nticks);
        int[] b = g.now();
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < PROBES.length; ++i) {
            int x = PROBES[i][0], y = PROBES[i][1], z = PROBES[i][2];
            int s = g.get(b, x, y, z);
            sb.append(idOf(s)).append(' ').append(metaOf(s)).append('\n');
        }
        System.out.print(sb);
    }
}
