// Verbatim MC 1.11.2 Explosion.doExplosionA block-ray + entity damage math on a synthetic
// cubic grid. Sources:
//   net/minecraft/world/Explosion.java doExplosionA
//   net/minecraft/util/math/MathHelper.java floor + sqrt
//   net/minecraft/block/Block.java hardness (resistance = hardness for table blocks)
//
// RAND-FREE: per-ray size scale uses fixed rand=0.5F:
//   f = size * (0.7F + 0.5F * 0.6F)
// Particles/sound/drops/fire (doExplosionB) CUT.
// Output mirrors core/explosion.h: count, packed destroyed (x<<8|y<<4|z) sorted x,y,z,
// then 3 entity damage floats (open exposure=1.0). Values as %016x of zero-extended u32 /
// float raw bits.

public class Golden {

    static final int DIM = 16;
    static final int VOL = DIM * DIM * DIM;
    static final int FACE = 16;
    static final int NUM_SCENARIOS = 5;
    static final int NUM_ENTITIES = 3;

    // ---- hardness table (mirrors mc_bpt_props for air/stone/dirt) ----
    static float hardness(int id) {
        if (id <= 0) return 0.0F;
        if (id == 1) return 1.5F; // stone
        if (id == 3) return 0.5F; // dirt
        return 1.5F;
    }

    static int idx(int x, int y, int z) {
        return (y * DIM + z) * DIM + x;
    }

    static boolean in(int x, int y, int z) {
        return x >= 0 && x < DIM && y >= 0 && y < DIM && z >= 0 && z < DIM;
    }

    // MathHelper.floor(double)
    static int floor(double value) {
        int i = (int) value;
        return value < (double) i ? i - 1 : i;
    }

    // MathHelper.sqrt(double) = (float)Math.sqrt, then widened (Entity.getDistance)
    static double dist(double dx, double dy, double dz) {
        return (double) (float) Math.sqrt(dx * dx + dy * dy + dz * dz);
    }

    static float densityScale() {
        return 0.7F + 0.5F * 0.6F;
    }

    // doExplosionA block rays; mark non-air in-bounds destroyed cells
    static void doExplosionBlocks(int[] grid, double ex, double ey, double ez,
                                  float size, boolean[] hit) {
        for (int i = 0; i < VOL; ++i) hit[i] = false;

        float dens = densityScale();
        final float stepDec = 0.22500001F;
        final double stepAdv = 0.30000001192092896D;

        for (int j = 0; j < FACE; ++j) {
            for (int k = 0; k < FACE; ++k) {
                for (int l = 0; l < FACE; ++l) {
                    if (j == 0 || j == 15 || k == 0 || k == 15 || l == 0 || l == 15) {
                        double d0 = (double) ((float) j / 15.0F * 2.0F - 1.0F);
                        double d1 = (double) ((float) k / 15.0F * 2.0F - 1.0F);
                        double d2 = (double) ((float) l / 15.0F * 2.0F - 1.0F);
                        double d3 = Math.sqrt(d0 * d0 + d1 * d1 + d2 * d2);
                        d0 = d0 / d3;
                        d1 = d1 / d3;
                        d2 = d2 / d3;
                        float f = size * dens;
                        double d4 = ex;
                        double d6 = ey;
                        double d8 = ez;

                        for (float f1 = 0.3F; f > 0.0F; f -= stepDec) {
                            int bx = floor(d4);
                            int by = floor(d6);
                            int bz = floor(d8);
                            int st = in(bx, by, bz) ? grid[idx(bx, by, bz)] : 0;

                            if (st > 0) {
                                float f2 = hardness(st);
                                f -= (f2 + 0.3F) * 0.3F;
                            }

                            if (f > 0.0F && in(bx, by, bz) && st > 0) {
                                hit[idx(bx, by, bz)] = true;
                            }

                            d4 += d0 * stepAdv;
                            d6 += d1 * stepAdv;
                            d8 += d2 * stepAdv;
                        }
                    }
                }
            }
        }
    }

    // entity damage: open exposure=1, no blast protection
    static float entityDamage(double entX, double entY, double entZ,
                              double ex, double ey, double ez,
                              float size, float exposure) {
        float f3 = size * 2.0F;
        if (f3 <= 0.0F) return 0.0F;
        double d12 = dist(entX - ex, entY - ey, entZ - ez) / (double) f3;
        if (d12 > 1.0D) return 0.0F;
        double d10 = (1.0D - d12) * (double) exposure;
        return (float) ((int) ((d10 * d10 + d10) / 2.0D * 7.0D * (double) f3 + 1.0D));
    }

    static float scenarioSize(int idx) {
        switch (idx) {
            case 0: return 4.0F;
            case 1: return 4.0F;
            case 2: return 2.0F;
            case 3: return 4.0F;
            default: return 1.0F;
        }
    }

    static void fillGrid(int idx, int[] grid) {
        int air = 0, stone = 1, dirt = 3;
        if (idx == 0) {
            for (int i = 0; i < VOL; ++i) grid[i] = air;
        } else if (idx == 1 || idx == 2) {
            for (int i = 0; i < VOL; ++i) grid[i] = stone;
        } else if (idx == 3) {
            for (int y = 0; y < DIM; ++y)
                for (int z = 0; z < DIM; ++z)
                    for (int x = 0; x < DIM; ++x)
                        grid[idx(x, y, z)] = (y < 8) ? dirt : stone;
        } else {
            for (int i = 0; i < VOL; ++i) grid[i] = dirt;
        }
    }

    static void entityPos(int ei, double[] out) {
        switch (ei) {
            case 0: out[0] = 8.0; out[1] = 8.0; out[2] = 8.0; break;
            case 1: out[0] = 8.0; out[1] = 8.0; out[2] = 4.0; break;
            default: out[0] = 8.0; out[1] = 8.0; out[2] = 1.0; break;
        }
    }

    static void emitU32(int v) {
        System.out.printf("%016x%n", v & 0xffffffffL);
    }

    static void emitFloat(float v) {
        System.out.printf("%016x%n", Float.floatToRawIntBits(v) & 0xffffffffL);
    }

    static void runScenario(int sidx) {
        int[] grid = new int[VOL];
        boolean[] hit = new boolean[VOL];
        float size = scenarioSize(sidx);
        double ox = 8.0, oy = 8.0, oz = 8.0;
        fillGrid(sidx, grid);
        doExplosionBlocks(grid, ox, oy, oz, size, hit);

        int count = 0;
        for (int x = 0; x < DIM; ++x)
            for (int y = 0; y < DIM; ++y)
                for (int z = 0; z < DIM; ++z)
                    if (hit[idx(x, y, z)]) ++count;
        emitU32(count);
        for (int x = 0; x < DIM; ++x)
            for (int y = 0; y < DIM; ++y)
                for (int z = 0; z < DIM; ++z)
                    if (hit[idx(x, y, z)])
                        emitU32((x << 8) | (y << 4) | z);

        float exposure = 1.0F;
        double[] epos = new double[3];
        for (int ei = 0; ei < NUM_ENTITIES; ++ei) {
            entityPos(ei, epos);
            emitFloat(entityDamage(epos[0], epos[1], epos[2], ox, oy, oz, size, exposure));
        }
    }

    public static void main(String[] args) {
        int sel = args.length > 0 ? Integer.parseInt(args[0]) : -1;
        if (sel >= 0 && sel < NUM_SCENARIOS) {
            runScenario(sel);
        } else {
            for (int i = 0; i < NUM_SCENARIOS; ++i)
                runScenario(i);
        }
    }
}
